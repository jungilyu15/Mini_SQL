#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>

#include "models.h"

/*
 * caller가 넘긴 schema와 row를 검증한 뒤
 * data/<table_name>.csv 파일 끝에 한 줄을 추가한다.
 *
 * 이 함수는 schema를 로드하지 않는다.
 * storage는 schema를 "사용만" 하고, schema 로딩 책임은 다른 모듈에 둔다.
 */
int append_row(
    const char *table_name,
    const TableSchema *schema,
    const StorageRow *row,
    char *error_buf,
    size_t error_buf_size
);

/*
 * data/<table_name>.csv 전체를 읽어 StorageRowList로 복원한다.
 * CSV 파일이 없으면 성공 + 빈 결과로 처리한다.
 *
 * 함수 시작 시 out_row_list는 항상 아래 상태로 초기화된다.
 *   - row_count = 0
 *   - rows = NULL
 * 따라서 실패하더라도 caller는 free_storage_row_list()를 안전하게 호출할 수 있다.
 */
int read_all_rows(
    const char *table_name,
    const TableSchema *schema,
    StorageRowList *out_row_list,
    char *error_buf,
    size_t error_buf_size
);

/*
 * StorageRowList가 들고 있는 동적 메모리를 해제한다.
 * 해제 후에는 다시 안전한 초기 상태(row_count = 0, rows = NULL)로 되돌린다.
 */
void free_storage_row_list(StorageRowList *row_list);

#endif
