# Tiny 웹 서버 개발 명세서

## 프로젝트 개요

HTTP/1.0 GET 요청을 처리하는 단순 iterative 웹 서버를 구현한다. 정적 파일(static content) 서빙과 CGI 프로그램 실행을 통한 동적 응답(dynamic content) 생성을 모두 지원한다. CSAPP 교재의 Tiny 서버를 기반으로 한다.

---

## 기술 스택

| 항목 | 요구사항 |
|------|----------|
| 언어 | C |
| 프로토콜 | HTTP/1.0 |
| 소켓 API | POSIX BSD Socket |
| 헬퍼 라이브러리 | csapp.h / csapp.c |
| 파일 서빙 방식 | mmap + Rio_writen |
| 동적 컨텐츠 실행 | fork + execve (CGI) |
| 빌드 도구 | Makefile |
| 실행 환경 | Linux (Ubuntu 20.04 이상) |

---

## 핵심 개념 및 용어 정의

| 용어 | 정의 |
|------|------|
| **HTTP/1.0** | HyperText Transfer Protocol 버전 1.0. 요청/응답 후 연결을 즉시 닫는 stateless 프로토콜 |
| **GET** | HTTP 메서드 중 리소스 조회를 요청하는 방식. Tiny는 이것만 지원한다 |
| **정적 컨텐츠(static content)** | 서버 파일시스템에 이미 존재하는 파일을 그대로 전송하는 응답 |
| **동적 컨텐츠(dynamic content)** | CGI 프로그램을 실행하여 런타임에 생성되는 응답 |
| **CGI** | Common Gateway Interface. 웹 서버가 외부 프로그램을 실행하고 그 stdout을 HTTP 응답으로 전달하는 규약 |
| **mmap** | 파일을 프로세스 가상 주소 공간에 매핑하는 시스템 콜. 파일을 메모리처럼 읽을 수 있게 한다 |
| **Munmap** | mmap으로 매핑한 메모리를 해제하는 시스템 콜 |
| **QUERY_STRING** | CGI 프로그램에 전달되는 환경변수. URI의 `?` 뒤 부분을 담는다 |
| **Dup2** | 파일 디스크립터를 복제한다. CGI에서 connfd를 STDOUT_FILENO로 리다이렉트하는 데 사용 |
| **stat / sbuf** | 파일의 메타데이터(크기, 타입, 권한 등)를 담는 구조체. `stat()` 시스템 콜로 채운다 |
| **iterative 서버** | 클라이언트를 하나씩 순차 처리하는 서버 구조 |

---

## 기능 요구사항

### 서버 기동

| ID | 요구사항 | 비고 |
|----|----------|------|
| S-01 | 지정 포트에서 TCP 연결 요청을 수신한다 | argv[1]로 포트 전달 |
| S-02 | 클라이언트 연결 수립 시 hostname/port를 stdout에 출력한다 | |
| S-03 | 포트 인자 없이 실행 시 usage 메시지를 stderr에 출력하고 종료한다 | argc != 2, exit(1) |

### HTTP 요청 처리

| ID | 요구사항 | 비고 |
|----|----------|------|
| R-01 | 요청 라인(method, uri, version)을 파싱한다 | `sscanf` 사용 |
| R-02 | GET 이외의 메서드는 501 Not Implemented로 응답한다 | `strcasecmp` 대소문자 무시 비교 |
| R-03 | 요청 헤더를 읽고 버린다 (파싱 없음) | 빈 라인(`\r\n`)까지 소비 |
| R-04 | URI에 `cgi-bin`이 없으면 정적 요청으로 판단한다 | |
| R-05 | URI에 `cgi-bin`이 있으면 동적 요청으로 판단한다 | |
| R-06 | 파일이 존재하지 않으면 404 Not Found로 응답한다 | `stat()` 반환값 검사 |
| R-07 | 정적 요청 시 일반 파일이 아니거나 읽기 권한이 없으면 403 Forbidden으로 응답한다 | `S_ISREG`, `S_IRUSR` 검사 |
| R-08 | 동적 요청 시 일반 파일이 아니거나 실행 권한이 없으면 403 Forbidden으로 응답한다 | `S_ISREG`, `S_IXUSR` 검사 |

### 정적 컨텐츠 서빙

| ID | 요구사항 | 비고 |
|----|----------|------|
| SC-01 | 파일 확장자로 Content-Type을 결정한다 | .html / .gif / .png / .jpg / 기타 |
| SC-02 | HTTP/1.0 200 OK 응답 헤더를 전송한다 | Content-length, Content-type 포함 |
| SC-03 | 파일을 mmap으로 매핑하여 전송하고 즉시 Munmap한다 | srcfd는 mmap 직후 Close |
| SC-04 | URI가 `/`로 끝나면 `home.html`을 기본 파일로 사용한다 | |

