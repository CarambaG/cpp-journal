# Журналирование на C++17 — части 1 и 2

Решение первых двух частей тестового задания: потокобезопасная библиотека для
записи сообщений в файл или TCP-сокет и многопоточное консольное приложение
для её проверки. Используются только C++17/STL, CMake и системный сокетный API.

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
- отправка записей на TCP-сервер через альтернативную реализацию
  `SocketLogger`;
- поддержка IPv4/IPv6 и доменных имён через `getaddrinfo`;
- корректная отправка всей записи даже при частичной записи в сокет.

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
include/journal/socket_logger.hpp — TCP-реализация
src/file_logger.cpp              — реализация библиотеки
src/socket_logger.cpp            — подключение и отправка через TCP
apps/journal_cli/                — многопоточное консольное приложение
tests/*_tests.cpp                — тесты библиотеки и приложения
CMakeLists.txt                   — отдельные цели библиотек, приложения и тестов
```

Обе реализации наследуют `ILogger`, поэтому место назначения можно менять без
изменения клиентской логики.

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
cmake --build build --target journal_cli
cmake --build build --target journal_tests
cmake --build build --target journal_cli_tests
```

Тесты проверяют обязательные поля записи, фильтрацию, смену уровня, режим
добавления, экранирование, ошибку открытия файла, некорректный уровень и
конкурентную запись из нескольких потоков. Отдельный набор тестов приложения
проверяет разбор ввода, порядок фоновой записи, освобождение очереди перед
завершением, конкурентную отправку сообщений и обработку ошибок записи.

## Консольное приложение — часть 2

`journal_cli` принимает сообщения в основном потоке и передаёт их через
защищённую `std::mutex` очередь в отдельный поток записи. `condition_variable`
позволяет фоновому потоку ожидать новые сообщения без активного опроса. После
помещения сообщения в очередь приложение сразу готово принимать следующий
ввод. При завершении все оставшиеся сообщения записываются до вызова `join()`.

Параметры запуска: путь к файлу журнала и уровень по умолчанию:

```bash
./build/journal_cli application.log info
```

Для Windows с генератором Visual Studio:

```powershell
.\build\Release\journal_cli.exe application.log info
```

Поддерживаются уровни `debug`, `info`, `error`. Сообщение можно вводить без
уровня или указать его в квадратных скобках:

```text
service started
[debug] request payload received
[error] database connection failed
```

Команды приложения:

- `/help` — показать подсказку;
- `/quit` или `/exit` — записать оставшиеся сообщения и завершить работу.

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

## Отправка журнала через TCP-сокет

`SocketLogger` подключается к TCP-серверу в конструкторе. Каждая запись
отправляется отдельной строкой в том же формате, что используется в файле.

```cpp
#include "journal/socket_logger.hpp"

int main() {
    journal::SocketLogger logger("127.0.0.1", 9000,
                                 journal::Level::Info);
    if (!logger.isReady()) {
        return 1;
    }

    return logger.log("application started") ==
                   journal::WriteResult::Written
               ? 0
               : 2;
}
```

Для быстрой локальной проверки можно сначала запустить TCP-приёмник:

```bash
nc -l 9000
```

После запуска программы в терминал сервера придёт строка вида:

```text
[2026-08-19T10:15:30.042Z] [INFO] application started
```

Соединение устанавливается один раз при создании объекта. Если подключение не
удалось, `isReady()` возвращает `false`, а `log()` — `NotReady`. Ошибка уже во
время отправки возвращается как `WriteError`; после неё логгер считается
неготовым. На Windows CMake автоматически добавляет системную библиотеку
`ws2_32`.
