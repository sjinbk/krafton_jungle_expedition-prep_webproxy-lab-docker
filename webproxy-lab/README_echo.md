# Echo 서버/클라이언트 개발 명세서

## 프로젝트 개요

TCP 소켓 기반의 에코(Echo) 서버/클라이언트 시스템을 구현한다. 클라이언트가 전송한 메시지를 서버가 그대로 되돌려 보내는 것을 핵심 동작으로 한다. 본 명세서는 단일 클라이언트 처리 기준의 iterative 서버를 대상으로 한다.

---

## 기술 스택

| 항목 | 요구사항 |
|------|----------|
| 언어 | C |
| 소켓 API | POSIX BSD Socket |
| 헬퍼 라이브러리 | csapp.h / csapp.c (CSAPP 교재 제공) |
| 빌드 도구 | Makefile |
| 실행 환경 | Linux (Ubuntu 20.04 이상) |

---

## 핵심 개념 및 용어 정의

| 용어 | 정의 |
|------|------|
| **에코(Echo)** | 수신한 데이터를 송신자에게 그대로 반사하는 동작. 네트워크 연결 검증 용도로 관용적으로 사용됨 |
| **Iterative 서버** | 클라이언트를 하나씩 순차 처리하는 서버. concurrent 서버와 대비되며, 본 구현의 범위 |
| **rio_t** | CSAPP의 Robust I/O 버퍼 타입. 커널 I/O의 short count 문제를 래핑하여 해결 |
| **listenfd** | 연결 요청 수신 전용 소켓 파일 디스크립터 |
| **connfd** | 실제 데이터 송수신에 사용되는 연결 소켓 파일 디스크립터 |
| **MAXLINE** | csapp.h에 정의된 최대 라인 버퍼 크기 (8192 bytes) |
| **stub** | 아직 구현하지 않은 함수를 컴파일·실행은 가능하되 단순화된 형태로 임시 작성한 것. 이후 실제 구현으로 교체할 것을 알고 의도적으로 단순화한다 |
| **불변 조건(invariant)** | 프로그램 실행 중 항상 참이어야 하는 구조적 성질. 검증 함수를 두어 명시적으로 관리한다 |

---

## 기능 요구사항

### 서버 (echoserver)

| ID | 요구사항 | 비고 |
|----|----------|------|
| S-01 | 지정된 포트에서 TCP 연결 요청을 수신한다 | 포트는 argv[1]로 전달 |
| S-02 | 클라이언트 연결 수립 시 hostname과 port를 stdout에 출력한다 | `Getnameinfo` 사용 |
| S-03 | 연결된 클라이언트로부터 라인 단위로 데이터를 수신한다 | `Rio_readlineb` 사용 |
| S-04 | 수신한 데이터를 수신 즉시 동일 클라이언트에게 그대로 송신한다 | `Rio_writen` 사용 |
| S-05 | 수신한 바이트 수를 stdout에 출력한다 | `"server received %d bytes\n"` |
| S-06 | 클라이언트가 연결을 종료하면 connfd를 닫고 다음 연결을 대기한다 | EOF 수신 시 루프 탈출 |
| S-07 | 포트 인자 없이 실행 시 usage 메시지를 출력하고 종료한다 | argc != 2 |

### 클라이언트 (echoclient)

| ID | 요구사항 | 비고 |
|----|----------|------|
| C-01 | 지정된 host, port로 TCP 연결을 수립한다 | argv[1]: host, argv[2]: port |
| C-02 | stdin에서 라인 단위로 입력을 읽어 서버로 송신한다 | `Fgets` + `Rio_writen` |
| C-03 | 서버로부터 에코 응답을 수신하여 stdout에 출력한다 | `Rio_readlineb` + `Fputs` |
| C-04 | stdin이 EOF에 도달하면 연결을 닫고 종료한다 | `Fgets` 반환값 NULL 체크 |
| C-05 | host/port 인자 없이 실행 시 usage 메시지를 출력하고 종료한다 | argc != 3 |

---

## 비기능 요구사항

| ID | 요구사항 |
|----|----------|
| NF-01 | 모든 소켓 함수는 csapp 래퍼 함수를 사용한다 |
| NF-02 | 시스템 콜 에러 발생 시 프로세스를 즉시 종료한다 (csapp 래퍼 기본 동작) |
| NF-03 | connfd는 echo() 처리 완료 후 반드시 `Close(connfd)`로 닫는다 |
| NF-04 | 서버는 listenfd를 명시적으로 닫지 않는다 (무한 루프 구조 전제) |

---

## 인터페이스 명세

### 실행 인터페이스

```
# 서버
./echoserver <port>
예: ./echoserver 8080

# 클라이언트
./echoclient <host> <port>
예: ./echoclient localhost 8080
```

### 함수 계약 명세

