# HTTP 웹 프록시 개발 명세서

## 문서 목적

이 문서는 Proxy Lab 구현을 위한 단일 기준 문서다. 앞부분은 채점과 직접 연결되는 필수 요구사항을 정리하고, 뒷부분은 실제 구현을 시작할 때 바로 사용할 수 있는 권장 내부 설계 가이드를 제시한다.

이 문서의 우선순위는 다음과 같다.

1. `driver.sh`
2. `proxy.c`, `csapp.c`, `csapp.h`, `tiny/tiny.c`
3. 본 문서

문서와 코드가 충돌하면 코드와 채점 스크립트를 우선한다. 문서 안의 "권장", "예시", "가능" 표현은 구현 방식을 고정하는 규범이 아니라 설계 가이드다.

---

## 프로젝트 개요

웹 브라우저와 웹 서버 사이에서 HTTP 요청을 중계하는 캐싱 프록시 서버를 구현한다. 클라이언트 요청을 받아 원본 서버로 전달하고, 응답을 다시 클라이언트에 돌려준다. 구현은 순차 처리(Part I), 동시 처리(Part II), 캐싱(Part III)으로 확장되며, 최종 평가는 `driver.sh`를 기준으로 한다.

**주어진 자산**
- `proxy.c`: 프록시 구현 대상 파일
- `csapp.h`, `csapp.c`: 소켓 및 RIO 헬퍼 라이브러리
- `tiny/`: 테스트용 원본 웹 서버와 정적 파일
- `nop-server.py`: 응답하지 않는 테스트 서버
- `driver.sh`: 자동 채점 스크립트

---

## 기술 스택

| 항목 | 내용 |
|------|------|
| 언어 | C |
| 프로토콜 | HTTP/1.0 중심, 클라이언트 요청은 HTTP/1.1 수신 가능 |
| 네트워크 API | POSIX BSD sockets |
| I/O | `csapp`의 RIO 패키지 |
| 동시성 | POSIX Threads, detached thread |
| 캐시 동기화 | readers-writers lock |
| 빌드 | `Makefile`, `gcc` |
| 실행 환경 | Linux |

---

## 핵심 용어

| 용어 | 정의 |
|------|------|
| 프록시 | 클라이언트와 원본 서버 사이에서 요청과 응답을 중계하는 프로그램 |
| 순차 프록시 | 한 요청 처리가 끝난 뒤 다음 요청을 받는 구조 |
| 동시 프록시 | 연결마다 별도 스레드를 두고 병렬 처리하는 구조 |
| detached thread | 종료 시 스레드 자원이 자동 회수되는 스레드 |
| RIO | short count에 안전한 `csapp`의 robust I/O 패키지 |
| readers-writers lock | 여러 읽기 접근은 동시에 허용하고, 쓰기는 단독으로만 허용하는 락 |
| LRU 근사 | 가장 최근에 덜 사용된 항목을 우선 퇴출하는 단순 정책 |
| SIGPIPE / EPIPE | 닫힌 소켓에 쓰기 시도 시 발생할 수 있는 종료 위험 |
| ECONNRESET | 연결 상대가 먼저 연결을 끊은 경우 발생하는 오류 |

---

## 시스템 개요

```text
[Client]
  |
  | HTTP/1.1 request line with absolute URI
  v
[Proxy]
  1. 요청 라인 파싱
  2. 필수 헤더 정리
  3. 필요 시 캐시 조회
  4. 원본 서버 연결
  5. HTTP/1.0 요청 전달
  6. 응답 스트리밍
  7. 조건 충족 시 캐시 저장
  |
  | HTTP/1.0 request with origin-form path
  v
[Origin Server: tiny]
```

---

## 주어진 상수

```c
#define MAX_CACHE_SIZE  1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
    "Gecko/20120305 Firefox/10.0.3\r\n";
```

위 상수는 수정하지 않는다. 프록시는 항상 이 `User-Agent` 값을 사용한다.

---

## 기능 요구사항

### Part I: Basic (40점)

