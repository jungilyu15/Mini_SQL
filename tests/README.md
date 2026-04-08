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

## 기능 테스트 시나리오

- 인자 없이 실행했을 때 usage 출력
- 없는 SQL 파일 경로 입력 시 명확한 오류 출력
- SQL 파일 안의 여러 문장을 세미콜론 기준으로 순차 실행
- 빈 문장 무시
- `INSERT` 문 실행 후 CSV 파일 생성 또는 append
- `SELECT *` 실행 후 표 형태 출력
- parse 실패 시 몇 번째 문장에서 실패했는지 보고
- execute 실패 시 몇 번째 문장에서 실패했는지 보고

## 엣지 케이스 테스트

- 작은따옴표 문자열 안의 세미콜론은 문장 구분자로 취급하지 않음
- 닫히지 않은 작은따옴표 문자열은 분리 단계에서 오류 처리
- 빈 SQL 문장(`;`만 있는 경우)은 무시
- schema 컬럼 수와 INSERT value 수 불일치
- int 컬럼에 숫자가 아닌 값이 들어온 경우
- CSV 필드 수 불일치
- `MAX_LINE_LENGTH`를 넘는 CSV 줄 읽기 오류
- 현재 단계에서 미지원인 `SELECT name FROM ...`, `WHERE` 절 거부

Current smoke scenarios for the app:

```sh
./mini_sql
./mini_sql missing.sql
./mini_sql sample/basic.sql
./mini_sql sample/insert_only.sql
./mini_sql sample/select_only.sql
```

Expected current behavior:

- no-argument run prints usage and exits non-zero
- missing sql-file path prints a clear file read error and exits non-zero
- valid sql-file input is split into statements and executed in order
- SELECT 결과는 표 형태로 콘솔에 출력된다
