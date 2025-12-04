// 경로: src/server.c
// 역할: 오목 게임 서버 프로그램.
//       - 유닉스 도메인 소켓(/tmp/omok.sock)을 사용하여 데몬(daemon) 형태로 동작
//       - 최대 2개의 클라이언트를 관리하며, 모드(PVP / PVAI)에 따라 게임을 진행
//       - 보드 상태 관리(board.c), 프로토콜 파싱(protocol.c), 로그 기록(log.c)과 연동
//       - 사람 vs 사람(PVP), 사람 vs AI(PVAI) 모드 지원

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include "board.h"
#include "protocol.h"
#include "log.h" // 로그 헤더 추가

#define SOCK_PATH "/tmp/omok.sock"  // 서버가 사용하는 유닉스 도메인 소켓 경로
#define PID_FILE  "/tmp/omok.pid"   // 데몬 PID를 기록하는 파일 경로
#define LOG_FILE  "omok.log"        // 로그를 기록할 파일 이름
#define MAX_CLIENTS 2               // 동시 접속 가능한 최대 클라이언트 수 (P1, P2)
#define MODE_NONE 0                 // 아직 모드가 선택되지 않은 상태
#define MODE_PVP 1                  // 사람 vs 사람 모드
#define MODE_PVAI 2                 // 사람 vs AI 모드

int server_fd = -1;
int running = 1;        // 서버 메인 루프 실행 플래그 (시그널에 의해 0으로 변경됨)
int game_mode=MODE_NONE;
int rand_initialized = 0;

// board.c 내부의 보드 상태를 참조하기 위한 함수
// 0: 빈칸, 1: 사람(P1), 2: AI(P2)
extern int get_stone(int x, int y);

// 보드에서 (x,y)가 유효한 좌표인지 검사하는 함수
static int in_range(int x, int y) {
    return (x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE);
}

// 한 방향(dx, dy)으로 같은 색(player) 돌이 몇 개 연속되는지 세는 함수
// 시작점은 (x,y)의 바로 다음 칸부터 검사
static int count_dir(int x, int y, int dx, int dy, int player) {
    int cnt = 0;
    int nx = x + dx;
    int ny = y + dy;

    while (in_range(nx, ny) && get_stone(nx, ny) == player) {
        cnt++;
        nx += dx;
        ny += dy;
    }
    return cnt;
}

// (x,y)에 player가 둔다고 가정했을 때, 해당 위치를 기준으로 만들 수 있는
// 최대 연속 길이(가로, 세로, 두 대각선 중 최대)를 계산
static int longest_line_if(int x, int y, int player) {
    // (x,y)는 현재 빈칸이라고 가정하고, 여기에 player의 돌이 새로 놓인다고 생각
    static const int dirs[4][2] = {
        {1, 0},  // 가로
        {0, 1},  // 세로
        {1, 1},  // 대각 ↘
        {1, -1}  // 대각 ↗
    };

    int best = 0;
    for (int k = 0; k < 4; k++) {
        int dx = dirs[k][0];
        int dy = dirs[k][1];

        int len = 1; // (x,y)에 새로 놓는 돌 1개 포함
        len += count_dir(x, y, dx,  dy, player);
        len += count_dir(x, y, -dx, -dy, player);

        if (len > best) best = len;
    }
    return best;
}

// (x,y)에 player가 두면 5목(이상)이 되는지 여부를 판단
static int is_five_if(int x, int y, int player) {
    return (longest_line_if(x, y, player) >= 5);
}