| ID | 필수 요구사항 | 채점 기준 |
|----|---------------|-----------|
| P1-01 | 지정 포트에서 TCP 연결을 수신한다 | `./proxy <port>` 실행 |
| P1-02 | 클라이언트의 HTTP GET 요청 라인과 헤더를 읽는다 | 요청 수신 가능 |
| P1-03 | absolute URI에서 host, port, path를 추출한다 | `http://host[:port]/path` |
| P1-04 | URI에 포트가 없으면 기본 포트 `80`을 사용한다 | 기본 포트 처리 |
| P1-05 | 원본 서버에 연결하고 요청을 전달한다 | 연결 및 전달 성공 |
| P1-06 | 원본 서버로 보내는 요청 라인의 버전은 `HTTP/1.0`이다 | 버전 강제 변환 |
| P1-07 | 원본 서버로 보내는 요청 라인은 absolute URI가 아니라 path만 사용한다 | `GET /path HTTP/1.0` |
| P1-08 | `Host`, `User-Agent`, `Connection`, `Proxy-Connection` 헤더를 올바르게 보낸다 | 필수 헤더 4종 |
| P1-09 | 위 4종을 제외한 추가 요청 헤더는 원본 서버로 전달한다 | 추가 헤더 유지 |
| P1-10 | 응답 헤더와 본문을 텍스트/바이너리 구분 없이 클라이언트에 전달한다 | `godzilla.jpg`, `tiny` 비교 |
| P1-11 | 요청 완료 뒤 클라이언트와 서버 소켓을 적절히 정리한다 | fd 누수 없음 |

### 필수 요청 헤더 규칙

| 헤더 | 필수 동작 |
|------|-----------|
| `Host` | 브라우저가 보낸 `Host` 값이 있으면 그것을 우선 사용하고, 없으면 URI에서 생성한다 |
| `User-Agent` | `proxy.c`에 주어진 상수값을 사용한다 |
| `Connection` | `close`로 고정한다 |
| `Proxy-Connection` | `close`로 고정한다 |

### Part II: Concurrency (15점)

| ID | 필수 요구사항 | 채점 기준 |
|----|---------------|-----------|
| P2-01 | 새 연결마다 독립적인 실행 흐름으로 요청을 처리한다 | `pthread_create` 등 |
| P2-02 | 스레드 자원이 누수되지 않도록 detached 방식으로 동작한다 | detach 필요 |
| P2-03 | `Accept` 루프의 지역 변수와 경합하지 않도록 연결 식별자를 안전하게 넘긴다 | connfd 복사 |
| P2-04 | 응답하지 않는 서버 요청이 다른 정상 요청을 막지 않아야 한다 | `nop-server.py` 테스트 |

### Part III: Cache (15점)

| ID | 필수 요구사항 | 채점 기준 |
|----|---------------|-----------|
| P3-01 | 전체 캐시 사용량은 `MAX_CACHE_SIZE`를 넘지 않는다 | 총 용량 제한 |
| P3-02 | 단일 오브젝트가 `MAX_OBJECT_SIZE`를 넘으면 저장하지 않는다 | 개별 객체 제한 |
| P3-03 | 캐시 히트 시 원본 서버 연결 없이 저장된 응답을 전달한다 | Tiny 종료 후 재요청 |
| P3-04 | 용량 부족 시 기존 항목을 퇴출한다 | LRU 근사 허용 |
| P3-05 | 여러 스레드가 캐시를 동시에 읽을 수 있어야 한다 | read concurrency |
| P3-06 | 캐시 쓰기와 퇴출은 단독 접근으로 보호한다 | write exclusivity |

---

## 비기능 요구사항

| ID | 필수 요구사항 | 근거 |
|----|---------------|------|
| NF-01 | 소켓 기반 입출력은 RIO 패키지를 사용한다 | CS:APP Proxy Lab 힌트 |
| NF-02 | `SIGPIPE`는 무시한다 | 소켓 쓰기 중 프로세스 종료 방지 |
| NF-03 | 연결 하나의 실패가 프록시 프로세스 전체 종료로 이어지면 안 된다 | 견고성 |
| NF-04 | 응답 본문 처리에 `strlen`을 사용하지 않는다 | 바이너리 안전성 |
| NF-05 | `EPIPE`, `ECONNRESET` 등은 연결 단위 실패로 처리한다 | 견고성 |
| NF-06 | fd와 heap 메모리 누수가 없어야 한다 | 안정성 |
| NF-07 | 비정상 입력에도 segfault 없이 종료 또는 오류 응답으로 처리한다 | 안정성 |

