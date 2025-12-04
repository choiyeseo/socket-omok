// 경로: src/log.c
// 역할: 서버 로그 시스템 구현.
//       - 로그 파일 열기
//       - 시간 스탬프 포함 로그 기록
//       - 서버 종료 시 로그 파일 정리

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include "log.h"

// 로그 파일 포인터 (전역)
// log_open()에서 fopen으로 초기화되며,
// log_write()에서 사용됨
static FILE *log_fp = NULL;

// 로그 파일 열기
// path 경로의 파일을 append 모드로 오픈함
// 성공하면 1, 실패하면 0 반환
int log_open(const char *path) {
    log_fp = fopen(path, "a");
    if (!log_fp) return 0;
    return 1;
}

// 로그 기록 함수
// fmt 형식 문자열 + 가변 인자를 받아 시간 스탬프와 함께 기록함
// log_fp가 NULL인 경우(파일 열기 실패 시) 아무 동작 안 함
void log_write(const char *fmt, ...) {
    if (!log_fp) return;

    // 현재 시간 얻기
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    // 시간 문자열 생성 (YYYY-MM-DD HH:MM:SS)
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", t);

    // 시간 스탬프 출력
    fprintf(log_fp, "[%s] ", time_str);

    // 전달받은 가변 인자 메시지 출력
    va_list args;
    va_start(args, fmt);
    vfprintf(log_fp, fmt, args);
    va_end(args);

    // 줄바꿈 및 즉시 파일로 flush
    fprintf(log_fp, "\n");
    fflush(log_fp);
}

// 로그 파일 닫기
// 서버 종료 시 반드시 호출해야 함
void log_close() {
    if (log_fp) {
        fclose(log_fp);
        log_fp = NULL;
    }
}

