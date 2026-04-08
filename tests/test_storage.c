#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "schema_manager.h"
#include "storage.h"

/*
 * 각 테스트 함수를 실행하고 결과를 공통 형식으로 출력한다.
 * 실패 원인은 개별 테스트에서 stderr로 추가 출력할 수 있다.
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
 * 이전 실행에서 남았을 수 있는 테스트 산출물을 지운다.
 * 파일이 원래 없으면 정상으로 본다.
 */
static int remove_if_exists(const char *path) {
    if (unlink(path) != 0 && access(path, F_OK) == 0) {
        perror("unlink");
        return -1;
    }
    return 0;
}

/*
 * storage 테스트가 생성하는 CSV 파일들을 한 번에 정리한다.
 * fixture로 커밋된 입력 파일은 건드리지 않고, 테스트 산출물만 대상으로 삼는다.
 */
static int cleanup_generated_storage_files(void) {
    if (remove_if_exists("data/append_basic.csv") != 0) {
        return -1;
    }
    if (remove_if_exists("data/append_multiple.csv") != 0) {
        return -1;
    }
    if (remove_if_exists("data/long_line.csv") != 0) {
        return -1;
    }
    return 0;
}

/*
 * 파일 내용을 통째로 읽어 문자열 버퍼로 돌려준다.
 * append 테스트에서 실제로 어떤 CSV가 써졌는지 바로 확인할 때 사용한다.
 */
static int read_file_to_buffer(const char *path, char *buffer, size_t buffer_size) {
    FILE *file = fopen(path, "r");
    size_t bytes_read = 0;

    if (file == NULL) {
        perror("fopen");
        return -1;
    }

    bytes_read = fread(buffer, 1, buffer_size - 1, file);
    if (ferror(file)) {
        fclose(file);
        return -1;
    }

    buffer[bytes_read] = '\0';
    fclose(file);
    return 0;
}

/*
 * id, name, age 형태의 공통 테스트 row를 빠르게 만드는 helper다.
 * schema 컬럼 순서에 맞춰 값을 채워 append/read 테스트에서 재사용한다.
 */
static StorageRow make_user_row(int id, const char *name, int age) {
    StorageRow row;

    memset(&row, 0, sizeof(row));
    row.value_count = 3;

    row.values[0].type = COL_INT;
    row.values[0].as.int_value = id;

    row.values[1].type = COL_TEXT;
    snprintf(row.values[1].as.string_value, sizeof(row.values[1].as.string_value), "%s", name);

    row.values[2].type = COL_INT;
    row.values[2].as.int_value = age;

    return row;
}

