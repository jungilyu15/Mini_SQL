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
./mini_sql sample/insert_users.sql
./mini_sql sample/select_users.sql
```

Expected pure-skeleton behavior:

- no-argument run prints usage and exits non-zero
- any sql-file argument prints a clear "not implemented" style error and exits non-zero