### 동적 컨텐츠 서빙

| ID | 요구사항 | 비고 |
|----|----------|------|
| DC-01 | `HTTP/1.0 200 OK` 와 `Server` 헤더를 부모 프로세스가 먼저 전송한다 | |
| DC-02 | fork()로 자식 프로세스를 생성한다 | |
| DC-03 | 자식 프로세스는 URI의 `?` 뒤 쿼리스트링을 `QUERY_STRING` 환경변수로 설정한다 | |
| DC-04 | 자식 프로세스는 connfd를 STDOUT_FILENO에 복제하고 CGI 프로그램을 execve로 실행한다 | CGI stdout이 소켓으로 직접 전송됨 |
| DC-05 | 부모 프로세스는 `Wait(NULL)`로 자식 종료를 기다린다 | zombie 프로세스 방지 |

### 에러 응답

| ID | 요구사항 | 비고 |
|----|----------|------|
| E-01 | 에러 응답은 HTTP 상태 코드 + HTML body로 구성한다 | |
| E-02 | Content-type: text/html, Content-length를 헤더에 포함한다 | |

---

## 비기능 요구사항

| ID | 요구사항 |
|----|----------|
| NF-01 | 모든 소켓/파일 I/O는 csapp 래퍼 함수를 사용한다 |
| NF-02 | connfd는 doit() 처리 완료 후 main에서 Close한다 |
| NF-03 | mmap한 메모리는 Rio_writen 직후 Munmap한다 |
| NF-04 | srcfd는 mmap 직후 Close한다 (전송 전에 닫아도 매핑은 유지됨) |

---

## 인터페이스 명세

### 실행 인터페이스

```
./tiny <port>
예: ./tiny 8080
```

### 함수 계약 명세

#### `doit(int fd)`

```
입력 조건:
  - fd는 Accept()로 수립된 유효한 연결 소켓이다

출력 조건:
  - HTTP 요청을 완전히 처리하고 응답을 전송한 상태다
  - 에러 발생 시 적절한 HTTP 에러 응답을 전송하고 반환한다

부수 효과:
  - 요청 라인과 헤더를 stdout에 출력한다
  - fd를 닫지 않는다 — 닫는 책임은 호출자(main)에 있다

반환값: void
```

#### `read_requesthdrs(rio_t *rp)`

```
입력 조건:
  - rp는 Rio_readinitb()로 초기화된 버퍼다
  - 요청 라인은 이미 소비된 상태다 (doit에서 먼저 읽음)

출력 조건:
  - 빈 라인(\r\n)까지 헤더를 모두 소비한 상태다

부수 효과:
  - 헤더 내용을 stdout에 출력한다
  - 헤더 값을 파싱하거나 저장하지 않는다 (단순 소비)

반환값: void
```

#### `parse_uri(char *uri, char *filename, char *cgiargs)`

```
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
```

#### `serve_static(int fd, char *filename, int filesize)`

```
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
```

#### `serve_dynamic(int fd, char *filename, char *cgiargs)`

```
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
```

#### `clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)`

```
입력 조건:
  - fd는 유효한 연결 소켓이다
  - errnum은 HTTP 상태 코드 문자열이다 ("404", "403", "501")

출력 조건:
  - HTTP 에러 응답 헤더와 HTML body가 fd로 전송된 상태다

부수 효과: 없음
반환값: void
```

#### `get_filetype(char *filename, char *filetype)`

```
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
```

---

## 요청 처리 흐름

```
[클라이언트 HTTP 요청]
        │
        ▼
     doit(fd)
        │
        ├─ Rio_readlineb() → 요청 라인 파싱 (method, uri, version)
        │
        ├─ method != GET? ──► clienterror(501) → return
        │
        ├─ read_requesthdrs() → 헤더 소비
        │
        ├─ parse_uri() → filename, cgiargs 결정, is_static 판단
        │
        ├─ stat(filename) 실패? ──► clienterror(404) → return
        │
        ├─ is_static == 1
        │      │
        │      ├─ 권한/타입 검사 실패? ──► clienterror(403) → return
        │      └─ serve_static(fd, filename, filesize)
        │
        └─ is_static == 0
               │
               ├─ 권한/타입 검사 실패? ──► clienterror(403) → return
               └─ serve_dynamic(fd, filename, cgiargs)
                      │
                      └─ fork()
                             ├─ [자식] setenv → Dup2 → Execve(CGI)
                             └─ [부모] Wait(NULL)
```

---

## URI 파싱 규칙

| URI 형태 | is_static | filename | cgiargs |
|----------|-----------|----------|---------|
| `/index.html` | 1 | `./index.html` | `""` |
| `/` | 1 | `./home.html` | `""` |
| `/images/cat.png` | 1 | `./images/cat.png` | `""` |
| `/cgi-bin/adder` | 0 | `./cgi-bin/adder` | `""` |
| `/cgi-bin/adder?a=1&b=2` | 0 | `./cgi-bin/adder` | `"a=1&b=2"` |

