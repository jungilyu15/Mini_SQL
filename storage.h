#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include <stdio.h>

#include "models.h"

/* CSV append용 storage 뼈대 함수 */
int append_csv_row(
    const char *data_dir,
    const TableSchema *schema,
    const InsertCommand *command,
    char *error_buf,
    size_t error_buf_size
);

/* CSV read용 storage 뼈대 함수 */
int stream_csv_rows(
    const char *data_dir,
    const TableSchema *schema,
    FILE *out_stream,
    char *error_buf,
    size_t error_buf_size
);

#endif
