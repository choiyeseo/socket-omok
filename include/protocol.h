// 경로: include/protocol.h

#ifndef PROTOCOL_H
#define PROTOCOL_H

#define CMD_NONE    0
#define CMD_JOIN    1
#define CMD_MOVE    2
#define CMD_EXIT    3
#define CMD_RESTART 4
#define CMD_MODE 5

int parse_command(const char* msg);

#endif