**주의:** `parse_uri`는 동적 요청 시 `uri` 버퍼를 직접 수정한다(`*ptr = '\0'`). 호출자는 이후 원본 uri를 신뢰할 수 없다.

---

## 불변 조건 (Invariant)

| ID | 조건 |
|----|------|
| INV-01 | serve_static 실행 시 srcfd는 mmap 직후, Rio_writen 이전에 Close된다 |
| INV-02 | serve_static 실행 시 srcp는 Rio_writen 완료 후 반드시 Munmap된다 |
| INV-03 | serve_dynamic의 자식 프로세스는 execve 전에 반드시 Dup2(fd, STDOUT_FILENO)를 완료한다 |
| INV-04 | clienterror는 항상 Content-length 헤더를 포함한다 |
| INV-05 | doit() 반환 후 main에서 반드시 Close(connfd)가 호출된다 |

---

## 구현 단계 분할 (수직 슬라이스)

### 슬라이스 1: 서버 기동 및 연결 수락

**구현 대상:** `main()` — Open_listenfd, Accept, Close 루프

**stub 처리:**
```c
void doit(int fd) {
    // stub: 연결만 수락하고 닫는다
    (void)fd;
}
```

**검증 항목:**
- 지정 포트에서 서버가 기동되는가?
- 클라이언트 연결 시 hostname/port가 stdout에 출력되는가?
- 연결 종료 후 서버가 다음 연결을 대기하는가?

```bash
./tiny 8080 &
curl -v http://localhost:8080/   # 연결만 되면 됨, 응답 내용 무관
# stdout에 "Accepted connection from (localhost, xxxxx)" 확인
```

---

### 슬라이스 2: 요청 파싱 및 에러 응답

**구현 대상:** `doit()` 요청 라인 파싱 부분, `read_requesthdrs()`, `clienterror()`

**stub 처리:**
```c
// parse_uri stub: 항상 정적 요청으로 처리, filename은 고정
int parse_uri(char *uri, char *filename, char *cgiargs) {
    strcpy(filename, "./index.html");
    strcpy(cgiargs, "");
    return 1;
}
// serve_static, serve_dynamic stub: 아무것도 하지 않음
void serve_static(int fd, char *filename, int filesize) { (void)fd; }
void serve_dynamic(int fd, char *filename, char *cgiargs) { (void)fd; }
```

**검증 항목:**
- GET 이외 메서드 요청 시 501 응답이 오는가?
- 존재하지 않는 파일 요청 시 404 응답이 오는가?
- 에러 응답 body가 HTML인가?

```bash
curl -X POST http://localhost:8080/   # 501 확인
curl http://localhost:8080/no_such_file.html   # 404 확인
```

---

### 슬라이스 3: URI 파싱

**구현 대상:** `parse_uri()`

**stub 해제:** parse_uri 실제 구현으로 교체

**검증 항목:**
- `/` → filename이 `./home.html`인가?
- `/index.html` → filename이 `./index.html`인가?
- `/cgi-bin/adder?a=1&b=2` → filename `./cgi-bin/adder`, cgiargs `a=1&b=2`인가?
- 반환값이 정적이면 1, 동적이면 0인가?

```c
// 단위 테스트 예시 (serve_* stub 상태에서 실행)
char filename[MAXLINE], cgiargs[MAXLINE];
char uri1[] = "/";
assert(parse_uri(uri1, filename, cgiargs) == 1);
assert(strcmp(filename, "./home.html") == 0);

char uri2[] = "/cgi-bin/adder?a=1&b=2";
assert(parse_uri(uri2, filename, cgiargs) == 0);
assert(strcmp(cgiargs, "a=1&b=2") == 0);
```

---

### 슬라이스 4: 정적 파일 서빙

**구현 대상:** `serve_static()`, `get_filetype()`

**stub 해제:** serve_static 실제 구현으로 교체

**검증 항목:**
- `./home.html`이 존재할 때 200 응답과 파일 내용이 오는가?
- Content-Type이 파일 확장자에 맞는가?
- 403 조건(읽기 권한 없는 파일)이 올바르게 처리되는가?

```bash
echo "<h1>Hello</h1>" > home.html
curl -v http://localhost:8080/
# HTTP/1.0 200 OK, Content-type: text/html 확인

chmod 000 home.html
curl -v http://localhost:8080/
# 403 Forbidden 확인
chmod 644 home.html
```

---

### 슬라이스 5: 동적 컨텐츠 서빙 (CGI)

**구현 대상:** `serve_dynamic()`

**stub 해제:** serve_dynamic 실제 구현으로 교체

