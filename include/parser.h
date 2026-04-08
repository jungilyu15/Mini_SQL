#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#include "models.h"

/*
 * INSERT 문 하나를 파싱해 InsertCommand를 채운다.
 * tokenizer가 만든 token 목록을 parser가 해석하는 구조를 따른다.
 *
 * 지원 예:
 *   INSERT INTO users VALUES (1, 'kim', 24);
 *
 * 정책:
 * - 키워드는 대소문자를 구분하지 않는다
 * - 공백은 적당히 유연하게 허용한다
 * - 마지막 세미콜론은 있어도 되고 없어도 된다
 * - 값은 raw SQL token 그대로 보관한다
 */
int parse_insert(
    const char *sql,
    InsertCommand *out_command,
    char *error_buf,
    size_t error_buf_size
);

/*
 * SELECT 문 하나를 파싱해 SelectCommand를 채운다.
 * tokenizer가 token화한 결과를 바탕으로 문법 구조만 해석한다.
 *
 * 지원 예:
 *   SELECT * FROM users;
 *   SELECT name, age FROM users;
 *   SELECT * FROM users WHERE id = 1;
 *   SELECT name, age FROM users WHERE name = 'kim';
 *
 * 정책:
 * - 키워드는 대소문자를 구분하지 않는다
 * - 공백은 적당히 유연하게 허용한다
 * - 마지막 세미콜론은 있어도 되고 없어도 된다
 * - 현재 단계에서는 "*" 또는 단순 컬럼 목록만 지원한다
 * - WHERE는 단일 조건 하나와 "=" 비교만 지원한다
 */
int parse_select(
    const char *sql,
    SelectCommand *out_command,
    char *error_buf,
    size_t error_buf_size
);

/*
 * 현재 단계의 parse_sql은 INSERT와 SELECT를 지원한다.
 * SELECT는 "*" 또는 명시적 컬럼 목록만 허용한다.
 * 내부적으로는 tokenizer -> parser 해석 순서를 따른다.
 */
int parse_sql(
    const char *sql,
    Command *out_command,
    char *error_buf,
    size_t error_buf_size
);

#endif