// (hx, hy) : 사람이 방금 둔 좌표 (CMD_MOVE 처리 시 전달되는 x,y)
// (x,y)에 AI가 둔다고 가정했을 때 해당 칸의 점수를 평가하는 함수
static int evaluate_cell(int x, int y, int hx, int hy) {
    // 이미 돌이 있으면 아주 낮은 점수 부여 (실질적으로 선택 불가)
    if (!in_range(x, y) || get_stone(x, y) != 0) {
        return -1000000000;
    }

    int score = 0;
    int ai    = 2; // AI는 Player 2
    int human = 1; // 사람은 Player 1

    // 1. AI가 두면 바로 이기는 자리 (5목 완성)
    if (is_five_if(x, y, ai)) {
        score += 1000000;
    }

    // 2. 사람이 두면 이기는 자리(= 즉시 막아야 하는 자리)
    if (is_five_if(x, y, human)) {
        score += 900000;
    }

    // 3. 양쪽의 최대 연속 길이에 따른 가중치 부여
    int myLen  = longest_line_if(x, y, ai);
    int oppLen = longest_line_if(x, y, human);

    if (myLen == 4)      score += 50000; // AI가 4목을 만들 수 있는 자리
    else if (myLen == 3) score += 10000; // 3목 자리

    if (oppLen == 4)      score += 40000; // 사람의 4목을 막는 자리
    else if (oppLen == 3) score +=  8000; // 사람의 3목 견제

    // 4. 중앙 선호 (중앙에서 멀어질수록 감점)
    int center = (BOARD_SIZE - 1) / 2;
    int dx = x - center;
    int dy = y - center;
    int dist2 = dx*dx + dy*dy;
    score -= dist2; // 거리 제곱만큼 점수 감소

    // 5. 사람이 마지막으로 둔 수(hx, hy)에 가까울수록 약간 선호
    if (hx >= 0 && hy >= 0) {
        int pdx = x - hx;
        int pdy = y - hy;
        int pd2 = pdx*pdx + pdy*pdy;
        score -= pd2; // 사람 돌 근처에서 싸우는 전략 선호
    }

    return score;
}

// 사람이 방금 둔 좌표 (hx, hy)를 참고해서
// AI가 둘 최적의 좌표를 (out_x, out_y)에 설정
static void choose_ai_move(int hx, int hy, int *out_x, int *out_y) {
    int bestScore = -1000000000;
    int bestX = 0, bestY = 0;

    // 전체 보드를 순회하면서 빈칸에 대해 evaluate_cell로 점수 평가
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            if (get_stone(x, y) != 0) continue; // 이미 돌이 있는 칸은 스킵

            int s = evaluate_cell(x, y, hx, hy);
            if (s > bestScore) {
                bestScore = s;
                bestX = x;
                bestY = y;
            }
        }
    }

    *out_x = bestX;
    *out_y = bestY;
}

// 시그널 핸들러
// SIGTERM, SIGINT 수신 시 running 플래그를 0으로 변경하여 메인 루프 종료 유도
void handle_signal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        log_write("Signal %d received. Stopping server...", sig);
        running = 0; // 루프 종료 유도
    }
}

// 클라이언트에게 모드 선택 요청을 보내는 함수
// 실제 텍스트 안내는 client.c에서 출력
void send_mode_select_message(int fd) {
    const char *msg =
        "MODE_SELECT\n";  // 한 줄로만 보내고, 실제 질문은 클라이언트에서 출력
    write(fd, msg, strlen(msg));
}

// 데몬화 함수
// - 부모 프로세스를 종료하고 백그라운드에서 동작
// - 세션 분리, 파일 디스크립터 정리, PID 파일 생성 등 수행
void daemonize() {
    pid_t pid;

    // 1. 1차 fork
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); // 부모 종료

    // 2. 새 세션 생성 (터미널로부터 분리)
    if (setsid() < 0) exit(EXIT_FAILURE);

    // 3. 일부 시그널 무시 설정
    signal(SIGCHLD, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    // 4. 2차 fork (세션 리더가 되지 않도록 재차 포크)
    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    // 5. 파일 모드 마스크 해제
    umask(0);

    // 6. 작업 디렉토리 변경 (필요 시 루트로 변경 가능)
    // chdir("/"); // 로그 파일을 위해 현재 디렉토리 유지 혹은 절대 경로 사용 필요

    // 7. 표준 입출력/에러를 /dev/null로 리다이렉션
    int fd = open("/dev/null", O_RDWR);
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);

    // 8. PID 파일 생성 (현재 데몬의 PID 기록)
    FILE *fp = fopen(PID_FILE, "w");
    if (fp) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
    }
}

// 현재 접속 중인 모든 클라이언트에게 동일한 메시지를 방송(broadcast)
void broadcast(int *client_fds, const char *msg) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] != -1) {
            write(client_fds[i], msg, strlen(msg));
        }
    }
}

