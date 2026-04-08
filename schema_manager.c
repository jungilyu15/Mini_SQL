#include "schema_manager.h"

#include <stdio.h>

int load_schema(
    const char *table_name,
    TableSchema *out_schema,
    char *error_buf,
    size_t error_buf_size
) {
    /*
     * TODO:
     * - schema/<table>.schema 파일을 연다.
     * - 각 줄을 읽어 컬럼 이름과 타입을 파싱한다.
     * - out_schema->columns와 column_count를 채운다.
     */
    (void)table_name;
    (void)out_schema;
    snprintf(error_buf, error_buf_size, "load_schema: 아직 구현되지 않았습니다");
    return -1;
}
