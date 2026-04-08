#include "storage.h"

#include <stdio.h>

int append_csv_row(
    const char *data_dir,
    const TableSchema *schema,
    const InsertCommand *command,
    char *error_buf,
    size_t error_buf_size
) {
    (void)data_dir;
    (void)schema;
    (void)command;

    /*
     * TODO:
     * - data/<table>.csv 파일을 연다.
     * - INSERT 값을 CSV 한 줄로 직렬화한다.
     * - 파일 끝에 append한다.
     */
    snprintf(error_buf, error_buf_size, "append_csv_row: 아직 구현되지 않았습니다");
    return -1;
}

int stream_csv_rows(
    const char *data_dir,
    const TableSchema *schema,
    FILE *out_stream,
    char *error_buf,
    size_t error_buf_size
) {
    (void)data_dir;
    (void)schema;
    (void)out_stream;

    /*
     * TODO:
     * - data/<table>.csv 파일을 연다.
     * - 각 row를 읽어 out_stream으로 전달한다.
     */
    snprintf(error_buf, error_buf_size, "stream_csv_rows: 아직 구현되지 않았습니다");
    return -1;
}
