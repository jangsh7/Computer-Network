# 🌐 Computer Network

컴퓨터네트워크 과목에서 진행한 과제를 모아둔 저장소입니다.  
신뢰성 있는 데이터 전송을 위한 슬라이딩 윈도우 프로토콜을 C로 직접 구현하고 성능을 비교했습니다.

<br>

## 🗂️ Projects

|  #  | 프로젝트                      |               핵심 내용                |
| :-: | :---------------------------- | :------------------------------------: |
|  1  | Go-Back-N vs Selective Repeat | ARQ, 슬라이딩 윈도우, 재전송 전략 비교 |

<br>

## 📝 Project Summary

### 1️⃣ Go-Back-N vs Selective Repeat

패킷 손실·손상·지연이 일어나는 네트워크 환경에서 데이터를 안정적으로 전달하기 위한 두 가지 ARQ 프로토콜을 구현하고, 같은 조건에서 성능을 비교했습니다.

**공통 설계**

- **이벤트 기반 시뮬레이터** 위에서 송신 측(A)과 수신 측(B)의 프로토콜 동작 구현
- **Pipelining** : 윈도우 크기 8로 ACK를 기다리지 않고 여러 패킷을 연속 전송
- **오류 검출** : 체크섬으로 손상된 패킷과 ACK를 판별
- **실험 조건** : 패킷 손실률 20%, 오류율 10%, 메시지 50개

**Go-Back-N** (`gbn.c`)

- **단일 타이머** : 윈도우의 가장 오래된 패킷(base)에만 타이머 설정
- **누적 ACK** : ACK를 받으면 base를 앞으로 이동시켜 윈도우 슬라이드
- **일괄 재전송** : 타임아웃 시 base부터 아직 ACK되지 않은 모든 패킷을 다시 전송
- **순차 수신** : 수신 측은 순서가 맞는 패킷만 받고 나머지는 버림

**Selective Repeat** (`sr.c`)

- **패킷별 개별 타이머** : 시퀀스 번호마다 독립적으로 타임아웃 관리
- **선택적 재전송** : 손실·손상된 패킷만 골라서 다시 전송
- **수신 버퍼링** : 순서가 어긋난 패킷도 버퍼에 저장한 뒤 순서대로 상위 계층에 전달
- **시퀀스 번호 공간** : 윈도우 크기의 2배(16)로 설정해 송·수신 윈도우 혼동 방지

<br>

## 📊 Results

| 항목             |   Go-Back-N    |  Selective Repeat  |
| :--------------- | :------------: | :----------------: |
| 정상 수신 메시지 |      7개       |      **12개**      |
| 재전송 패킷 수   |    약 28개     |      **18개**      |
| 재전송율         |     약 56%     |     **약 36%**     |
| Throughput       | 0.53 bytes/sec | **0.90 bytes/sec** |

GBN은 패킷 하나만 손실돼도 윈도우 전체를 다시 보내고 순서가 어긋난 패킷을 버리기 때문에 재전송이 크게 늘었습니다.  
SR은 손실된 패킷만 다시 보내고 순서가 어긋난 패킷을 버퍼에 보관해, **재전송은 줄이고 Throughput은 약 1.7배 높였습니다.**

<br>

## 🚀 How to Run

```bash
gcc gbn.c -o gbn && ./gbn
gcc sr.c -o sr && ./sr
```

손실률, 오류율, 메시지 수는 각 파일 상단의 `LOSS_PROB`, `CORRUPT_PROB`, `SIM_MESSAGE_MAX`로 바꿀 수 있습니다.

<br>

## 🛠️ Tech Stack

![C](https://img.shields.io/badge/C-A8B9CC?style=flat-square&logo=c&logoColor=black)
![Visual Studio](https://img.shields.io/badge/Visual_Studio-5C2D91?style=flat-square&logo=visualstudio&logoColor=white)

<br>

## 📁 Repository Structure

```
Computer-Network/
    ├── gbn.c
    ├── sr.c
    └── report.pdf
```

<br>

## ⚠️ Note

이벤트 기반 네트워크 시뮬레이터 부분(`tolayer3`, `starttimer`, `jimsrand` 등)은 Kurose & Ross 교재 _Computer Networking: A Top-Down Approach_ 의 프로그래밍 과제 코드를 바탕으로 합니다.  
GBN·SR 프로토콜 로직(`A_output`, `A_input`, `A_timerinterrupt`, `B_input` 등)은 직접 구현했습니다.

<br>

## ©️ Copyright

Copyright © 2025 장서현. All rights reserved.

이 저장소의 코드와 보고서는 학습 및 포트폴리오 목적으로 공개합니다.  
**무단 복제 및 과제 제출 용도로의 사용을 금지합니다.**
