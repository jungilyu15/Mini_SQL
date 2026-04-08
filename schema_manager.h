#ifndef SCHEMA_MANAGER_H
#define SCHEMA_MANAGER_H

#include <stddef.h>

#include "models.h"

/* schema_manager 모듈의 뼈대 함수 */
int load_schema(
    const char *table_name,
    TableSchema *out_schema,
    char *error_buf,
    size_t error_buf_size
);

#endif
