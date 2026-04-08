#include "executor.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "schema_manager.h"
#include "storage.h"

/*
 * 공통 오류 메시지를 기록한다.
 * error_buf가 NULL이어도 안전하게 호출할 수 있도록 방어적으로 처리한다.
 */
static void set_error(char *error_buf, size_t error_buf_size, const char *message)
{
    if (error_buf != NULL && error_buf_size > 0)
    {
        snprintf(error_buf, error_buf_size, "%s", message);
    }
}

/*
 * 형식화된 오류 메시지를 남길 때 사용하는 helper다.
 * 비슷한 snprintf 코드를 반복하지 않기 위해 분리한다.
 */
static void set_errorf(char *error_buf, size_t error_buf_size, const char *format, ...)
{
    va_list args;

    if (error_buf == NULL || error_buf_size == 0 || format == NULL)
    {
        return;
    }

    va_start(args, format);
    vsnprintf(error_buf, error_buf_size, format, args);
    va_end(args);
}

/*
 * 함수 시작 시 결과 구조체를 항상 예측 가능한 상태로 초기화한다.
 * 실패 후에도 free_execution_result()를 안전하게 호출할 수 있어야 한다.
 */
static void init_execution_result(ExecutionResult *result)
{
    if (result != NULL)
    {
        memset(result, 0, sizeof(*result));
    }
}

/*
 * schema의 enum 타입을 cast_value가 받는 문자열 이름으로 바꾼다.
 * 현재 단계에서는 int / string 두 타입만 지원하므로 단순 매핑으로 충분하다.
 */
static const char *column_type_name(ColumnType type)
{
    if (type == COL_INT)
    {
        return "int";
    }

    if (type == COL_TEXT)
    {
        return "string";
    }

    return NULL;
}

/* schema 안에서 특정 컬럼 이름의 위치를 찾는다. */
static int find_schema_column_index(
    const TableSchema *schema,
    const char *column_name,
    size_t *out_index
)
{
    size_t i = 0;

    if (schema == NULL || column_name == NULL || out_index == NULL)
    {
        return -1;
    }

    for (i = 0; i < schema->column_count; i++)
    {
        if (strcmp(schema->columns[i].name, column_name) == 0)
        {
            *out_index = i;
            return 0;
        }
    }

    return -1;
}

/*
 * WHERE 비교는 schema 타입 기준으로 캐스팅한 결과끼리 비교한다.
 * 현재는 int / string 두 타입만 있으므로 타입이 같을 때 단순 비교면 충분하다.
 */
static int storage_value_equals(const StorageValue *left, const StorageValue *right)
{
    if (left == NULL || right == NULL)
    {
        return 0;
    }

    if (left->type != right->type)
    {
        return 0;
    }

    if (left->type == COL_INT)
    {
        return left->as.int_value == right->as.int_value;
    }

    return strcmp(left->as.string_value, right->as.string_value) == 0;
}

/*
 * SELECT WHERE 절이 있으면 row 목록을 필터링한다.
 * WHERE가 없으면 입력 row 목록의 소유권을 그대로 출력 구조로 넘긴다.
 */
