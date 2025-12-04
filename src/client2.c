// 경로: src/client2.c
// 역할: 유닉스 도메인 소켓을 통해 오목 서버에 접속하는 클라이언트 프로그램.
//       - 서버와의 메시지 송수신
//       - 로컬 보드 상태 관리 및 화면 출력
//       - 사용자 입력(좌표, exit, restart 등) 처리

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/select.h> 

#define SOCK_PATH "/tmp/omok.sock"   // 서버와 통신할 유닉스 도메인 소켓 경로
#define BOARD_SIZE 15                // 오목판 크기 (15x15)

// 클라이언트 로컬 보드
// 서버에서 수신한 MOVE 정보를 기반으로 화면 표시용으로만 사용
int my_board[BOARD_SIZE][BOARD_SIZE];

int my_player_id=0;   // 서버로부터 할당받은 플레이어 번호 (1 또는 2)
int current_turn=0;   // 현재 턴의 플레이어 번호



// 로컬 보드를 0으로 초기화
void init_my_board() {
    memset(my_board, 0, sizeof(my_board));
}

// UI 그리기 함수
// my_board 배열을 기반으로 콘솔에 오목판을 출력
void draw_board() {
    // 화면 클리어 (ANSI Escape code)
    printf("\033[2J\033[1;1H"); 
    
    // 상단 x 좌표 인덱스 출력
    printf("   ");
    for (int i = 0; i < BOARD_SIZE; i++) printf("%2d ", i);
    printf("\n");

    // 각 행(y)에 대해 돌 상태 출력
    for (int i = 0; i < BOARD_SIZE; i++) {
        printf("%2d ", i);
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (my_board[i][j] == 0) printf(" . ");       // 빈 칸
            else if (my_board[i][j] == 1) printf(" O ");  // Player 1 (흑)
            else if (my_board[i][j] == 2) printf(" X ");  // Player 2 (백)
        }
        printf("\n");
    }
    printf("\nCommands: exit, restart, x y\n");
}