함수를 구현하기 전, 각 함수의 계약(contract)을 먼저 확정한다. 구현은 이 계약을 만족해야 하며, 호출자는 내부 구현을 보지 않고 계약만으로 사용할 수 있어야 한다.

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

#### `main()` — echoclient

```
입력 조건:
  - argc == 3, argv[1]은 host, argv[2]는 포트 번호 문자열이다

출력 조건:
  - stdin EOF 도달 시 서버 연결을 닫고 프로세스가 종료된다

부수 효과:
  - stdin 입력 라인을 서버로 전송하고, 수신한 에코를 stdout으로 출력한다
```

### I/O 흐름

```
[클라이언트 stdin]
      │  Fgets()
      ▼
   buf[MAXLINE]
      │  Rio_writen(clientfd, buf, strlen(buf))
      ▼
  [TCP 소켓]  ────────────────────────────────►  [서버 수신]
                                                  Rio_readlineb()
                                                       │
                                              printf("server received %d bytes")
                                                       │
                                                  Rio_writen()
                                                       │
  [TCP 소켓]  ◄────────────────────────────────  [서버 송신]
      │
  Rio_readlineb(rio, buf, MAXLINE)
      │
  Fputs(buf, stdout)
      ▼
[클라이언트 stdout]
```

---

## 데이터 흐름 및 상태 정의

### 서버 상태 전이

```
[START]
  │
  ▼
Open_listenfd(port)        ← listenfd 생성
  │
  ▼
while(1):
  Accept()                 ← connfd 생성, 블로킹 대기
  │
  ▼
  echo(connfd)             ← 데이터 송수신 반복 (EOF까지)
  │
  ▼
  Close(connfd)            ← 연결 종료
  │
  └──────────────────────► (다시 Accept 대기)
```

### 종료 조건

| 주체 | 종료 트리거 | 처리 |
|------|-------------|------|
| 서버 | 해당 없음 | 무한 루프, 명시적 종료 없음 |
| echo() | `Rio_readlineb` 반환값 0 (EOF) | while 루프 탈출 |
| 클라이언트 | `Fgets` 반환값 NULL (stdin EOF) | while 루프 탈출 후 `Close` |

---

## 구현 단계 분할 (수직 슬라이스)

각 슬라이스는 "외부에서 관찰할 수 있는 동작" 단위로 정의한다. 슬라이스 하나가 완성되면 검증 후 다음 슬라이스로 진행한다. 이전 슬라이스의 검증 항목이 다음 슬라이스에서도 통과해야 한다.

### 슬라이스 1: 서버 기동 및 연결 수립

**구현 대상:** `main()` (서버) — `Open_listenfd`, `Accept`, `Close` 흐름

**stub 처리:** `echo(connfd)` 를 아래와 같이 임시 대체
```c
void echo(int connfd) {
    // stub: 연결만 수락하고 아무것도 하지 않는다
    (void)connfd;
}
```

**검증 항목:**
- 서버가 지정 포트에서 기동되는가?
- 클라이언트가 연결하면 hostname/port 출력이 나타나는가?
- 클라이언트 종료 후 서버가 다음 연결을 대기하는가?

```bash
# 검증 방법
./echoserver 8080 &
telnet localhost 8080   # 연결 후 Ctrl+] → quit
# 서버 stdout에 "Connected to (localhost, xxxxx)" 출력 확인
```

---

### 슬라이스 2: 에코 송수신

**구현 대상:** `echo()` 실제 구현 — `Rio_readinitb`, `Rio_readlineb`, `Rio_writen`

**stub 해제:** 슬라이스 1의 stub을 실제 `echo()` 구현으로 교체

**검증 항목:**
- 클라이언트가 보낸 문자열이 그대로 돌아오는가?
- 서버 stdout에 수신 바이트 수가 출력되는가?
- 다중 라인 입력 시 순서가 유지되는가?

```bash
# 검증 방법
echo "hello" | ./echoclient localhost 8080
# stdout에 "hello" 출력 확인
printf "line1\nline2\nline3\n" | ./echoclient localhost 8080
# 3줄이 순서대로 출력 확인
```

---

### 슬라이스 3: 연속 클라이언트 처리

**구현 대상:** 추가 구현 없음 — 슬라이스 1~2의 조합 검증

**검증 항목:**
- 클라이언트 A 종료 후 서버 재기동 없이 클라이언트 B가 정상 접속되는가?
- 각 연결마다 독립적인 에코가 동작하는가?

```bash
# 검증 방법
echo "first" | ./echoclient localhost 8080
echo "second" | ./echoclient localhost 8080
# 두 번 모두 정상 에코 확인
```

---

### 슬라이스 4: 에러 처리 및 인자 검증

**구현 대상:** argc 검사, usage 메시지 출력

**검증 항목:**
- 인자 없이 실행 시 usage 메시지가 stderr에 출력되는가?
- exit(0)으로 종료되는가?

