# Библиотека журналирования на C++17 — часть 1

Решение первой части тестового задания: потокобезопасная библиотека для записи
текстовых сообщений в файл. Используются только C++17/STL, CMake и GCC.

## Возможности

- три уровня важности: `Debug`, `Info`, `Error`;
- фильтрация сообщений ниже текущего уровня по умолчанию;
- изменение уровня после создания объекта;
- запись времени получения сообщения в UTC с точностью до миллисекунд;
- сборка статической (`libjournal.a`) и динамической (`libjournal.so`)
  библиотек отдельными CMake-целями;
- потокобезопасные запись и изменение уровня;
- явные коды результата вместо исключений в бизнес-логике;
- сохранение предыдущих записей: файл открывается в режиме добавления;
- экранирование переводов строк и обратной косой черты, поэтому одна запись
  всегда занимает одну строку журнала.

Формат записи:

```text
[2026-08-18T10:15:30.042Z] [INFO] service started
```

Уровень по умолчанию одновременно является минимальным уровнем записи. Вызов
`log(message)` присваивает сообщению этот уровень. Перегрузка
`log(message, level)` позволяет указать уровень явно.

## Структура

```text
include/journal/logger.hpp       — уровни, результаты и общий интерфейс
include/journal/file_logger.hpp  — файловая реализация
src/file_logger.cpp              — реализация библиотеки
tests/file_logger_tests.cpp      — автономные unit-тесты
CMakeLists.txt                   — цели static/shared/tests
```

Общий интерфейс `ILogger` оставляет возможность добавить сокетную реализацию
без изменения клиентского кода, как предложено в дополнительном пункте задания.

## Сборка и тестирование

Требования: компилятор с поддержкой C++17 (GCC или MSVC), CMake 3.16+.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Для Visual Studio в Windows CMake создаёт многоконфигурационный проект, поэтому
конфигурацию нужно передавать на этапе сборки и запуска тестов:

```powershell
cmake -S . -B build
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

В Windows статическая библиотека называется `journal_static.lib`, а
динамическая поставляется как `journal.dll` вместе с import-библиотекой
`journal.lib`. Разные имена предотвращают конфликт двух `.lib` файлов.

Можно собрать цели отдельно:

```bash
cmake --build build --target journal_static
cmake --build build --target journal_shared
cmake --build build --target journal_tests
```

Тесты проверяют обязательные поля записи, фильтрацию, смену уровня, режим
добавления, экранирование, ошибку открытия файла, некорректный уровень и
конкурентную запись из нескольких потоков.

## Пример использования

```cpp
#include "journal/file_logger.hpp"

int main() {
    journal::FileLogger logger("application.log", journal::Level::Info);
    if (!logger.isReady()) {
        return 1;
    }

    const auto first = logger.log("application started");
    const auto filtered = logger.log("diagnostic details",
                                     journal::Level::Debug);

    if (!logger.setDefaultLevel(journal::Level::Debug)) {
        return 2;
    }
    const auto second = logger.log("diagnostic details",
                                   journal::Level::Debug);

    return first == journal::WriteResult::Written &&
                   filtered == journal::WriteResult::Filtered &&
                   second == journal::WriteResult::Written
               ? 0
               : 3;
}
```

Для статической линковки используйте цель `Journal::static`, для динамической —
`Journal::shared` внутри того же CMake-проекта.
