#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/http/client.h>
#include <zephyr/data/json.h>
#include <zephyr/logging/log.h>

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// === Настройки WiFi ===
#define WIFI_SSID     "YourSSID"
#define WIFI_PSK      "YourPassword"

// === Адрес локального сервера ===
#define SERVER_HOST   "192.168.1.100"   // IP вашего сервера
#define SERVER_PORT   8080               // порт
#define WEATHER_PATH   "/weather.json"    // путь к файлу с погодой

// === Пины устройств (если не заданы в overlay) ===
// Реле на GPIO4
#define RELAY_NODE DT_ALIAS(relay)       // можно задать алиас в overlay
static const struct gpio_dt_spec relay = GPIO_DT_SPEC_GET_OR(RELAY_NODE, gpios, {0});

// Датчики
static const struct device *const dht21 = DEVICE_DT_GET_ANY(dht);
static const struct device *const mhz19b = DEVICE_DT_GET_ANY(winsen_mhz19b);
static const struct device *const uart_display = DEVICE_DT_GET(DT_NODELABEL(uart2));

// === Структура для хранения данных с сервера ===
struct weather_data {
    float temp;
    float pressure;
    int humidity;
    // ... добавьте свои поля
};

// === Глобальные переменные ===
static struct weather_data current_weather;
static K_SEM_DEFINE(weather_ready_sem, 0, 1);   // семафор для сигнала о новых данных
static bool wifi_connected = false;

// === Прототипы функций ===
static void wifi_connect(void);
static void http_fetch_weather(void);
static void parse_weather_json(const char *json_str);
static void read_local_sensors(float *temp, float *hum, uint16_t *co2);
static void update_display(void);

// === Поток для периодического опроса сервера (раз в 30 минут) ===
void weather_thread(void *, void *, void *)
{
    while (1) {
        LOG_INF("Fetching weather from server...");
        http_fetch_weather();
        k_sleep(K_MINUTES(30));
    }
}

K_THREAD_DEFINE(weather_tid, 4096, weather_thread, NULL, NULL, NULL, 7, 0, 0);

// === Поток для локального опроса датчиков и вывода на дисплей (раз в минуту) ===
void sensor_thread(void *, void *, void *)
{
    while (1) {
        // Читаем локальные датчики
        float temp_local = 0, hum_local = 0;
        uint16_t co2_local = 0;
        read_local_sensors(&temp_local, &hum_local, &co2_local);

        // Если есть свежие данные с сервера, используем их, иначе пропускаем
        if (k_sem_take(&weather_ready_sem, K_NO_WAIT) == 0) {
            LOG_INF("New weather data: T=%.1f, P=%.1f, H=%d",
                    current_weather.temp, current_weather.pressure, current_weather.humidity);
        }

        // Формируем строку для отправки на индикатор
        char display_buf[128];
        snprintf(display_buf, sizeof(display_buf),
                 "Local: T=%.1f H=%.1f CO2=%d  Yandex: T=%.1f H=%d\r\n",
                 temp_local, hum_local, co2_local,
                 current_weather.temp, current_weather.humidity);

        // Отправляем в UART (RS-485)
        for (int i = 0; i < strlen(display_buf); i++) {
            uart_poll_out(uart_display, display_buf[i]);
        }

        k_sleep(K_MINUTES(1));
    }
}

K_THREAD_DEFINE(sensor_tid, 2048, sensor_thread, NULL, NULL, NULL, 5, 0, 0);

// === Инициализация ===
void main(void)
{
    LOG_INF("Weather Station starting...");

    // Инициализация реле (выключено)
    if (!device_is_ready(relay.port)) {
        LOG_ERR("Relay GPIO port not ready");
    } else {
        gpio_pin_configure_dt(&relay, GPIO_OUTPUT_INACTIVE);
        LOG_INF("Relay initialized");
    }

    // Инициализация датчиков
    if (dht21 && device_is_ready(dht21)) {
        LOG_INF("DHT21 sensor ready");
    } else {
        LOG_ERR("DHT21 sensor not ready");
    }

    if (mhz19b && device_is_ready(mhz19b)) {
        LOG_INF("MH-Z19B sensor ready");
    } else {
        LOG_ERR("MH-Z19B sensor not ready");
    }

    // Подключение к WiFi
    wifi_connect();

    // Потоки запустятся автоматически после определения
}

