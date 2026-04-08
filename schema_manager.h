#ifndef SCHEMA_MANAGER_H
#define SCHEMA_MANAGER_H

#include <stddef.h>

#include "models.h"

/*
 * schema/<table_name>.schema 파일을 읽어 TableSchema를 채운다.
 *
 * 지원 포맷:
 *   column_name:type_name
 *
 * 예:
 *   id:int
 *   name:string
 *
 * 성공 시 0을 반환하고 out_schema를 채운다.
 * 실패 시 -1을 반환하고 error_buf에 사람이 읽을 수 있는 오류 메시지를 기록한다.
 */
int load_schema(
    const char *table_name,
    TableSchema *out_schema,
    char *error_buf,
    size_t error_buf_size
);

/*
 * schema의 column 수와 입력 value 개수가 일치하는지 검증한다.
 *
 * 이번 단계에서는 "개수 검증"만 담당한다.
 * 각 value의 실제 타입 변환이나 전체 row 검증은 아직 여기서 하지 않는다.
 */
int validate_values(
    const TableSchema *schema,
    const SqlValue *values,
    size_t value_count,
    char *error_buf,
    size_t error_buf_size
);

/*
 * schema의 타입 이름과 SQL raw token 문자열을 받아
 * StorageValue 하나로 변환한다.
 *
 * 지원 타입:
 * - int
 * - string
 *
 * string은 raw SQL token 기준으로 작은따옴표가 포함된 형태만 허용한다.
 * 예: 'Alice'
 */
int cast_value(
    const char *type_name,
    const char *raw_value,
    StorageValue *out_value,
    char *error_buf,
    size_t error_buf_size
);

#endif