---

## 오류 및 견고성 요구

아래 항목은 구현 방식이 아니라 반드시 만족해야 하는 동작 기준이다.

- 닫힌 클라이언트 소켓에 쓰는 도중 `SIGPIPE`로 프로세스가 죽지 않아야 한다.
- 원본 서버 연결 실패, DNS 실패, 중간 전송 실패는 현재 연결 처리 실패로 끝나야 하며 프록시 전체가 종료되면 안 된다.
- 바이너리 응답은 바이트 단위로 그대로 전달해야 한다.
- 클라이언트 연결 fd, 서버 연결 fd, 스레드 인자 포인터, 캐시 데이터 버퍼의 수명은 명확해야 한다.
- 동시성 추가 뒤에도 각 연결의 실패가 다른 연결 처리에 영향을 주지 않아야 한다.

---

## 범위

### 범위 내

- HTTP `GET` 요청
- absolute URI 형식의 프록시 요청
- HTTP/1.0으로의 요청 변환
- 텍스트 및 바이너리 응답 포워딩
- 메모리 기반 캐시

### 범위 외

| 항목 | 처리 방침 |
|------|-----------|
| `POST` 등 다른 메서드 | 필수 아님 |
| HTTPS / TLS | 필수 아님 |
| persistent connection | 지원 대상 아님 |
| 멀티라인 요청 헤더 | 필수 아님 |
| 디스크 기반 캐시 | 필수 아님 |

---

## 외부 인터페이스

프록시의 외부 실행 인터페이스는 다음 하나만 유지한다.

```bash
./proxy <port>
```

예:

```bash
./proxy 15214
```

---

## 최종 채점 기준

최종 평가는 `driver.sh` 기준으로 해석한다. 문서의 예시 테스트는 수동 검증을 돕기 위한 보조 자료이며, 채점 기준 자체를 대체하지 않는다.

### Basic

다음 파일을 직접 내려받은 결과와 프록시 경유 결과가 바이트 단위로 같아야 한다.

- `home.html`
- `csapp.c`
- `tiny.c`
- `godzilla.jpg`
- `tiny`

### Concurrency

- 응답하지 않는 서버에 대한 요청이 걸려 있는 동안에도 Tiny에 대한 정상 요청이 시간 안에 완료되어야 한다.

### Cache

- Tiny 종료 이후에도 이미 내려받은 캐시 대상 파일을 프록시가 반환해야 한다.

---

## 수동 검증 시나리오

### Basic

```bash
cd tiny && ./tiny 15213 &
cd .. && ./proxy 15214 &

for file in home.html csapp.c tiny.c godzilla.jpg tiny; do
    curl --silent --proxy http://localhost:15214 \
         http://localhost:15213/$file > /tmp/proxy_$file
    curl --silent http://localhost:15213/$file > /tmp/direct_$file
    diff -q /tmp/proxy_$file /tmp/direct_$file && echo "$file OK"
done
```

### Concurrency

```bash
./tiny/tiny 15213 &
./proxy 15214 &
./nop-server.py 15215 &

curl --proxy http://localhost:15214 http://localhost:15215/blocked &
curl --max-time 5 --proxy http://localhost:15214 \
     http://localhost:15213/home.html
```

### Cache

```bash
./tiny/tiny 15213 &
./proxy 15214 &

curl --silent --proxy http://localhost:15214 \
     http://localhost:15213/home.html > /tmp/first

kill %1

curl --silent --proxy http://localhost:15214 \
     http://localhost:15213/home.html > /tmp/cached

diff /tmp/first /tmp/cached && echo "Cache HIT OK"
```

### Robustness

```bash
curl --proxy http://localhost:15214 http://nonexistent.server.local/x
curl --proxy http://localhost:15214 http://localhost:15213/home.html
```