// === Подключение к WiFi (синхронно) ===
static void wifi_connect(void)
{
    struct net_if *iface = net_if_get_default();
    if (!iface) {
        LOG_ERR("No network interface");
        return;
    }

    struct wifi_connect_req_params params = {
        .ssid = WIFI_SSID,
        .psk = WIFI_PSK,
        .ssid_length = strlen(WIFI_SSID),
        .psk_length = strlen(WIFI_PSK),
        .channel = WIFI_CHANNEL_ANY,
        .security = WIFI_SECURITY_TYPE_PSK,
    };

    LOG_INF("Connecting to WiFi...");
    int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
    if (rc) {
        LOG_ERR("WiFi connection failed: %d", rc);
        return;
    }
    wifi_connected = true;
    LOG_INF("WiFi connected");
}

// === HTTP GET запрос к серверу ===
static void http_fetch_weather(void)
{
    if (!wifi_connected) {
        LOG_ERR("WiFi not connected");
        return;
    }

    struct http_request req;
    memset(&req, 0, sizeof(req));

    req.method = HTTP_GET;
    req.url = WEATHER_PATH;
    req.host = SERVER_HOST;
    req.port = SERVER_PORT;
    req.protocol = "HTTP/1.1";
    req.response = http_response_cb;  // колбэк для обработки ответа
    req.recv_buf = NULL;               // можно использовать статический буфер

    int rc = http_client_req(&req);
    if (rc < 0) {
        LOG_ERR("HTTP request failed: %d", rc);
    }
}

// === Колбэк для обработки ответа сервера ===
static void http_response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    if (final_data == HTTP_DATA_FINAL) {
        if (rsp->http_status_code == 200) {
            LOG_INF("HTTP OK, length: %zd", rsp->content_length);
            // Здесь нужно разобрать JSON, который лежит в rsp->recv_buf или в rsp->body_fragments
            // Для простоты будем считать, что весь ответ помещается в recv_buf.
            // На практике используйте http_response_parse_body() или сохраняйте фрагменты.
            // Предположим, что ответ получен целиком:
            if (rsp->recv_buf_len > 0) {
                parse_weather_json(rsp->recv_buf);
            }
        } else {
            LOG_ERR("HTTP error: %d", rsp->http_status_code);
        }
    }
}

// === Парсинг JSON с помощью cJSON ===
static void parse_weather_json(const char *json_str)
{
    // Пример парсинга, адаптируйте под свой формат
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        LOG_ERR("JSON parse error");
        return;
    }

    cJSON *temp = cJSON_GetObjectItem(root, "temp");
    cJSON *pressure = cJSON_GetObjectItem(root, "pressure");
    cJSON *humidity = cJSON_GetObjectItem(root, "humidity");

    if (cJSON_IsNumber(temp)) {
        current_weather.temp = temp->valuedouble;
    }
    if (cJSON_IsNumber(pressure)) {
        current_weather.pressure = pressure->valuedouble;
    }
    if (cJSON_IsNumber(humidity)) {
        current_weather.humidity = humidity->valueint;
    }

    cJSON_Delete(root);

    // Сигнализируем, что данные обновлены
    k_sem_give(&weather_ready_sem);
}

// === Чтение локальных датчиков ===
static void read_local_sensors(float *temp, float *hum, uint16_t *co2)
{
    if (dht21) {
        sensor_sample_fetch(dht21);
        struct sensor_value val;
        sensor_channel_get(dht21, SENSOR_CHAN_AMBIENT_TEMP, &val);
        *temp = sensor_value_to_double(&val);
        sensor_channel_get(dht21, SENSOR_CHAN_HUMIDITY, &val);
        *hum = sensor_value_to_double(&val);
    }

    if (mhz19b) {
        sensor_sample_fetch(mhz19b);
        struct sensor_value val;
        sensor_channel_get(mhz19b, SENSOR_CHAN_CO2, &val);
        *co2 = val.val1;  // для MH-Z19B CO2 возвращается в val1 как целое (ppm)
    }
}