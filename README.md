# Mini_SQL

파일 기반으로 동작하는 아주 작은 C SQL 처리기입니다.  
SQL 파일을 입력으로 받아 `INSERT`와 `SELECT`를 순차 실행하고, schema는 `schema/`, 데이터는 `data/` 아래 CSV 파일로 관리합니다.

현재 목표는 학습용 MVP입니다. 데이터베이스 서버를 띄우지 않고도 SQL 흐름을 끝까지 확인할 수 있도록 단순하고 읽기 쉬운 구조를 유지합니다.

## 프로젝트 소개

- 구현 언어: C11
- 실행 방식: `./mini_sql <sql-file>`
- 저장 방식:
  - schema: `schema/<table>.schema`
  - data: `data/<table>.csv`
- 지원 범위:
  - `INSERT INTO <table> VALUES (...);`
  - `SELECT * FROM <table>;`
  - `SELECT <column1>, <column2> FROM <table>;`

## 지원 SQL

현재 단계에서 지원하는 SQL은 아래 두 가지입니다.

### INSERT

```sql
INSERT INTO users VALUES (1, 'kim', 24);
```

정책:

- 키워드는 대소문자를 구분하지 않습니다
- 공백은 적당히 유연하게 허용합니다
- 마지막 세미콜론은 있어도 되고 없어도 됩니다
- 값은 schema 순서와 동일해야 합니다

### SELECT

```sql
SELECT * FROM users;
SELECT name, age FROM users;
```

정책:

- `*` 또는 명시적 컬럼 목록을 지원합니다
- 결과는 콘솔에 표 형태로 출력합니다
- 키워드는 대소문자를 구분하지 않습니다
- 공백은 적당히 유연하게 허용합니다

예시 출력:

```text
+-----+-------+-----+
| id  | name  | age |
+-----+-------+-----+
| 1   | Alice | 28  |
| 2   | Bob   | 31  |
+-----+-------+-----+
(2 rows)
```

## Schema 포맷

schema 파일은 `schema/<table>.schema` 경로에 두고, 한 줄에 하나의 컬럼을 `column:type` 형식으로 작성합니다.

예: `schema/users.schema`

```text
id:int
name:string
age:int
```

현재 지원 타입:

- `int`
- `string`

## 실행 방법

### 1. 빌드

```sh
cc -std=c11 -Wall -Wextra -pedantic main.c parser.c executor.c schema_manager.c storage.c -o mini_sql
```

### 2. 실행

```sh
./mini_sql sample/basic.sql
```

또는

```sh
./mini_sql sample/insert_only.sql
./mini_sql sample/select_only.sql
./mini_sql sample/select_columns.sql
```

## 예제

### 기본 예제

`sample/basic.sql`

```sql
INSERT INTO users VALUES (100, 'kim', 24);
SELECT * FROM users;
```

### INSERT만 실행하는 예제

`sample/insert_only.sql`

```sql
INSERT INTO users VALUES (200, 'park', 29);
```

### SELECT만 실행하는 예제

`sample/select_only.sql`

```sql
SELECT * FROM users;
```

### 특정 컬럼만 조회하는 예제

`sample/select_columns.sql`

```sql
SELECT name, age FROM users;
```

## 기능 테스트 시나리오

아래 시나리오를 기준으로 주요 기능을 검증합니다.

1. 인자 없이 실행하면 usage를 출력하고 종료한다.
2. 없는 SQL 파일을 넘기면 명확한 파일 읽기 오류를 출력한다.
3. SQL 파일 안의 여러 문장을 세미콜론 기준으로 순차 실행한다.
4. 빈 문장은 무시하고 나머지 SQL만 실행한다.
5. `INSERT` 실행 시 schema 로드, 값 개수 검증, 타입 캐스팅, CSV append 흐름이 연결된다.
6. `SELECT *` 또는 명시적 컬럼 SELECT 실행 시 필요한 컬럼만 표 형태로 출력한다.
7. parse 또는 execute 단계 실패 시 몇 번째 문장에서 실패했는지 함께 알려준다.

더 자세한 테스트 빌드/실행 방법은 [tests/README.md](/Users/jinhyuk/krafton/Mini_SQL/tests/README.md)에 정리되어 있습니다.

## 제한 사항

현재는 아래 기능을 지원하지 않습니다.

- REPL
- `WHERE`
- `UPDATE`
- `DELETE`
- `CREATE TABLE`
- `JOIN`
- 복잡한 SQL 일반화
- CSV quoting / escaping
- 문자열 내부 작은따옴표 escape

또한 schema와 data 디렉터리는 미리 존재해야 하며, SQL 값 순서는 schema 컬럼 순서와 같아야 합니다.
