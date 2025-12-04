// 경로: src/protocol.c
// 역할: 문자열로 들어온 명령을 보고 어떤 명령인지 판별함.

#include <string.h>
#include "protocol.h"

int parse_command(const char* msg) {
    if (strncmp(msg, "JOIN", 4) == 0) {
        return CMD_JOIN;
    }
    if (strncmp(msg, "MOVE", 4) == 0) {
        return CMD_MOVE;
    }
    if (strncmp(msg, "EXIT", 4) == 0) {
        return CMD_EXIT;
    }
    if (strncmp(msg, "MODE", 4) == 0) {
	    return CMD_MODE;
    }
    if (strncmp(msg, "RESTART", 7) == 0) {
        return CMD_RESTART;
    }
    return CMD_NONE;
}

