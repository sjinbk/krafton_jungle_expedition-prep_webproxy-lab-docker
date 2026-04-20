#include "csapp.h"

/*
#### `main()` — echoclient

```
입력 조건:
  - argc == 3, argv[1]은 host, argv[2]는 포트 번호 문자열이다

출력 조건:
  - stdin EOF 도달 시 서버 연결을 닫고 프로세스가 종료된다

부수 효과:
  - stdin 입력 라인을 서버로 전송하고, 수신한 에코를 stdout으로 출력한다
```
*/
int main(int argc, char **argv) {
    int clientfd;
    char *host;
    char *port;
    char buf[MAXLINE];
    rio_t rio;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }

    host = argv[1];
    port = argv[2];
    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);

    while (Fgets(buf, MAXLINE, stdin) != NULL) {
        Rio_writen(clientfd, buf, strlen(buf));
        Rio_readlineb(&rio, buf, MAXLINE);
        Fputs(buf, stdout);
    }

    Close(clientfd);
    exit(0);
}