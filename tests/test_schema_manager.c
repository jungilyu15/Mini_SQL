#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "schema_manager.h"

/*
 * 개별 테스트 함수를 공통 형식으로 실행한다.
 * 실패한 테스트는 각 함수 내부에서 필요한 추가 정보를 stderr로 남길 수 있다.
 */
static int run_test(const char *name, int (*test_fn)(void)) {
    int result = test_fn();

    if (result == 0) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s\n", name);
    }

    return result;
}

/*
 * 정상 users schema를 로드한 뒤 기본 column 구성이 기대와 같은지 검증한다.
 * 이후 다른 테스트에서 재사용하는 기준 schema 역할도 한다.
 */
static int expect_successful_users_schema(void) {
    TableSchema schema;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("users", &schema, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (strcmp(schema.table_name, "users") != 0) {
        return 1;
    }
    if (schema.column_count != 3) {
        return 1;
    }
    if (strcmp(schema.columns[0].name, "id") != 0 || schema.columns[0].type != COL_INT) {
        return 1;
    }
    if (strcmp(schema.columns[1].name, "name") != 0 || schema.columns[1].type != COL_TEXT) {
        return 1;
    }
    if (strcmp(schema.columns[2].name, "age") != 0 || schema.columns[2].type != COL_INT) {
        return 1;
    }

    return 0;
}

/* column:type 줄 사이 공백을 허용하는 정책이 유지되는지 확인한다. */
static int expect_successful_schema_with_spaces(void) {
    TableSchema schema;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("with_spaces", &schema, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (schema.column_count != 3) {
        return 1;
    }
    if (strcmp(schema.columns[0].name, "id") != 0 || schema.columns[0].type != COL_INT) {
        return 1;
    }
    if (strcmp(schema.columns[1].name, "name") != 0 || schema.columns[1].type != COL_TEXT) {
        return 1;
    }

    return 0;
}

/* 존재하지 않는 schema 파일이면 명확한 오류 메시지가 나와야 한다. */
static int expect_missing_file_error(void) {
    TableSchema schema;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("does_not_exist", &schema, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    if (strstr(error_buf, "does_not_exist") == NULL) {
        return 1;
    }

    return 0;
}

/*
 * 잘못된 schema fixture에 대해 공통적으로 사용하는 검증 helper다.
 * 기대한 오류 메시지 일부가 실제 메시지에 포함되는지만 확인한다.
 */
static int expect_invalid_format_error(const char *table_name, const char *expected_text) {
    TableSchema schema;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema(table_name, &schema, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    if (strstr(error_buf, expected_text) == NULL) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    return 0;
}

/*
 * InsertCommand는 table_name + raw SqlValue 배열을 가지는 구조여야 한다.
 * Python의 dataclass 개념을 C에서는 고정 배열과 count 필드로 번역했는지 확인한다.
 */
static int test_insert_command_shape(void) {
    InsertCommand command;

    memset(&command, 0, sizeof(command));
    snprintf(command.table_name, sizeof(command.table_name), "%s", "users");
    command.value_count = 2;
    snprintf(command.values[0].raw, sizeof(command.values[0].raw), "%s", "1");
    snprintf(command.values[1].raw, sizeof(command.values[1].raw), "%s", "'Alice'");

    if (strcmp(command.table_name, "users") != 0) {
        return 1;
    }
    if (command.value_count != 2) {
        return 1;
    }
    if (strcmp(command.values[0].raw, "1") != 0) {
        return 1;
    }
    if (strcmp(command.values[1].raw, "'Alice'") != 0) {
        return 1;
    }

    return 0;
}

/*
 * SELECT *는 select_all = true, column_count = 1, columns[0] = "*"로 표현한다.
 * 실행 로직과 parser가 같은 표현을 공유할 수 있도록 구조를 맞춘다.
 */
static int test_select_command_select_all_shape(void) {
    SelectCommand command;

    memset(&command, 0, sizeof(command));
    snprintf(command.table_name, sizeof(command.table_name), "%s", "users");
    command.select_all = true;
    command.column_count = 1;
    snprintf(command.columns[0], sizeof(command.columns[0]), "%s", "*");

    if (strcmp(command.table_name, "users") != 0) {
        return 1;
    }
    if (!command.select_all) {
        return 1;
    }
    if (command.column_count != 1) {
        return 1;
    }
    if (strcmp(command.columns[0], "*") != 0) {
        return 1;
    }

    return 0;
}

/* 특정 컬럼 SELECT를 위해 columns[]와 column_count를 담을 수 있어야 한다. */
static int test_select_command_columns_shape(void) {
    SelectCommand command;

    memset(&command, 0, sizeof(command));
    snprintf(command.table_name, sizeof(command.table_name), "%s", "users");
    command.select_all = false;
    command.column_count = 2;
    snprintf(command.columns[0], sizeof(command.columns[0]), "%s", "id");
    snprintf(command.columns[1], sizeof(command.columns[1]), "%s", "name");

    if (command.select_all) {
        return 1;
    }
    if (command.column_count != 2) {
        return 1;
    }
    if (strcmp(command.columns[0], "id") != 0) {
        return 1;
    }
    if (strcmp(command.columns[1], "name") != 0) {
        return 1;
    }

    return 0;
}

/* SELECT WHERE 최소 표현이 column + raw value token 형태로 담기는지 확인한다. */
static int test_select_command_where_shape(void) {
    SelectCommand command;

    memset(&command, 0, sizeof(command));
    snprintf(command.table_name, sizeof(command.table_name), "%s", "users");
    command.select_all = false;
    command.column_count = 2;
    snprintf(command.columns[0], sizeof(command.columns[0]), "%s", "name");
    snprintf(command.columns[1], sizeof(command.columns[1]), "%s", "age");
    command.where.has_where = true;
    snprintf(command.where.column, sizeof(command.where.column), "%s", "name");
    snprintf(command.where.value.raw, sizeof(command.where.value.raw), "%s", "'kim'");

    if (!command.where.has_where) {
        return 1;
    }
    if (strcmp(command.where.column, "name") != 0) {
        return 1;
    }
    if (strcmp(command.where.value.raw, "'kim'") != 0) {
        return 1;
    }

    return 0;
}

/* schema column 수와 입력 value 수가 정확히 같으면 성공해야 한다. */
static int test_validate_values_success(void) {
    TableSchema schema;
    SqlValue values[3];
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("users", &schema, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    memset(values, 0, sizeof(values));
    snprintf(values[0].raw, sizeof(values[0].raw), "%s", "1");
    snprintf(values[1].raw, sizeof(values[1].raw), "%s", "'Alice'");
    snprintf(values[2].raw, sizeof(values[2].raw), "%s", "28");

    if (validate_values(&schema, values, 3, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    return 0;
}

/* 값이 부족하면 validate_values가 명확히 실패해야 한다. */
static int test_validate_values_too_few(void) {
    TableSchema schema;
    SqlValue values[2];
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("users", &schema, error_buf, sizeof(error_buf)) != 0) {
        return 1;
    }

    memset(values, 0, sizeof(values));
    if (validate_values(&schema, values, 2, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    return strstr(error_buf, "입력 value 수") == NULL;
}

/* 값이 많아도 validate_values가 실패해야 한다. */
static int test_validate_values_too_many(void) {
    TableSchema schema;
    SqlValue values[4];
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("users", &schema, error_buf, sizeof(error_buf)) != 0) {
        return 1;
    }

    memset(values, 0, sizeof(values));
    if (validate_values(&schema, values, 4, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    return strstr(error_buf, "입력 value 수") == NULL;
}

/* int raw token은 StorageValue의 int 필드로 변환되어야 한다. */
static int test_cast_value_int_success(void) {
    StorageValue value;
    char error_buf[MAX_ERROR_LENGTH];

    if (cast_value("int", "123", &value, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (value.type != COL_INT) {
        return 1;
    }
    if (value.as.int_value != 123) {
        return 1;
    }

    return 0;
}

/* 숫자가 아닌 int raw token은 실패해야 한다. */
static int test_cast_value_int_invalid(void) {
    StorageValue value;
    char error_buf[MAX_ERROR_LENGTH];

    if (cast_value("int", "abc", &value, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    return strstr(error_buf, "유효한 int") == NULL;
}

/* 작은따옴표로 감싼 string raw token은 따옴표를 제거해 저장해야 한다. */
static int test_cast_value_string_success(void) {
    StorageValue value;
    char error_buf[MAX_ERROR_LENGTH];

    if (cast_value("string", "'Alice'", &value, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "unexpected error: %s\n", error_buf);
        return 1;
    }

    if (value.type != COL_TEXT) {
        return 1;
    }
    if (strcmp(value.as.string_value, "Alice") != 0) {
        return 1;
    }

    return 0;
}

/* string raw token은 작은따옴표가 필수다. */
static int test_cast_value_string_requires_quotes(void) {
    StorageValue value;
    char error_buf[MAX_ERROR_LENGTH];

    if (cast_value("string", "Alice", &value, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    return strstr(error_buf, "작은따옴표") == NULL;
}

/* 지원하지 않는 타입 이름은 실패해야 한다. */
static int test_cast_value_unknown_type(void) {
    StorageValue value;
    char error_buf[MAX_ERROR_LENGTH];

    if (cast_value("unknown", "123", &value, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    return strstr(error_buf, "지원하지 않는 타입") == NULL;
}

static int test_missing_colon(void) {
    return expect_invalid_format_error("missing_colon", "형식이 잘못되었습니다");
}

static int test_missing_name(void) {
    return expect_invalid_format_error("missing_name", "빈 column/type");
}

static int test_missing_type(void) {
    return expect_invalid_format_error("missing_type", "빈 column/type");
}

static int test_unsupported_type(void) {
    return expect_invalid_format_error("unsupported_type", "지원하지 않는 타입");
}

static int test_empty_schema(void) {
    return expect_invalid_format_error("empty", "유효한 column이 없습니다");
}

static int test_too_many_columns(void) {
    return expect_invalid_format_error("too_many_columns", "최대치");
}

int main(void) {
    char original_cwd[1024];
    int failures = 0;

    if (getcwd(original_cwd, sizeof(original_cwd)) == NULL) {
        perror("getcwd");
        return 1;
    }

    if (chdir("tests/fixtures") != 0) {
        perror("chdir");
        return 1;
    }

    failures += run_test("insert command shape", test_insert_command_shape);
    failures += run_test("select command select_all shape", test_select_command_select_all_shape);
    failures += run_test("select command columns shape", test_select_command_columns_shape);
    failures += run_test("select command where shape", test_select_command_where_shape);
    failures += run_test("valid schema", expect_successful_users_schema);
    failures += run_test("schema with spaces", expect_successful_schema_with_spaces);
    failures += run_test("validate values success", test_validate_values_success);
    failures += run_test("validate values too few", test_validate_values_too_few);
    failures += run_test("validate values too many", test_validate_values_too_many);
    failures += run_test("cast int success", test_cast_value_int_success);
    failures += run_test("cast int invalid", test_cast_value_int_invalid);
    failures += run_test("cast string success", test_cast_value_string_success);
    failures += run_test("cast string requires quotes", test_cast_value_string_requires_quotes);
    failures += run_test("cast unknown type", test_cast_value_unknown_type);
    failures += run_test("missing file", expect_missing_file_error);
    failures += run_test("missing colon", test_missing_colon);
    failures += run_test("missing name", test_missing_name);
    failures += run_test("missing type", test_missing_type);
    failures += run_test("unsupported type", test_unsupported_type);
    failures += run_test("empty schema", test_empty_schema);
    failures += run_test("too many columns", test_too_many_columns);

    if (chdir(original_cwd) != 0) {
        perror("chdir");
        return 1;
    }

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    printf("all schema_manager tests passed\n");
    return 0;
}
