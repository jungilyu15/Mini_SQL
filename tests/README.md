# Stage 0 Smoke Tests

Run the build:

```sh
cc -std=c11 -Wall -Wextra -pedantic main.c parser.c executor.c schema_manager.c storage.c -o mini_sql
```

Smoke scenarios:

```sh
./mini_sql
./mini_sql missing.sql
./mini_sql sample/insert_users.sql
./mini_sql sample/select_users.sql
```

Expected pure-skeleton behavior:

- no-argument run prints usage and exits non-zero
- any sql-file argument prints a clear "not implemented" style error and exits non-zero
