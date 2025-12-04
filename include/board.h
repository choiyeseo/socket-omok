// 경로: include/board.h
// 역할: 오목판 관련 함수들의 선언을 제공함.

#ifndef BOARD_H
#define BOARD_H

#define BOARD_SIZE 15

// 오목판 초기화
void init_board();

// 오목판 출력 (디버깅용)
void print_board();

// 돌 두기 (성공:1, 실패:0)
int place_stone(int x, int y, int player);

// 승리 판정 (해당 player가 5목이면 1, 아니면 0)
int check_win(int player);

//get info about stone
int get_stone(int x, int y);

#endif

