/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  int listenfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  int connfd;
  char hostname[MAXLINE], port[MAXLINE];

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,&clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);
    Close(connfd);
  }
}


/*
입력 조건:
  - fd는 Accept()로 수립된 유효한 연결 소켓이다

출력 조건:
  - HTTP 요청을 완전히 처리하고 응답을 전송한 상태다
  - 에러 발생 시 적절한 HTTP 에러 응답을 전송하고 반환한다

부수 효과:
  - 요청 라인과 헤더를 stdout에 출력한다
  - fd를 닫지 않는다 — 닫는 책임은 호출자(main)에 있다

반환값: void
*/
void doit(int fd) {

  rio_t rio;
  char buf[MAXLINE];
  char method[MAXLINE];
  char uri[MAXLINE];
  char version[MAXLINE];
  int is_static;
  char filename[MAXLINE];
  char cgiargs[MAXLINE];
  struct stat sbuf;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  if(strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  is_static = parse_uri(uri, filename, cgiargs);

  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static) {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      /*
        1. S_ISREG(sbuf.st_mode)
          - sbuf.st_mode: 파일의 권한/타입 정보
          - S_ISREG(): 일반 파일(regular file)인지 검사
          - !S_ISREG(): 일반 파일이 아님 (디렉토리, 심볼릭링크 등)

        2. S_IRUSR & sbuf.st_mode
          - S_IRUSR: 소유자 읽기 권한 비트 (0400)
          - &: 비트 AND 연산으로 해당 권한 ON인지 확인
          - !(S_IRUSR & sbuf.st_mode): 소유자가 읽기 권한 없음
      */
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  }
  else {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}


/*
입력 조건:
  - rp는 Rio_readinitb()로 초기화된 버퍼다
  - 요청 라인은 이미 소비된 상태다 (doit에서 먼저 읽음)

출력 조건:
  - 빈 라인(\r\n)까지 헤더를 모두 소비한 상태다

부수 효과:
  - 헤더 내용을 stdout에 출력한다
  - 헤더 값을 파싱하거나 저장하지 않는다 (단순 소비)

반환값: void
*/
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);

  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  
  return;
}


/*
입력 조건:
  - uri는 null-terminated 문자열이다
  - filename, cgiargs는 MAXLINE 이상의 버퍼다

출력 조건:
  - filename: URI를 로컬 파일 경로로 변환한 결과 ("." + uri)
  - cgiargs: 정적 요청이면 빈 문자열, 동적 요청이면 ? 뒤의 쿼리스트링
  - uri가 /로 끝나면 filename에 "home.html"이 이어붙여진다
  - 동적 요청 시 uri의 ? 이후가 제거된다 (원본 uri 변형됨 — 주의)

부수 효과:
  - 동적 요청 시 uri 버퍼를 직접 수정한다

반환값: 1(정적) / 0(동적)
*/
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  if (!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/'){
      strcat(filename, "home.html");
    }
    return 1;
  }
  else {
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}


/*
입력 조건:
  - filename은 존재하고 읽기 가능한 일반 파일 경로다
  - filesize는 stat()으로 얻은 실제 파일 크기다

출력 조건:
  - HTTP 응답 헤더와 파일 내용이 fd로 완전히 전송된 상태다

부수 효과:
  - mmap으로 파일을 매핑하고 전송 후 Munmap한다
  - srcfd는 mmap 직후 Close한다
  - 응답 헤더를 stdout에 출력한다

반환값: void
*/
void serve_static(int fd, char *filename, int filesize) {
  char filetype[MAXLINE];
  char buf[MAXBUF];
  int srcfd;
  char *srcp;

  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK \r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd); // fix point
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}


/*
입력 조건:
  - filename은 null-terminated 파일 경로다
  - filetype은 MAXLINE 이상의 버퍼다

출력 조건:
  - filetype에 MIME 타입 문자열이 기록된다

지원 확장자 및 MIME 타입:
  .html → text/html
  .gif  → image/gif
  .png  → image/png
  .jpg  → image/jpeg
  기타  → text/plain

부수 효과: 없음
반환값: void
*/
void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  }
  else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  }
  else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  }
  else if (strstr(filename, "jpg")) {
    strcpy(filetype, "image/jpeg");
  }
  // else if (strstr(filename, "mp4")) {
  //   strcpy(filetype, "video/mp4");
  // }
  else {
    strcpy(filetype, "text/plain");
  }
}


/*
입력 조건:
  - filename은 존재하고 실행 권한이 있는 CGI 프로그램 경로다
  - cgiargs는 null-terminated 쿼리스트링이다 (없으면 빈 문자열)

출력 조건:
  - CGI 프로그램의 stdout 출력이 fd로 전달된 상태다

부수 효과:
  - fork()로 자식 프로세스를 생성한다
  - 자식: QUERY_STRING 환경변수 설정 → Dup2(fd, STDOUT) → execve
  - 부모: Wait(NULL)로 자식 종료 대기

반환값: void
*/
void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE];
  char *emptylist[] = { NULL };

  sprintf(buf, "HTTP/1.0 200 OK \r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) {
    setenv("QUERY_STRING", cgiargs, 1);
    Dup2(fd, STDOUT_FILENO);
    Execve(filename, emptylist, environ);
  }
  Wait(NULL);
}


/*
입력 조건:
  - fd는 유효한 연결 소켓이다
  - errnum은 HTTP 상태 코드 문자열이다 ("404", "403", "501")

출력 조건:
  - HTTP 에러 응답 헤더와 HTML body가 fd로 전송된 상태다

부수 효과: 없음
반환값: void
*/
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char body[MAXBUF];
  char buf[MAXLINE];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}