```bash
# 검증 방법
./echoserver           # "usage: ./echoserver <port>" 출력 확인
./echoclient localhost  # "usage: ./echoclient <host> <port>" 출력 확인
```

---

## 불변 조건 (Invariant)

아래 조건은 프로그램 실행 중 항상 참이어야 한다. 디버깅 시 이 조건이 깨지는 지점을 먼저 특정한다.

| ID | 조건 |
|----|------|
| INV-01 | echo() 실행 중 connfd는 유효한 열린 소켓이다 |
| INV-02 | echo() 반환 후 main에서 반드시 Close(connfd)가 호출된다 |
| INV-03 | Rio_readlineb가 반환하는 문자열은 항상 `\n`으로 끝나거나 EOF(n==0)다 |
| INV-04 | Rio_writen에 전달하는 길이는 buf에 실제 기록된 바이트 수와 일치한다 |

디버깅 보조 함수 예시:

```c
// 디버깅 빌드에서만 활성화. 실제 배포 시 제거 또는 #ifdef로 비활성화
static void assert_echo_invariants(int connfd, const char *buf, size_t n) {
    assert(connfd >= 0);                    // INV-01
    assert(n == 0 || buf[n-1] == '\n');     // INV-03
    assert(n <= MAXLINE);                   // 버퍼 오버플로 방지
}
```

---

## 제약 및 명시적 설계 결정

### 범위 내 (구현 대상)

- 단일 클라이언트 순차 처리 (iterative)
- 라인 단위 (`\n` 포함) 송수신
- IPv4 / IPv6 모두 지원 (`sockaddr_storage` 사용)

### 범위 외 (미구현, 이유 명시)

| 항목 | 결정 | 근거 |
|------|------|------|
| 동시 클라이언트 처리 | 미구현 | iterative 서버 학습 목적. 동시성 문제 자체가 발생하지 않는 구조 — 별도로 동시성 제어가 필요 없음을 명시 |
| 부분 수신(partial read) 처리 | csapp에 위임 | `Rio_readlineb` 내부 루프가 처리 |
| 최대 연결 대기 큐 | 기본값 사용 | `Open_listenfd` 내부에서 `LISTENQ` 상수로 설정 |
| 타임아웃 | 미구현 | 학습 범위 외 |

---

## 빌드 명세

```makefile
CC = gcc
CFLAGS = -Wall -O2
LIBS = -lpthread

all: echoserver echoclient

echoserver: echoserver.c csapp.c
	$(CC) $(CFLAGS) -o echoserver echoserver.c csapp.c $(LIBS)

echoclient: echoclient.c csapp.c
	$(CC) $(CFLAGS) -o echoclient echoclient.c csapp.c $(LIBS)

clean:
	rm -f echoserver echoclient
```

**의존 파일:** `csapp.h`, `csapp.c` — CSAPP 교재 공식 배포본 사용

---

## 테스트 시나리오

| 슬라이스 | 시나리오 | 입력 | 기대 결과 |
|----------|----------|------|-----------|
| 1 | 서버 기동 | `./echoserver 8080` | 포트 바인딩 성공, Accept 대기 |
| 1 | 연결 수립 | telnet으로 접속 | `Connected to (...)` 출력 |
| 2 | 정상 에코 | `hello\n` 전송 | 클라이언트 stdout에 `hello\n` 출력 |
| 2 | 다중 라인 | `line1\nline2\n` 순차 전송 | 각 라인 순서대로 에코 |
| 2 | 바이트 수 출력 | `hello\n` (6바이트) 전송 | 서버 stdout에 `server received 6 bytes` |
| 3 | 연속 클라이언트 | A 종료 후 B 접속 | 서버 재기동 없이 B 정상 처리 |
| 4 | 인자 오류 (서버) | `./echoserver` | `usage: ./echoserver <port>` 후 종료 |
| 4 | 인자 오류 (클라이언트) | `./echoclient localhost` | `usage: ./echoclient <host> <port>` 후 종료 |
| 4 | 클라이언트 정상 종료 | Ctrl+D (stdin EOF) | 클라이언트 종료, 서버는 다음 연결 대기 |

---

## 추가 고려사항 (문서화 대상)

- **Concurrent 서버 확장:** `fork()` 기반 process-per-client 또는 I/O multiplexing(`select`/`epoll`)으로 확장 가능. 이 경우 connfd 소유권 이전 등 계약이 재정의되어야 함
- **SIGPIPE 처리:** 클라이언트 비정상 종료 시 `write()` 호출이 SIGPIPE를 발생시킬 수 있음
- **포트 재사용:** 서버 재기동 시 `SO_REUSEADDR` 옵션 필요 (csapp의 `Open_listenfd`에서 이미 처리)
- **버퍼 크기 한계:** MAXLINE(8192) 초과 메시지는 분할 수신됨. 프로토콜 레벨 메시지 경계 정의 필요