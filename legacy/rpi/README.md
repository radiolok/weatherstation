# Версия 1: Raspberry Pi

Первая версия метеостанции. Raspberry Pi раз в 30 минут запрашивает прогноз у Яндекса и сохраняет JSON, а раз в минуту второй скрипт рисует экраны и отправляет их на табло через USB-RS-485. Комнатные температура, влажность и давление — с BME280 по I²C. Подсветку включает отдельное реле Sonoff.

Версия 2 на ESP32-S3 заменяет всё это, см. [корневой README](../../README.md). Здесь код сохранён как есть: из `scripts/futaba.py` взят протокол табло Mobitec.

## Файлы

| Файл | Назначение |
| --- | --- |
| `scripts/yandex_w.py` | Запрос прогноза у Яндекса, сохранение в `yandex-weather.json`. Ключ API в `yandex-secret.key`, координаты в `gps.key` |
| `scripts/futaba.py` | Отрисовка экранов и отправка на табло: 4800 8N1, кадр `FF 06 A2 … CS FF` |
| `scripts/update.sh` | Обёртка для cron: запуск `futaba.py` на `/dev/ttyUSB0` |
| `scripts/bme280_test.py` | Проверка датчика BME280 |
| `img/` | Иконки погоды и значков, `w_pic.json` — соответствие условий Яндекса иконкам |
| `GP4.ttf` | Шрифт для текстовых надписей |

## Установка

```sh
pip3 install yaweather pillow RPi.bme280 smbus2
```

В `~/.local/lib/python3.10/site-packages/yaweather/api.py` заменить `forecast` на `informers` — так этого требует Яндекс.

crontab:

```
0  *    * * *   ubuntu    python3 /home/ubuntu/weatherstation/yandex_w.py > /dev/null
30 *    * * *   ubuntu    python3 /home/ubuntu/weatherstation/yandex_w.py > /dev/null
*  *    * * *   root    /home/ubuntu/weatherstation/update.sh
```
