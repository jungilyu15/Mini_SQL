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

#endif
