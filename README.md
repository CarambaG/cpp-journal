# Журналирование на C++17 — части 1, 2 и 3

Решение тестового задания: потокобезопасная библиотека для записи сообщений в
файл или TCP-сокет, многопоточное консольное приложение для её проверки и
TCP-программа сбора статистики. Используются только C++17/STL, CMake и
системный сокетный API.
Целевая операционная система — актуальная версия Ubuntu/Debian, компилятор —
GCC.

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
  всегда занимает одну строку журнала;
- отправка записей на TCP-сервер через альтернативную реализацию
  `SocketLogger`;
- поддержка IPv4/IPv6 и доменных имён через `getaddrinfo`;
- корректная отправка всей записи даже при частичной записи в сокет;
- приём записей от нескольких TCP-клиентов;
- статистика общего количества сообщений, количества по уровням и за
  последний час;
- статистика минимальной, максимальной и средней длины сообщений;
- вывод статистики после каждого N-го сообщения и по таймауту T.

Формат записи:

```text
[2026-08-18T10:15:30.042Z] [INFO] service started
```

Уровень по умолчанию одновременно является минимальным уровнем записи. Вызов
`log(message)` присваивает сообщению этот уровень. Перегрузка
`log(message, level)` позволяет указать уровень явно.

## Структура

```text
include/journal/logger.hpp        — уровни, результаты и общий интерфейс
include/journal/file_logger.hpp   — файловая реализация
include/journal/socket_logger.hpp — TCP-реализация
src/file_logger.cpp               — реализация библиотеки
src/socket_logger.cpp             — подключение и отправка через TCP
apps/journal_cli/                 — многопоточное консольное приложение
apps/journal_stats/               — TCP-сервер и сборщик статистики
tests/*_tests.cpp                 — тесты библиотеки и приложения
CMakeLists.txt                    — отдельные цели библиотек, приложения и тестов
```

Обе реализации наследуют `ILogger`, поэтому место назначения можно менять без
изменения клиентской логики.

## Подготовка Ubuntu/Debian

Установите GCC, CMake, Git и `netcat` для ручной проверки сокетного логгера:

```bash
sudo apt update
sudo apt install -y build-essential cmake git netcat-openbsd
```

Проверьте установленные версии:

```bash
g++ --version
cmake --version
git --version
```

## Получение исходного кода

Для первого запуска клонируйте репозиторий:

```bash
git clone https://github.com/CarambaG/cpp-journal.git
cd cpp-journal
```

Если репозиторий уже клонирован, получите последние изменения:

```bash
cd cpp-journal
git pull --ff-only
```

## Release-сборка и тестирование

Создайте конфигурацию Release:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Соберите библиотеки, приложение и тесты. `nproc` передаёт CMake количество
доступных логических процессоров:

```bash
cmake --build build --parallel "$(nproc)"
```

Запустите все тесты:

```bash
ctest --test-dir build --output-on-failure
```

Для подробного вывода каждого теста:

```bash
ctest --test-dir build --verbose
```

Результаты сборки находятся в каталоге `build`:

- `libjournal.a` — статическая библиотека;
- `libjournal.so` — динамическая библиотека;
- `journal_cli` — консольное приложение;
- `journal_stats` — TCP-программа сбора статистики;
- `journal_tests` — тесты библиотеки;
- `journal_cli_tests` — тесты приложения;
- `journal_stats_tests` — тесты парсинга, статистики и TCP-интеграции.

## Сборка отдельных целей

Можно собрать цели отдельно:

```bash
cmake --build build --target journal_static --parallel "$(nproc)"
cmake --build build --target journal_shared --parallel "$(nproc)"
cmake --build build --target journal_cli --parallel "$(nproc)"
cmake --build build --target journal_stats --parallel "$(nproc)"
cmake --build build --target journal_tests --parallel "$(nproc)"
cmake --build build --target journal_cli_tests --parallel "$(nproc)"
cmake --build build --target journal_stats_tests --parallel "$(nproc)"
```

Тесты проверяют обязательные поля записи, фильтрацию, смену уровня, режим
добавления, экранирование, ошибку открытия файла, некорректный уровень и
конкурентную запись из нескольких потоков. Отдельный набор тестов приложения
проверяет разбор ввода, порядок фоновой записи, освобождение очереди перед
завершением, конкурентную отправку сообщений и обработку ошибок записи. Тесты
части 3 проверяют сетевой формат, статистику, правила N/T и полный путь от
`SocketLogger` до TCP-сервера.

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

После завершения посмотрите содержимое журнала:

```bash
cat application.log
```

## Сбор статистики по TCP — часть 3

`journal_stats` прослушивает TCP-сокет и принимает строки, сформированные
`SocketLogger`. Параметры запуска:

```text
journal_stats <адрес> <порт> <N> <T-секунд>
```

Например, выводить статистику после каждого 10-го сообщения либо через 5
секунд после предыдущего вывода, если данные изменились:

```bash
./build/journal_stats 0.0.0.0 9000 10 5
```

Для приёма только локальных подключений используйте:

```bash
./build/journal_stats 127.0.0.1 9000 10 5
```

Параметры означают:

- `0.0.0.0` или `127.0.0.1` — адрес прослушивания;
- `9000` — TCP-порт;
- `10` — значение N;
- `5` — таймаут T в секундах.

`SocketLogger` клиента должен подключаться к тому же адресу и порту:

```cpp
journal::SocketLogger logger("127.0.0.1", 9000,
                             journal::Level::Debug);
logger.log("service started", journal::Level::Info);
```

Для быстрой ручной проверки без отдельной клиентской программы можно отправить
строку через `netcat`:

```bash
printf '[2026-08-20T10:15:30.042Z] [INFO] service started\n' | \
  nc 127.0.0.1 9000
```

Сервер выводит принятую запись и статистику:

```text
Received: [2026-08-20T10:15:30.042Z] [INFO] service started
--- Statistics ---
Total messages: 1
By level: DEBUG=0 INFO=1 ERROR=0
Last hour: 1
Message length (bytes): min=15 max=15 avg=15.00
```

Количество за последний час рассчитывается по времени получения сообщения
сервером. Длина считается по исходному тексту сообщения после снятия
экранирования `\\n`, `\\r` и `\\\\`. TCP-фрагменты объединяются до символа
перевода строки, поэтому частичные и несколько последовательных `send()`
обрабатываются корректно. Некорректные записи не включаются в статистику.

Сервер поддерживает несколько одновременных клиентов и корректно завершается
по `Ctrl+C` (`SIGINT`) или `SIGTERM`. При остановке изменившаяся статистика
выводится последний раз.

## Debug-сборка

Для отладки создайте отдельный каталог сборки:

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --parallel "$(nproc)"
ctest --test-dir build-debug --output-on-failure
```

Запуск Debug-версии приложения:

```bash
./build-debug/journal_cli application-debug.log debug
```

## Очистка и повторная сборка

Очистите созданные объектные файлы и бинарные файлы:

```bash
cmake --build build --target clean
```

Соберите проект повторно:

```bash
cmake --build build --parallel "$(nproc)"
```

## Установка

Установите библиотеку, заголовочные файлы, `journal_cli` и `journal_stats` в
пользовательский каталог без прав суперпользователя:

```bash
cmake --install build --prefix "$HOME/.local"
```

Запуск установленного приложения:

```bash
"$HOME/.local/bin/journal_cli" application.log info
"$HOME/.local/bin/journal_stats" 127.0.0.1 9000 10 5
```

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
неготовым.