**검증 항목:**
- CGI 프로그램이 실행되고 stdout이 클라이언트에게 전달되는가?
- QUERY_STRING 환경변수가 올바르게 설정되는가?
- 부모 프로세스가 자식 종료를 Wait하는가? (zombie 프로세스 없음)

```bash
# 테스트용 CGI 프로그램
cat > cgi-bin/hello << 'EOF'
#!/bin/sh
echo "Content-type: text/plain"
echo ""
echo "Hello, QUERY_STRING=$QUERY_STRING"
EOF
chmod +x cgi-bin/hello
mkdir -p cgi-bin

curl "http://localhost:8080/cgi-bin/hello?name=world"
# "Hello, QUERY_STRING=name=world" 확인
```

---

## 제약 및 설계 결정

### 범위 내

- HTTP/1.0 GET 메서드만 처리
- 정적: .html / .gif / .png / .jpg / 기타(text/plain)
- 동적: CGI 방식, fork + execve

### 범위 외 (미구현, 이유 명시)

| 항목 | 결정 | 근거 |
|------|------|------|
| POST, HEAD 등 다른 메서드 | 501 반환 | 학습 목적 구현, 확장 가능 |
| HTTP/1.1 persistent connection | 미지원 | HTTP/1.0 기준, 응답 후 연결 종료 |
| 동시 클라이언트 처리 | 미구현 | iterative 구조 — 동시성 제어 불필요 |
| 요청 헤더 파싱 | 미구현 | Host, Cookie 등 무시하고 소비만 함 |
| mp4 등 추가 MIME 타입 | 미구현 | 코드에 주석 처리된 상태로 남겨둠 |
| 디렉토리 리스팅 | 미구현 | / 요청 시 home.html로 고정 대응 |

---

## 빌드 명세

```makefile
CC = gcc
CFLAGS = -Wall -O2
LIBS = -lpthread

all: tiny

tiny: tiny.c csapp.c
	$(CC) $(CFLAGS) -o tiny tiny.c csapp.c $(LIBS)

clean:
	rm -f tiny
```

**의존 파일:** `csapp.h`, `csapp.c` — CSAPP 교재 공식 배포본 사용

---

## 테스트 시나리오

| 슬라이스 | 시나리오 | 요청 | 기대 결과 |
|----------|----------|------|-----------|
| 1 | 서버 기동 | `./tiny 8080` | 포트 바인딩 성공 |
| 1 | 연결 수락 | curl 접속 | `Accepted connection from (...)` 출력 |
| 2 | 잘못된 메서드 | `POST /` | 501 Not Implemented |
| 2 | 존재하지 않는 파일 | `GET /ghost.html` | 404 Not Found |
| 3 | 루트 요청 URI 파싱 | `GET /` | filename = `./home.html` |
| 3 | CGI URI 파싱 | `GET /cgi-bin/add?a=1` | filename = `./cgi-bin/add`, cgiargs = `a=1` |
| 4 | 정적 HTML 서빙 | `GET /index.html` | 200 OK, text/html |
| 4 | 정적 이미지 서빙 | `GET /logo.png` | 200 OK, image/png |
| 4 | 읽기 권한 없음 | `GET /secret.html` (chmod 000) | 403 Forbidden |
| 5 | CGI 실행 | `GET /cgi-bin/hello?name=world` | CGI stdout이 응답 body로 전달 |
| 5 | 실행 권한 없음 | `GET /cgi-bin/hello` (chmod 644) | 403 Forbidden |
| 1~5 | 연속 요청 | 여러 요청 순차 전송 | 서버 재기동 없이 모두 처리 |

---

## 추가 고려사항

- **대용량 파일 서빙:** mmap은 filesize 전체를 한 번에 매핑한다. 매우 큰 파일에서는 메모리 부족이 발생할 수 있다. 청크 단위 read + write 방식으로 대체 가능
- **CGI 에러 처리:** 자식 프로세스가 execve 실패 시 부모는 이를 감지하지 못한다. 자식에서 실패 시 적절한 에러 응답을 보내고 exit하는 처리가 누락되어 있다
- **Concurrent 확장:** 현재 iterative 구조에서는 한 CGI 프로그램이 느리면 다음 요청이 전부 블로킹된다. pre-fork 또는 thread pool 방식으로 확장 가능
- **parse_uri의 uri 변형 부작용:** `*ptr = '\0'`으로 원본 uri를 직접 수정한다. 이후 호출자가 원본 uri를 재사용하는 코드가 추가되면 버그로 이어질 수 있다. 방어적으로 uri 복사본을 만들어 처리하는 것이 안전하다
- **clienterror의 body 버퍼 크기:** body를 MAXBUF(8192)에 조립한다. 에러 메시지가 길면 오버플로 가능성이 있다. snprintf로 교체하는 것이 권장된다