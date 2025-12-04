// 경로: src/log.h
#ifndef LOG_H
#define LOG_H

// 로그 파일 열기
int log_open(const char *path);

// 로그 기록 (printf 처럼 사용)
void log_write(const char *fmt, ...);

// 로그 파일 닫기
void log_close();

#endif