static int apply_select_where_filter(
    const TableSchema *schema,
    const SelectCommand *command,
    StorageRowList *input_rows,
    StorageRowList *out_rows,
    char *error_buf,
    size_t error_buf_size
)
{
    size_t where_column_index = 0;
    const char *type_name = NULL;
    StorageValue where_value;
    size_t row_index = 0;

    if (schema == NULL || command == NULL || input_rows == NULL || out_rows == NULL)
    {
        set_error(error_buf, error_buf_size, "executor: WHERE 필터링 인자가 올바르지 않습니다");
        return -1;
    }

    out_rows->row_count = 0;
    out_rows->rows = NULL;

    if (!command->where.has_where)
    {
        *out_rows = *input_rows;
        input_rows->row_count = 0;
        input_rows->rows = NULL;
        return 0;
    }

    if (find_schema_column_index(schema, command->where.column, &where_column_index) != 0)
    {
        set_errorf(
            error_buf,
            error_buf_size,
            "executor: WHERE 컬럼 '%s'이(가) schema에 없습니다",
            command->where.column
        );
        return -1;
    }

    type_name = column_type_name(schema->columns[where_column_index].type);
    if (type_name == NULL)
    {
        set_errorf(
            error_buf,
            error_buf_size,
            "executor: WHERE 컬럼 '%s'의 타입을 해석할 수 없습니다",
            command->where.column
        );
        return -1;
    }

    memset(&where_value, 0, sizeof(where_value));
    if (cast_value(
            type_name,
            command->where.value.raw,
            &where_value,
            error_buf,
            error_buf_size) != 0)
    {
        return -1;
    }

    if (input_rows->row_count > 0)
    {
        out_rows->rows = (StorageRow *)calloc(input_rows->row_count, sizeof(StorageRow));
        if (out_rows->rows == NULL)
        {
            set_error(error_buf, error_buf_size, "executor: WHERE 결과 row 버퍼를 할당할 수 없습니다");
            return -1;
        }
    }

    for (row_index = 0; row_index < input_rows->row_count; row_index++)
    {
        if (storage_value_equals(
                &input_rows->rows[row_index].values[where_column_index],
                &where_value))
        {
            out_rows->rows[out_rows->row_count] = input_rows->rows[row_index];
            out_rows->row_count++;
        }
    }

    if (out_rows->row_count == 0)
    {
        free(out_rows->rows);
        out_rows->rows = NULL;
    }

    return 0;
}

/*
 * INSERT raw token 배열을 schema 순서의 StorageRow로 변환한다.
 * 이 단계에서 각 값의 실제 타입 해석이 처음 일어난다.
 */
static int build_storage_row_from_insert(
    const TableSchema *schema,
    const InsertCommand *command,
    StorageRow *out_row,
    char *error_buf,
    size_t error_buf_size
)
{
    size_t i = 0;

    if (schema == NULL || command == NULL || out_row == NULL)
    {
        set_error(error_buf, error_buf_size, "executor: INSERT row 변환 인자가 올바르지 않습니다");
        return -1;
    }

    memset(out_row, 0, sizeof(*out_row));
    out_row->value_count = schema->column_count;

    for (i = 0; i < schema->column_count; i++)
    {
        const char *type_name = column_type_name(schema->columns[i].type);

        if (type_name == NULL)
        {
            set_errorf(
                error_buf,
                error_buf_size,
                "executor: '%s' column의 타입을 해석할 수 없습니다",
                schema->columns[i].name
            );
            return -1;
        }

        if (cast_value(
                type_name,
                command->values[i].raw,
                &out_row->values[i],
                error_buf,
                error_buf_size) != 0)
        {
            return -1;
        }
    }

    return 0;
}

/*
 * INSERT 실행 흐름을 담당한다.
 * schema를 읽고, value 개수를 검증하고, 각 raw token을 typed row로 캐스팅한 뒤
 * storage에 append를 요청한다.
 */