// 개행 문자('\n')까지 fd에서 한 줄을 읽는 유틸 함수
// 성공 시 읽은 길이, EOF/에러 시 0 이하 반환
int read_line(int fd, char* buf, int size) {
    int i = 0;
    char c;
    while (i < size - 1) {
        int n = read(fd, &c, 1);
        if (n <= 0) return n;
        if (c == '\n') {
            buf[i] = '\0';
            return i;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

int main() {
    int fd;
    struct sockaddr_un addr;
    char buf[256];
    int game_over = 0;   // 게임 종료 여부 표시 플래그

    init_my_board();     // 시작 시 로컬 보드 초기화

    // 1. 유닉스 도메인 소켓 생성
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return 1;
    }

    // 2. 소켓 주소 구조체 초기화 및 경로 설정
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SOCK_PATH);

    // 3. 서버에 connect 시도
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(fd);
        return 1;
    }

    // 접속 메시지 (JOIN 명령 전송)
    write(fd, "JOIN user2\n", 11);
    printf("Connected. Waiting for opponent...\n");

    // 메인 이벤트 루프
    while (1) {
        // 보드가 바뀔 때마다 다시 그림 (게임 오버가 아닐 때만)
        if (!game_over) {
            // draw_board(); // 여기서 매번 그리면 깜빡일 수 있으니 MOVE 때 그림
        }

        // select()를 사용하여 키보드 입력(표준입력)과 서버 소켓을 동시에 감시
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(0, &readfds);   // stdin
        FD_SET(fd, &readfds);  // 서버 소켓

        int max_fd = fd;

        if (select(max_fd + 1, &readfds, NULL, NULL, NULL) < 0) break;

        // 서버 메시지 수신 처리
        if (FD_ISSET(fd, &readfds)) {
            int n = read_line(fd, buf, sizeof(buf));
            if (n <= 0) break;

           // 서버에서 OK PLAYER1 / OK PLAYER2 수신 시
	   if(strncmp(buf,"OK PLAYER", 9)==0){
		   int p;
		   if(sscanf(buf,"OK PLAYER%d",&p)==1){
			   my_player_id=p;
			   printf("you are player %d\n",my_player_id);
		   }
	   }

            // 디버깅용 (필요하면 주석 처리)
            // printf("[Server] %s\n", buf);

            // 1. MOVE 파싱 및 UI 업데이트
            // 형식: MOVE <player> <x> <y>
            int p, x, y;
            if (strncmp(buf, "MOVE", 4) == 0) {
                if (sscanf(buf + 5, "%d %d %d", &p, &x, &y) == 3) {
                    if (x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE) {
                        my_board[x][y] = p; // 좌표계 주의 (서버가 x,y를 행,열로 쓰는지 확인 필요)
                        draw_board();
                    }
                }
            }

            // 2. RESET 처리 (서버에서 RESET 수신 시 보드 초기화)
            if (strncmp(buf, "RESET", 5) == 0) {
                init_my_board();
                draw_board();
                game_over = 0;
            }

            // START 수신 시 게임 시작 알림 및 보드 출력
            if (strncmp(buf, "START", 5) == 0) {
                draw_board();
                printf("Game Started!\n");
            }

            // TURN 수신 시 현재 턴 정보 갱신 및 안내 메시지 출력
            if (strncmp(buf, "TURN", 4) == 0) {
                // TURN 메시지가 오면 입력 프롬프트 갱신
                int turn;
                sscanf(buf + 5, "%d", &turn);
		current_turn = turn;

    		if (my_player_id == 0) {
       		 // 아직 내 번호를 못 받은 상태일 수도 있으니 안전하게
        		printf("턴 정보 수신: Player %d 차례\n", current_turn);
    		} else if (current_turn == my_player_id) {
        		printf(">> 지금은 당신(Player %d)의 차례입니다. 행 열을 입력하세요: ", my_player_id);
    		} else {
        		printf(">> 지금은 상대(Player %d)의 차례입니다. 기다려 주세요.\n", current_turn);
    		}

                //printf(">> TURN Player %d: ", turn);
                fflush(stdout);
            }

            // WIN 문자열이 포함된 경우 승리 메시지 출력
            if (strstr(buf, "WIN")) {
                printf("\n🏆 %s 🏆\n", buf);
            }

            // GAME_OVER 수신 시 게임 종료 상태로 전환
            if (strstr(buf, "GAME_OVER")) {
                game_over = 1;
                printf("Game Over. Type 'restart' to play again or 'exit'.\n");
            }

	    // 상대 클라이언트 종료 알림
	    if (strncmp(buf, "OPPONENT_EXIT", 13) == 0) {
                printf("상대가 나갔습니다. 프로그램을 종료합니다.\n");
                break;   // 메인 while(1) 탈출
            }
        }

        // 키보드 입력 처리
        if (FD_ISSET(0, &readfds)) {
            char input[128];
            if (!fgets(input, sizeof(input), stdin)) break;
            
            // 개행 제거
            input[strcspn(input, "\n")] = 0;
            if (strlen(input) == 0) continue;

            // 1) exit 명령
            if (strcmp(input, "exit") == 0) {
                write(fd, "EXIT\n", 5);
                break;

            // 2) restart 명령
            } else if (strcmp(input, "restart") == 0) {
                write(fd, "RESTART\n", 8);

            // 3) 좌표 입력 처리
            } else {
		         // 여기서부터 "좌표 입력"은 내 턴일 때만 허용
                if (game_over) {
                        printf("이미 게임이 종료되었습니다. 'restart' 또는 'exit'만 가능합니다.\n");
                        continue;
                }
                if (my_player_id == 0) {
                        printf("아직 플레이어 번호를 받지 못했습니다. 잠시만 기다려 주세요.\n");
                        continue;
                }
                if (current_turn != my_player_id) {
                        printf("지금은 상대(Player %d)의 차례입니다. 좌표를 입력할 수 없습니다.\n",
                        current_turn);
                        continue;
                }

                int r, c;
                if (sscanf(input, "%d %d", &r, &c) == 2) {

			// ① 좌표 범위 검사 (0 ~ BOARD_SIZE-1)
                    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) {
                        printf("유효하지 않은 좌표값입니다. 0 ~ %d 사이의 값을 입력해 주세요.\n",
                               BOARD_SIZE - 1);
                        continue;   // 서버로 보내지 않고 다시 입력 기다림
                    }

                    // ② 이미 돌이 있는지 검사
                    if (my_board[r][c] != 0) {
                        printf("이미 말이 있습니다. 다른 좌표를 선택해 주세요.\n");
                        continue;   // 서버로 보내지 않고 다시 입력 기다림
                    }

                    // 서버로 MOVE 명령 전송
                    char msg[64];
                    snprintf(msg, sizeof(msg), "MOVE %d %d\n", r, c);
                    write(fd, msg, strlen(msg));
                }else {
                        printf("좌표는 '행 열' 형식으로 입력해 주세요. 예) 7 8\n");
                }

            }
        }
    }

    // 소켓 닫고 종료
    close(fd);
    return 0;
}

