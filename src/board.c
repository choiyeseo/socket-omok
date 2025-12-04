// 경로: src/board.c
// 역할: 오목판을 관리하고 돌을 두며, 승리 여부를 판정함.

#include <stdio.h>
#include "board.h"

// 전역 오목판 배열
static int board[BOARD_SIZE][BOARD_SIZE];


int get_stone(int x, int y) {
    if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) return -1;
    return board[y][x];  // ★ 다른 함수들과 동일하게 [y][x] 사용
}

// 오목판 초기화
void init_board() {
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            board[y][x] = 0;
        }
    }
}

// 오목판 출력 (서버 디버깅용)
void print_board() {
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {
            printf("%d ", board[y][x]);
        }
        printf("\n");
    }
}

// 돌 두기 (성공:1, 실패:0)
int place_stone(int x, int y, int player) {
    if (x < 0 || x >= BOARD_SIZE || y < 0 || y >= BOARD_SIZE) {
        return 0;
    }
    if (board[y][x] != 0) {
        return 0;
    }
    board[y][x] = player;
    return 1;
}

// 승리 판정 (player 돌이 5개 연속이면 1 반환)
int check_win(int player) {
    for (int y = 0; y < BOARD_SIZE; y++) {
        for (int x = 0; x < BOARD_SIZE; x++) {

            if (board[y][x] != player) continue;

            // 가로
            if (x + 4 < BOARD_SIZE) {
                if (board[y][x+1] == player &&
                    board[y][x+2] == player &&
                    board[y][x+3] == player &&
                    board[y][x+4] == player) {
                    return 1;
                }
            }

            // 세로
            if (y + 4 < BOARD_SIZE) {
                if (board[y+1][x] == player &&
                    board[y+2][x] == player &&
                    board[y+3][x] == player &&
                    board[y+4][x] == player) {
                    return 1;
                }
            }

            // 대각선 ↘
            if (x + 4 < BOARD_SIZE && y + 4 < BOARD_SIZE) {
                if (board[y+1][x+1] == player &&
                    board[y+2][x+2] == player &&
                    board[y+3][x+3] == player &&
                    board[y+4][x+4] == player) {
                    return 1;
                }
            }

            // 대각선 ↗
            if (x + 4 < BOARD_SIZE && y - 4 >= 0) {
                if (board[y-1][x+1] == player &&
                    board[y-2][x+2] == player &&
                    board[y-3][x+3] == player &&
                    board[y-4][x+4] == player) {
                    return 1;
                }
            }
        }
    }

    return 0;
}