---

## 권장 내부 설계 및 구현 가이드

이 절의 내용은 구현을 시작하기 위한 권장안이다. 함수 이름과 분해 방식은 `tiny/tiny.c`의 `main -> doit -> read_requesthdrs -> parse_uri -> clienterror` 흐름을 프록시 서버용으로 확장한 형태를 기준으로 제시한다.

### 권장 함수 분해

```c
void doit(int fd);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void read_requesthdrs(rio_t *rp, char *other_hdrs, char *host_hdr);
void build_http_header(char *http_header,
                       char *hostname,
                       char *path,
                       char *host_hdr,
                       char *other_hdrs);
void forward_response(int serverfd, int clientfd, char *cache_key);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
void *thread(void *vargp);
```

#### 설계 의도

- `doit(int fd)`는 `tiny.c`와 동일하게 연결 하나를 끝까지 처리하는 중심 함수로 둔다.
- `parse_uri()`는 Tiny의 `parse_uri()`처럼 URI 파싱 책임만 분리하되, 정적 파일 경로가 아니라 `hostname`, `port`, `path`를 추출하도록 확장한다.
- `read_requesthdrs()`는 Tiny처럼 요청 라인 다음 헤더를 모두 소비하되, 프록시에서 필요한 `Host`와 기타 헤더를 분리해 수집하도록 확장한다.
- `build_http_header()`는 원본 서버로 보낼 HTTP/1.0 요청 헤더를 조립한다.
- `forward_response()`는 원본 서버 응답을 클라이언트로 스트리밍하고, 캐시 가능하면 바이트를 축적해 저장한다.
- `clienterror()`는 Tiny와 같은 패턴으로 잘못된 메서드나 파싱 실패 시 최소한의 HTTP 오류 응답을 내려주는 용도로 둘 수 있다.
- `thread()`는 Part II 이후 `doit()` 호출과 fd 정리를 담당한다.

### 권장 자료구조

Tiny는 별도 구조체 없이 로컬 버퍼 조합으로 동작한다. 프록시는 헤더 재조합과 캐시 키 계산이 필요하므로, Tiny의 버퍼 중심 스타일을 유지하면서 필요한 상태만 묶는 확장 구조를 권장한다.

```c
typedef struct {
    char method[MAXLINE];
    char uri[MAXLINE];
    char version[MAXLINE];
    char hostname[MAXLINE];
    char port[16];
    char path[MAXLINE];
    char host_hdr[MAXLINE];
    char other_hdrs[MAXBUF];
} request_meta_t;

typedef struct cache_block {
    char cache_key[MAXLINE];
    char *data;
    size_t size;
    unsigned long stamp;
    struct cache_block *prev;
    struct cache_block *next;
} cache_block_t;

typedef struct {
    cache_block_t *head;
    cache_block_t *tail;
    size_t total_size;
    unsigned long next_stamp;
    pthread_rwlock_t lock;
} cache_list_t;
```

#### 자료구조 의도

- `request_meta_t`는 Tiny의 `method`, `uri`, `version`, `filename`, `cgiargs` 지역 버퍼 묶음을 프록시용으로 확장한 형태다.
- `cache_block_t`는 캐시 객체 하나의 키, 바이트 버퍼, 크기, 퇴출 순서를 담는다.
- `cache_list_t`는 전체 캐시 상태와 동기화 객체를 묶는다.

### Tiny 기준 확장 흐름

```text
main
  -> Accept loop
  -> Part I: doit(connfd)
  -> Part II: thread -> doit(connfd)

doit
  -> 요청 라인 읽기
  -> method / uri / version 파싱
  -> 필요 시 clienterror 반환
  -> parse_uri
  -> read_requesthdrs
  -> 캐시 조회
  -> 원본 서버 연결
  -> build_http_header
  -> 원본 서버에 요청 전달
  -> forward_response
```

### 요청 처리 권장 순서