/* append_row가 새 CSV 파일을 만들고 첫 row를 기록하는지 확인한다. */
static int test_append_creates_file(void) {
    TableSchema schema;
    StorageRow row;
    char error_buf[MAX_ERROR_LENGTH];
    char buffer[512];

    if (remove_if_exists("data/append_basic.csv") != 0) {
        return 1;
    }

    if (load_schema("append_basic", &schema, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    row = make_user_row(1, "Alice", 28);
    if (append_row("append_basic", &schema, &row, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (read_file_to_buffer("data/append_basic.csv", buffer, sizeof(buffer)) != 0) {
        return 1;
    }

    if (strcmp(buffer, "1,Alice,28\n") != 0) {
        return 1;
    }

    if (remove_if_exists("data/append_basic.csv") != 0) {
        return 1;
    }

    return 0;
}

/* append_row를 여러 번 호출했을 때 파일 끝에 순서대로 누적되는지 확인한다. */
static int test_append_multiple_rows(void) {
    TableSchema schema;
    StorageRow row1;
    StorageRow row2;
    char error_buf[MAX_ERROR_LENGTH];
    char buffer[512];

    if (remove_if_exists("data/append_multiple.csv") != 0) {
        return 1;
    }

    if (load_schema("append_multiple", &schema, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    row1 = make_user_row(1, "Alice", 28);
    row2 = make_user_row(2, "Bob", 31);

    if (append_row("append_multiple", &schema, &row1, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (append_row("append_multiple", &schema, &row2, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (read_file_to_buffer("data/append_multiple.csv", buffer, sizeof(buffer)) != 0) {
        return 1;
    }

    if (strcmp(buffer, "1,Alice,28\n2,Bob,31\n") != 0) {
        return 1;
    }

    if (remove_if_exists("data/append_multiple.csv") != 0) {
        return 1;
    }

    return 0;
}

/* 기존 CSV fixture를 읽어 typed StorageRow 배열로 복원하는지 확인한다. */
static int test_read_all_rows_valid_csv(void) {
    TableSchema schema;
    StorageRowList row_list;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("read_valid", &schema, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (read_all_rows("read_valid", &schema, &row_list, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (row_list.row_count != 2) {
        free_storage_row_list(&row_list);
        return 1;
    }

    if (row_list.rows[0].values[0].as.int_value != 1) {
        free_storage_row_list(&row_list);
        return 1;
    }
    if (strcmp(row_list.rows[0].values[1].as.string_value, "Alice") != 0) {
        free_storage_row_list(&row_list);
        return 1;
    }
    if (row_list.rows[1].values[2].as.int_value != 31) {
        free_storage_row_list(&row_list);
        return 1;
    }

    free_storage_row_list(&row_list);
    if (row_list.row_count != 0 || row_list.rows != NULL) {
        return 1;
    }

    return 0;
}

/* CSV 파일이 없으면 오류가 아니라 빈 결과로 처리하는 정책을 검증한다. */
static int test_missing_csv_returns_empty(void) {
    TableSchema schema;
    StorageRowList row_list;
    char error_buf[MAX_ERROR_LENGTH];

    if (remove_if_exists("data/missing_csv.csv") != 0) {
        return 1;
    }

    if (load_schema("missing_csv", &schema, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (read_all_rows("missing_csv", &schema, &row_list, error_buf, sizeof(error_buf)) != 0) {
        fprintf(stderr, "%s\n", error_buf);
        return 1;
    }

    if (row_list.row_count != 0 || row_list.rows != NULL) {
        return 1;
    }

    free_storage_row_list(&row_list);
    return 0;
}

/* row 값 개수가 schema column 수와 다르면 append가 실패해야 한다. */
static int test_append_row_count_mismatch(void) {
    TableSchema schema;
    StorageRow row;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("append_basic", &schema, error_buf, sizeof(error_buf)) != 0) {
        return 1;
    }

    memset(&row, 0, sizeof(row));
    row.value_count = 2;
    row.values[0].type = COL_INT;
    row.values[0].as.int_value = 1;
    row.values[1].type = COL_TEXT;
    snprintf(row.values[1].as.string_value, sizeof(row.values[1].as.string_value), "%s", "Alice");

    if (append_row("append_basic", &schema, &row, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    return strstr(error_buf, "개수") == NULL;
}

/* StorageValue.type이 schema 타입과 다를 때 append가 실패해야 한다. */
static int test_append_type_mismatch(void) {
    TableSchema schema;
    StorageRow row;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("append_basic", &schema, error_buf, sizeof(error_buf)) != 0) {
        return 1;
    }

    row = make_user_row(1, "Alice", 28);
    row.values[0].type = COL_TEXT;
    snprintf(row.values[0].as.string_value, sizeof(row.values[0].as.string_value), "%s", "wrong");

    if (append_row("append_basic", &schema, &row, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    return strstr(error_buf, "타입") == NULL;
}

/* 단순 CSV 범위를 벗어나는 TEXT 값은 append 단계에서 거부한다. */
static int test_append_invalid_text_chars(void) {
    TableSchema schema;
    StorageRow row;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("append_basic", &schema, error_buf, sizeof(error_buf)) != 0) {
        return 1;
    }

    row = make_user_row(1, "Alice,Bob", 28);
    if (append_row("append_basic", &schema, &row, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    return strstr(error_buf, "쉼표") == NULL;
}

/* CSV 필드 수가 schema와 맞지 않으면 read가 실패해야 한다. */
static int test_read_bad_field_count(void) {
    TableSchema schema;
    StorageRowList row_list;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("bad_field_count", &schema, error_buf, sizeof(error_buf)) != 0) {
        return 1;
    }

    row_list.row_count = 123;
    row_list.rows = (StorageRow *)0x1;

    if (read_all_rows("bad_field_count", &schema, &row_list, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    if (row_list.row_count != 0 || row_list.rows != NULL) {
        return 1;
    }

    return strstr(error_buf, "필드 개수") == NULL;
}

/* int 컬럼에 숫자가 아닌 값이 들어 있으면 read가 실패해야 한다. */
static int test_read_invalid_int(void) {
    TableSchema schema;
    StorageRowList row_list;
    char error_buf[MAX_ERROR_LENGTH];

    if (load_schema("invalid_int", &schema, error_buf, sizeof(error_buf)) != 0) {
        return 1;
    }

    if (read_all_rows("invalid_int", &schema, &row_list, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    if (row_list.row_count != 0 || row_list.rows != NULL) {
        return 1;
    }

    return strstr(error_buf, "유효한 int") == NULL;
}

/* MAX_LINE_LENGTH를 넘는 줄을 만나면 즉시 오류로 처리해야 한다. */
static int test_read_long_line_error(void) {
    TableSchema schema;
    StorageRowList row_list;
    char error_buf[MAX_ERROR_LENGTH];
    char long_line[MAX_LINE_LENGTH + 20];
    FILE *file = NULL;
    size_t i = 0;

    if (load_schema("long_line", &schema, error_buf, sizeof(error_buf)) != 0) {
        return 1;
    }

    for (i = 0; i < sizeof(long_line) - 1; i++) {
        long_line[i] = 'a';
    }
    long_line[sizeof(long_line) - 1] = '\0';

    file = fopen("data/long_line.csv", "w");
    if (file == NULL) {
        return 1;
    }
    fputs(long_line, file);
    fclose(file);

    if (read_all_rows("long_line", &schema, &row_list, error_buf, sizeof(error_buf)) == 0) {
        return 1;
    }

    if (row_list.row_count != 0 || row_list.rows != NULL) {
        return 1;
    }

    if (remove_if_exists("data/long_line.csv") != 0) {
        return 1;
    }

    return strstr(error_buf, "MAX_LINE_LENGTH") == NULL;
}

/* free_storage_row_list가 언제 호출돼도 안전한 초기 상태로 되돌리는지 확인한다. */
static int test_free_storage_row_list_resets_state(void) {
    StorageRowList row_list;

    row_list.row_count = 1;
    row_list.rows = malloc(sizeof(StorageRow));
    if (row_list.rows == NULL) {
        return 1;
    }

    memset(row_list.rows, 0, sizeof(StorageRow));
    free_storage_row_list(&row_list);

    if (row_list.row_count != 0 || row_list.rows != NULL) {
        return 1;
    }

    return 0;
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

    /*
     * 이전 테스트 실행에서 남았을 수 있는 산출물을 먼저 지운다.
     * 이렇게 해 두면 테스트가 실행 순서와 무관하게 항상 같은 상태에서 시작한다.
     */
    if (cleanup_generated_storage_files() != 0) {
        return 1;
    }

    failures += run_test("append creates file", test_append_creates_file);
    failures += run_test("append multiple rows", test_append_multiple_rows);
    failures += run_test("read valid csv", test_read_all_rows_valid_csv);
    failures += run_test("missing csv returns empty", test_missing_csv_returns_empty);
    failures += run_test("append row count mismatch", test_append_row_count_mismatch);
    failures += run_test("append type mismatch", test_append_type_mismatch);
    failures += run_test("append invalid text chars", test_append_invalid_text_chars);
    failures += run_test("read bad field count", test_read_bad_field_count);
    failures += run_test("read invalid int", test_read_invalid_int);
    failures += run_test("read long line", test_read_long_line_error);
    failures += run_test("free row list resets state", test_free_storage_row_list_resets_state);

    if (cleanup_generated_storage_files() != 0) {
        return 1;
    }

    if (chdir(original_cwd) != 0) {
        perror("chdir");
        return 1;
    }

    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }

    printf("all storage tests passed\n");
    return 0;
}
