#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <stddef.h>

#include "models.h"

/*
 * executor는 parser가 만든 Command를 받아
 * schema_manager / storage를 순서대로 연결하는 조정자 역할만 맡는다.
 *
 * 출력은 가능하면 main 같은 상위 호출자에서 담당할 수 있도록
 * 실행 결과를 구조체로 돌려주는 방식을 사용한다.
 */
typedef struct {
    /*
     * SELECT 실행 결과인지 표시한다.
     * INSERT 성공 시에는 false이고, rows는 비어 있는 상태를 유지한다.
     */
    bool has_rows;

    /*
     * 실행에 사용한 schema를 함께 돌려준다.
     * 호출자는 column 이름과 타입을 이 정보로 해석할 수 있다.
     */
    TableSchema schema;

    /*
     * SELECT 결과 row 목록이다.
     * INSERT 성공 시에는 row_count = 0, rows = NULL 상태를 유지한다.
     */
    StorageRowList rows;
} ExecutionResult;

/*
 * Command 하나를 실행한다.
 *
 * - INSERT:
 *   schema 로드 -> value 개수 검증 -> 타입 캐스팅 -> StorageRow 구성 -> append_row 호출
 *
 * - SELECT:
 *   schema 로드 -> read_all_rows 호출 -> WHERE 필터링(선택) -> projection(선택) -> 결과 row 목록 반환
 *
 * 현재 단계에서는 SELECT의 단일 WHERE "=" 조건 하나만 지원한다.
 */
int execute_command(
    const Command *command,
    ExecutionResult *out_result,
    char *error_buf,
    size_t error_buf_size
);

/*
 * ExecutionResult 안의 동적 메모리를 정리하고
 * 다시 안전한 초기 상태로 되돌린다.
 */
void free_execution_result(ExecutionResult *result);

#endif