```text
doit(fd):
  1. Rio_readinitb(&rio, fd)
  2. 요청 라인 1줄 읽기
  3. method, uri, version 파싱
  4. GET 이외 메서드는 clienterror 또는 조기 반환
  5. parse_uri(uri, hostname, port, path)
  6. read_requesthdrs(&rio, other_hdrs, host_hdr)
  7. cache_key 생성
  8. cache hit 이면 즉시 client로 응답
  9. 서버 연결
  10. build_http_header(...)
  11. 서버로 요청 쓰기
  12. forward_response(serverfd, fd, cache_key)
  13. fd 정리
```

### 응답 전달 권장 방식

- 응답 본문은 `Rio_readnb()` 기반으로 반복 전송한다.
- 클라이언트로는 읽는 즉시 전송한다.
- 캐시에 저장할 후보 데이터는 누적 길이가 `MAX_OBJECT_SIZE` 이하일 때만 별도 버퍼에 복사한다.
- 누적 길이가 한도를 넘으면 캐시 저장만 포기하고 클라이언트 전송은 계속한다.

### 원본 서버 연결 처리 권장안

필수 요구는 "서버 연결 실패가 프록시 프로세스 종료로 이어지지 않는 것"이다. 이를 위한 구현 방식은 아래 둘 중 하나를 권장한다.

1. `open_clientfd()`를 직접 사용하고 반환값을 호출자에서 처리한다.
2. `Open_clientfd()`와 유사한 형태의 비-종료 래퍼를 별도로 만든다.

`csapp.c`의 종료형 래퍼를 요청 처리 경로에서 그대로 호출하면 연결 하나의 실패가 프록시 전체 종료로 이어질 수 있다.

### 캐시 동기화 권장안

| 연산 | 권장 락 |
|------|---------|
| 조회 | `pthread_rwlock_rdlock` |
| 저장 | `pthread_rwlock_wrlock` |
| 퇴출 | `pthread_rwlock_wrlock` |

`pthread_mutex` 하나로 모든 읽기를 직렬화하는 방식은 Part III 목표와 맞지 않는다.

### 불변 조건

| ID | 조건 |
|----|------|
| INV-01 | 전체 캐시 크기는 항상 `MAX_CACHE_SIZE` 이하 |
| INV-02 | 저장되는 단일 응답은 항상 `MAX_OBJECT_SIZE` 이하 |
| INV-03 | 원본 서버로 보내는 요청 라인은 항상 `HTTP/1.0` |
| INV-04 | 원본 서버로 보내는 요청 라인은 path만 포함 |
| INV-05 | `thread()`는 자신이 받은 `connfd` 복사본을 사용하고 종료 전에 정리 |
| INV-06 | 각 락은 모든 반환 경로에서 해제 |
| INV-07 | `SIGPIPE`는 프로그램 시작 초기에 무시 |

### 구현 단계 권장 순서

1. `main()`과 `doit()`로 순차 프록시 골격을 만든다.
2. `parse_uri()`, `read_requesthdrs()`, `build_http_header()`를 완성해 Basic을 맞춘다.
3. `forward_response()`를 바이너리 안전하게 만든다.
4. 종료형 `csapp` 래퍼를 요청 경로에서 제거하거나 우회해 견고성을 맞춘다.
5. `thread()`를 추가해 Concurrency를 맞춘다.
6. 캐시 구조와 조회/삽입을 추가해 Cache를 맞춘다.

---

## Active Unknowns / Deferred Decisions

아래 항목은 구현에 영향을 줄 수 있지만, 과제 통과를 위해 한 가지 정답만 강제할 필요는 없다.

- 캐시 키를 단순 문자열 비교로 둘지, host 대소문자 정규화까지 할지
- 캐시 코드를 `proxy.c` 안에 둘지, 별도 `cache.c/.h`로 분리할지
- LRU 근사를 삽입 순서 기반으로 둘지, 조회 시 timestamp 갱신까지 넣을지
- 잘못된 요청에 대해 조용히 연결을 닫을지, `clienterror()`로 HTTP 오류 응답을 줄지

---

## 참고 파일

- [README.md](./README.md)
- [proxy.c](./proxy.c)
- [csapp.c](./csapp.c)
- [tiny/tiny.c](./tiny/tiny.c)
- [driver.sh](./driver.sh)
