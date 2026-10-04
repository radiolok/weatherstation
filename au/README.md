# Сборочные единицы

Внешние репозитории, от которых зависит проект. Каждый подключён как git submodule, поэтому версия зафиксирована коммитом в этом репозитории.

| Папка | Репозиторий | Где используется |
| --- | --- | --- |
| [`metar_cpp`](https://github.com/radiolok/metar_cpp) | radiolok/metar_cpp | Прошивка: разбор сводок METAR для резервного прогноза. Подключается как модуль Zephyr, нужны `CONFIG_CPP`, `CONFIG_STD_CPP17`, `CONFIG_REQUIRES_FULL_LIBCPP` |

Получить сабмодули после обычного клонирования:

```sh
git submodule update --init --recursive
```

Добавить новую сборочную единицу:

```sh
git submodule add https://github.com/<owner>/<repo> au/<repo>
```

Обновить до последнего коммита ветки по умолчанию:

```sh
git submodule update --remote au/<repo>
git commit -am "au: update <repo>"
```
