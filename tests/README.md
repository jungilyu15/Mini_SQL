# Tests

Build the app:

```sh
cc -std=c11 -Wall -Wextra -pedantic main.c parser.c executor.c schema_manager.c storage.c -o mini_sql
```

Build the schema manager unit test:

```sh
cc -std=c11 -Wall -Wextra -pedantic -I. tests/test_schema_manager.c schema_manager.c -o tests/test_schema_manager
```

Run the schema manager unit test:

```sh
./tests/test_schema_manager
```

The schema manager test covers schema loading, command model shape checks,
`validate_values`, and `cast_value`.
Build the parser unit test:

```sh
cc -std=c11 -Wall -Wextra -pedantic -I. tests/test_parser.c parser.c -o tests/test_parser
```

Run the parser unit test:

```sh
./tests/test_parser
```

The parser test currently covers INSERT parsing and `SELECT * FROM <table>` parsing only.

Build the executor unit test:

```sh
cc -std=c11 -Wall -Wextra -pedantic -I. tests/test_executor.c executor.c schema_manager.c storage.c -o tests/test_executor
```

Run the executor unit test:

```sh
./tests/test_executor
```

The executor test covers minimal INSERT/SELECT execution flow only.

Build the main CLI integration test:

```sh
cc -std=c11 -Wall -Wextra -pedantic -I. tests/test_main.c parser.c executor.c schema_manager.c storage.c -o tests/test_main
```

Run the main CLI integration test:

```sh
./tests/test_main
```

The main test covers SQL file reading, statement splitting, empty statement skipping,
and end-to-end INSERT/SELECT execution through the CLI entrypoint.

Build the storage unit test:

```sh
cc -std=c11 -Wall -Wextra -pedantic -I. tests/test_storage.c storage.c schema_manager.c -o tests/test_storage
```

Run the storage unit test:

```sh
./tests/test_storage
```

The storage test binary changes directory to `tests/fixtures` internally and
cleans up the CSV files it creates during the run.

Current smoke scenarios for the app:

```sh
./mini_sql
./mini_sql missing.sql
./mini_sql sample/basic.sql
./mini_sql sample/insert_users.sql
./mini_sql sample/select_users.sql
```

Expected current behavior:

- no-argument run prints usage and exits non-zero
- missing sql-file path prints a clear file read error and exits non-zero
- valid sql-file input is split into statements and executed in order
- SELECT 결과는 표 형태로 콘솔에 출력된다