int main() {
    // 1. 데몬화 실행
    daemonize();

    // ★ rand 초기화 (필요 시 AI에 난수 요소를 넣기 위해 사용 가능)
    if (!rand_initialized) {
        srand((unsigned int)time(NULL));
        rand_initialized = 1;
    }

    // 2. 로그 시스템 시작
    if (!log_open(LOG_FILE)) {
        exit(EXIT_FAILURE); // 로그 파일을 열지 못하면 서버 종료
    }
    log_write("Server Daemon Started. PID: %d", getpid());

    // 3. 종료 관련 시그널 등록 (SIGTERM, SIGINT)
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    int client_fd[MAX_CLIENTS] = { -1, -1 }; // 클라이언트 소켓 FD
    int joined[MAX_CLIENTS]    = { 0, 0 };   // 각 클라이언트의 JOIN 여부
    int current_turn = 1;                    // 현재 턴인 플레이어 (1 또는 2)
    int game_over = 0;                       // 게임 종료 여부 플래그
    struct sockaddr_un addr;

    // 서버용 유닉스 도메인 소켓 생성
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        log_write("Socket creation failed");
        return 1;
    }

    // 기존에 남아 있을 수 있는 소켓 파일 제거
    unlink(SOCK_PATH);

    // 소켓 주소 구조체 초기화 및 경로 설정
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_PATH);

    // 소켓 bind
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        log_write("Bind failed");
        close(server_fd);
        return 1;
    }

    // 클라이언트 접속 대기 (listen)
    if (listen(server_fd, 5) == -1) {
        log_write("Listen failed");
        close(server_fd);
        return 1;
    }

    log_write("Server listening on %s", SOCK_PATH);

    init_board();          // 게임 보드 초기화
    int player_count = 0;  // 현재 접속 중인 클라이언트 수

    // 메인 루프 (running 플래그로 제어)
    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);

        // 서버 소켓을 감시 집합에 추가
        FD_SET(server_fd, &readfds);
        int maxfd = server_fd;

        // 각 클라이언트 소켓도 감시 집합에 추가
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fd[i] != -1) {
                FD_SET(client_fd[i], &readfds);
                if (client_fd[i] > maxfd) maxfd = client_fd[i];
            }
        }

        // 타임아웃 설정 (1초마다 깨어나 시그널 처리 여부 확인)
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(maxfd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0 && running) {
            // select가 시그널 등으로 인터럽트된 경우를 제외한 에러 처리
            log_write("Select error");
            continue;
        }

        if (activity == 0) continue; // 타임아웃 시 다시 루프

        // 새 클라이언트 접속 처리
        if (FD_ISSET(server_fd, &readfds)) {
            int new_fd = accept(server_fd, NULL, NULL);
            if (new_fd != -1) {
                if (player_count < MAX_CLIENTS) {
                    int slot = (client_fd[0] == -1) ? 0 : 1;
                    client_fd[slot] = new_fd;
                    log_write("Client connected: FD=%d (Slot %d)", new_fd, slot);
                    player_count++;
                } else {
                    // 인원이 꽉 찬 경우 새 연결은 바로 종료
                    close(new_fd);
                }
            }
        }

        // 각 클라이언트로부터 온 메시지 처리
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_fd[i] == -1) continue;
            if (!FD_ISSET(client_fd[i], &readfds)) continue;

            char buf[256] = {0};
            int n = read(client_fd[i], buf, sizeof(buf) - 1);
            
            if (n <= 0) {
                // 클라이언트 연결 종료 처리
                log_write("Client disconnected: FD=%d", client_fd[i]);
                close(client_fd[i]);
                client_fd[i] = -1;
                joined[i] = 0;
                player_count--;
                continue;
            }

            buf[n] = '\0';
            char *newline = strchr(buf, '\n');
            if (newline) *newline = '\0';
            
            log_write("Client[%d]: %s", i, buf);

            int cmd = parse_command(buf); // protocol.c에서 명령어 파싱
            int player_id = i + 1;        // 클라이언트 인덱스를 기반으로 1 또는 2로 매핑

            // CMD_JOIN 처리: 클라이언트가 게임에 참가 요청
            if (cmd == CMD_JOIN) {
                joined[i] = 1;
                int fd_i = client_fd[i];

                if (i == 0) {
                    // 첫 번째 플레이어
                    write(fd_i, "OK PLAYER1\n", 11);

                    // ★ 모드 선택 요청 보내기 (P1만 선택)
                    send_mode_select_message(fd_i);

                    log_write("Player 1 joined. Waiting for mode selection.");
                } else {
                    // 두 번째 플레이어
                    write(fd_i, "OK PLAYER2\n", 11);
                    log_write("Player 2 joined.");

                    // ★ PVP 모드에서만 두 번째가 들어왔을 때 바로 시작
                    if (game_mode == MODE_PVP && joined[0] && joined[1]) {
                        log_write("All players joined in PVP mode. Starting game.");
                        broadcast(client_fd, "START\n");
                        broadcast(client_fd, "TURN 1\n");
                    }
                }
                continue;
            }

	    // ★ CMD_MODE: 플레이어 1이 모드 선택 (1: PVAI, 2: PVP)
            if (cmd == CMD_MODE) {
                int mode_num = 0;
                if (sscanf(buf, "MODE %d", &mode_num) != 1) {
                    write(client_fd[i], "ERR INVALID_MODE\n", 18);
                    log_write("Invalid MODE from P%d: %s", player_id, buf);
                    continue;
                }

                if (mode_num == 1) {
                    // 사람 vs AI 모드 선택
                    game_mode = MODE_PVAI;
                    log_write("Player %d selected PVAI mode.", player_id);

                    init_board();
                    current_turn = 1;
                    game_over = 0;

                    broadcast(client_fd, "START\n");
                    broadcast(client_fd, "TURN 1\n");
                    continue;

                } else if (mode_num == 2) {
                    // 사람 vs 사람 모드 선택
                    game_mode = MODE_PVP;
                    log_write("Player %d selected PVP mode. Waiting for opponent.", player_id);

                    write(client_fd[i],
                          "상대방을 기다리는 중입니다...\n",
                          strlen("상대방을 기다리는 중입니다...\n"));

                    // 이미 2명이 접속해 있다면 바로 게임 시작
                    if (joined[0] && joined[1]) {
                        log_write("Second player already joined. Starting PVP game.");
                        broadcast(client_fd, "START\n");
                        broadcast(client_fd, "TURN 1\n");
                    }
                    continue;

                } else {
                    // 허용되지 않는 모드 번호
                    write(client_fd[i], "ERR MODE_MUST_BE_1_OR_2\n", 24);
                    log_write("Out-of-range MODE from P%d: %d", player_id, mode_num);
                    continue;
                }
            }

            // CMD_MOVE: 돌 두기 요청 처리
            if (cmd == CMD_MOVE) {
                if (game_over) {
                    write(client_fd[i], "ERR GAME_OVER\n", 14);
                    continue;
                }
                if (player_id != current_turn) {
                    write(client_fd[i], "ERR NOT_YOUR_TURN\n", 19);
                    continue;
                }

                int x, y;
                if (sscanf(buf + 5, "%d %d", &x, &y) != 2) {
                    write(client_fd[i], "ERR BAD_FORMAT\n", 16);
                    continue;
                }

                // 1) 먼저 사람의 수 처리 (모든 모드 공통)
                if (!place_stone(x, y, player_id)) {
                    write(client_fd[i], "ERR INVALID_MOVE\n", 18);
                    continue;
                }

                log_write("Player %d move (%d, %d)", player_id, x, y);

                // 모든 클라이언트에게 방금 둔 수를 방송
                char move_msg[64];
                snprintf(move_msg, sizeof(move_msg), "MOVE %d %d %d\n", player_id, x, y);
                broadcast(client_fd, move_msg);

                // 2) 사람이 이겼는지 먼저 확인
                if (check_win(player_id)) {
                    char win_msg[32];
                    snprintf(win_msg, sizeof(win_msg), "WIN P%d\n", player_id);
                    broadcast(client_fd, win_msg);
                    broadcast(client_fd, "GAME_OVER\n");
                    game_over = 1;
                    log_write("Game Over. Winner: P%d", player_id);
                    continue;
                }

                // ===============================
                //  모드별 분기
                // ===============================

                // (1) 사람 vs 사람(PVP) 모드: 턴만 교대로 변경
                if (game_mode == MODE_PVP) {
                    current_turn = (current_turn == 1) ? 2 : 1;
                    char turn_msg[32];
                    snprintf(turn_msg, sizeof(turn_msg), "TURN %d\n", current_turn);
                    broadcast(client_fd, turn_msg);
                    continue;
                }

                // (2) 사람 vs AI(PVAI) 모드: AI의 수를 바로 계산 및 처리
                if (game_mode == MODE_PVAI) {
                    int ai_player = 2;
                    int ax = -1, ay = -1;

                    // ★ 방금 사람(P1)이 둔 좌표 (x, y)를 기준으로
                    //   AI가 둘 최적의 자리를 계산
                    choose_ai_move(x, y, &ax, &ay);

                    // 안전 장치: 혹시라도 선택 좌표가 유효하지 않으면
                    if (!in_range(ax, ay) || get_stone(ax, ay) != 0) {
                        // fallback: 가장 왼쪽 위부터 빈칸을 찾는 간단한 전략
                        int placed = 0;
                        for (int yy = 0; yy < BOARD_SIZE && !placed; yy++) {
                            for (int xx = 0; xx < BOARD_SIZE && !placed; xx++) {
                                if (place_stone(xx, yy, ai_player)) {
                                    ax = xx;
                                    ay = yy;
                                    placed = 1;
                                }
                            }
                        }
                        // 둘 곳이 없는 경우 (무승부)
                        if (!placed) {
                            broadcast(client_fd, "GAME_OVER\n");
                            game_over = 1;
                            log_write("Game Over. Board full (draw).");
                            continue;
                        }
                    } else {
                        // 정상적으로 선택된 좌표에 AI 돌을 놓음
                        place_stone(ax, ay, ai_player);
                    }

                    // AI가 둔 수를 클라이언트에 알림
                    char ai_move_msg[64];
                    snprintf(ai_move_msg, sizeof(ai_move_msg),
                             "MOVE %d %d %d\n", ai_player, ax, ay);
                    broadcast(client_fd, ai_move_msg);

                    // AI 승리 여부 판정
                    if (check_win(ai_player)) {
                        char win_msg[32];
                        snprintf(win_msg, sizeof(win_msg), "WIN P%d\n", ai_player);
                        broadcast(client_fd, win_msg);
                        broadcast(client_fd, "GAME_OVER\n");
                        game_over = 1;
                        log_write("Game Over. Winner: AI(P2)");
                        continue;
                    }

                    // 게임이 계속되면 다시 사람 차례로 되돌림
                    current_turn = player_id;  // 보통 1
                    char turn_msg[32];
                    snprintf(turn_msg, sizeof(turn_msg), "TURN %d\n", current_turn);
                    broadcast(client_fd, turn_msg);
                    continue;
                    }
                // (예외) 모드가 설정되지 않은 경우: 기본적으로 PVP와 동일하게 턴 교대
                current_turn = (current_turn == 1) ? 2 : 1;
                char turn_msg[32];
                snprintf(turn_msg, sizeof(turn_msg), "TURN %d\n", current_turn);
                broadcast(client_fd, turn_msg);
                continue;
            }


	    
            // CMD_RESTART: 게임이 끝난 뒤 재시작 요청
            if (cmd == CMD_RESTART) {
                if (!game_over) {
                    write(client_fd[i], "ERR NOT_GAME_OVER\n", 18);
                    continue;
                }
                log_write("Game Restart requested by P%d", player_id);
                
                // 1. 보드 초기화
                init_board();
                
                // 2. 상태 초기화
                current_turn = 1;
                game_over = 0;

                // 3. 클라이언트에 알림
                broadcast(client_fd, "RESET\n");
                broadcast(client_fd, "TURN 1\n");
                continue;
            }

           // CMD_EXIT: 한 플레이어가 종료를 요청한 경우 처리
            if (cmd == CMD_EXIT) {
		    log_write("Player %d exited", player_id);

   		    // 상대 플레이어 인덱스 계산 (0 ↔ 1)
   		    int other = (i == 0) ? 1 : 0;

		    // 상대에게 "상대가 나갔습니다" 알림
		    if (client_fd[other] != -1) {
        		write(client_fd[other], "OPPONENT_EXIT\n", 14);
    			}

		    // 나간 쪽 정리
		    close(client_fd[i]);
		    client_fd[i] = -1;
 		    joined[i] = 0;
		    player_count--;

		    // 게임 상태 초기화 (새 게임을 위해 보드/턴/종료 상태 재설정)
		    init_board();
		    current_turn = 1;
		    game_over = 0;

                 /* 이전 버전: 단순히 GAME_OVER 방송만 하고 종료시켰던 코드
                 close(client_fd[i]);
                 client_fd[i] = -1;
                 joined[i] = 0;
                 player_count--;
                 broadcast(client_fd, "GAME_OVER\n"); // 상대방에게 종료 알림
                 game_over = 1;*/
                 continue;
            }
        }
    }

    // 서버 종료 처리
    log_write("Server shutting down...");
    close(server_fd);
    unlink(SOCK_PATH);   // 소켓 파일 삭제
    unlink(PID_FILE);    // PID 파일 삭제
    log_close();         // 로그 파일 정리
    
    return 0;
}

