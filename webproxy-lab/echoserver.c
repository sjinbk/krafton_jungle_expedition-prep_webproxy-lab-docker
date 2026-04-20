#include "csapp.h"

/*
#### `echo(int connfd)`

```
입력 조건(precondition):
  - connfd는 Accept()로 수립된 유효한 연결 소켓 파일 디스크립터다
  - connfd는 읽기/쓰기 모두 가능한 상태다

출력 조건(postcondition):
  - 클라이언트가 전송한 모든 라인이 동일 순서로 클라이언트에게 되돌아간다
  - 함수 반환 시점에 클라이언트는 EOF를 수신했거나 연결 오류가 발생한 상태다

부수 효과:
  - 수신한 바이트 수를 stdout에 출력한다
  - connfd를 닫지 않는다 — 닫는 책임은 호출자(main)에게 있다
  - rio_t 버퍼는 함수 내부에서 선언되고 소멸한다 (스택 변수)

반환값: void
에러 처리: Rio_* 래퍼가 에러 시 프로세스를 종료한다
```
*/
void echo(int connfd) {
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0){
        printf("server received %d bytes\n", (int)n);
        Rio_writen(connfd, buf, n);
    }
}

/*
#### `main()` — echoserver

```
입력 조건:
  - argc == 2, argv[1]은 유효한 포트 번호 문자열이다

출력 조건:
  - 프로세스가 명시적으로 종료되지 않는 한 무한 루프에서 연결을 처리한다

부수 효과:
  - 클라이언트 연결마다 hostname/port를 stdout에 출력한다
  - 각 echo() 호출 완료 후 connfd를 Close한다
  - listenfd는 닫지 않는다
```
*/
int main(int argc, char **argv) {
    int listenfd;
    int connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE];
    char client_port[MAXLINE];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        echo(connfd);
        Close(connfd);
    }
    
    exit(0);
}