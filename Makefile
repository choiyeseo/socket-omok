CC = gcc
CFLAGS = -Wall -g -Iinclude

# 타겟 목록
all: server client client2

# server 컴파일 시 src/log.c 추가 필수!
server: src/server.c src/board.c src/protocol.c src/log.c
	$(CC) $(CFLAGS) -o server src/server.c src/board.c src/protocol.c src/log.c

client: src/client.c
	$(CC) $(CFLAGS) -o client src/client.c

client2: src/client2.c
	$(CC) $(CFLAGS) -o client2 src/client2.c

clean:
	rm -f server client client2 *.o omok.log