static int execute_insert_command(
    const InsertCommand *command,
    ExecutionResult *out_result,
    char *error_buf,
    size_t error_buf_size
)
{
    StorageRow row;

    if (command == NULL || out_result == NULL)
    {
        set_error(error_buf, error_buf_size, "executor: INSERT 실행 인자가 올바르지 않습니다");
        return -1;
    }

    if (load_schema(command->table_name, &out_result->schema, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    if (validate_values(
            &out_result->schema,
            command->values,
            command->value_count,
            error_buf,
            error_buf_size) != 0)
    {
        return -1;
    }

    if (build_storage_row_from_insert(
            &out_result->schema,
            command,
            &row,
            error_buf,
            error_buf_size) != 0)
    {
        return -1;
    }

    if (append_row(command->table_name, &out_result->schema, &row, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    out_result->has_rows = false;
    return 0;
}

/*
 * SELECT 실행 흐름을 담당한다.
 * schema 전체 row를 읽은 뒤, 필요하면 WHERE 필터링과 컬럼 projection을 적용한다.
 */
static int execute_select_command(
    const SelectCommand *command,
    ExecutionResult *out_result,
    char *error_buf,
    size_t error_buf_size
)
{
    TableSchema full_schema;
    StorageRowList full_rows;
    StorageRowList filtered_rows;
    size_t i = 0;
    size_t row_index = 0;
    size_t selected_indexes[MAX_COLUMNS];

    if (command == NULL || out_result == NULL)
    {
        set_error(error_buf, error_buf_size, "executor: SELECT 실행 인자가 올바르지 않습니다");
        return -1;
    }

    memset(&full_schema, 0, sizeof(full_schema));
    full_rows.row_count = 0;
    full_rows.rows = NULL;
    filtered_rows.row_count = 0;
    filtered_rows.rows = NULL;

    if (load_schema(command->table_name, &full_schema, error_buf, error_buf_size) != 0)
    {
        return -1;
    }

    if (read_all_rows(
            command->table_name,
            &full_schema,
            &full_rows,
            error_buf,
            error_buf_size) != 0)
    {
        return -1;
    }

    if (apply_select_where_filter(
            &full_schema,
            command,
            &full_rows,
            &filtered_rows,
            error_buf,
            error_buf_size) != 0)
    {
        free_storage_row_list(&full_rows);
        return -1;
    }

    free_storage_row_list(&full_rows);

    if (command->select_all)
    {
        out_result->schema = full_schema;
        out_result->rows = filtered_rows;
        out_result->has_rows = true;
        return 0;
    }

    memset(&out_result->schema, 0, sizeof(out_result->schema));
    snprintf(out_result->schema.table_name, sizeof(out_result->schema.table_name), "%s", full_schema.table_name);
    out_result->schema.column_count = command->column_count;

    for (i = 0; i < command->column_count; i++)
    {
        if (find_schema_column_index(&full_schema, command->columns[i], &selected_indexes[i]) != 0)
        {
            set_errorf(
                error_buf,
                error_buf_size,
                "executor: 존재하지 않는 컬럼 '%s'입니다",
                command->columns[i]
            );
            free_storage_row_list(&filtered_rows);
            return -1;
        }

        out_result->schema.columns[i] = full_schema.columns[selected_indexes[i]];
    }

    out_result->rows.row_count = filtered_rows.row_count;
    out_result->rows.rows = NULL;

    if (filtered_rows.row_count > 0)
    {
        out_result->rows.rows = (StorageRow *)calloc(filtered_rows.row_count, sizeof(StorageRow));
        if (out_result->rows.rows == NULL)
        {
            free_storage_row_list(&filtered_rows);
            set_error(error_buf, error_buf_size, "executor: SELECT 결과 row 버퍼를 할당할 수 없습니다");
            return -1;
        }
    }

    for (row_index = 0; row_index < filtered_rows.row_count; row_index++)
    {
        out_result->rows.rows[row_index].value_count = command->column_count;

        for (i = 0; i < command->column_count; i++)
        {
            out_result->rows.rows[row_index].values[i] =
                filtered_rows.rows[row_index].values[selected_indexes[i]];
        }
    }

    free_storage_row_list(&filtered_rows);
    out_result->has_rows = true;
    return 0;
}

int execute_command(
    const Command *command,
    ExecutionResult *out_result,
    char *error_buf,
    size_t error_buf_size
)
{
    if (command == NULL || out_result == NULL)
    {
        set_error(error_buf, error_buf_size, "execute_command: 잘못된 인자입니다");
        return -1;
    }

    init_execution_result(out_result);

    if (command->type == CMD_INSERT)
    {
        return execute_insert_command(&command->as.insert, out_result, error_buf, error_buf_size);
    }

    if (command->type == CMD_SELECT)
    {
        return execute_select_command(&command->as.select, out_result, error_buf, error_buf_size);
    }

    set_error(error_buf, error_buf_size, "execute_command: 지원하지 않는 Command 타입입니다");
    return -1;
}

void free_execution_result(ExecutionResult *result)
{
    if (result == NULL)
    {
        return;
    }

    free_storage_row_list(&result->rows);
    memset(&result->schema, 0, sizeof(result->schema));
    result->has_rows = false;
}
