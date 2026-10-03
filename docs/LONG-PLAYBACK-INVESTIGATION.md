# HQ2 장시간 재생 오류 조사 기록

> **해결됨 (2026-10-03).** 원인은 DSWiFi ARM7 `Wifi_Update()` 재진입이었다(아래 표 13번,
> [HQ2-CHANGELOG.md](HQ2-CHANGELOG.md) 9차 변경, 상류 제보 [blocksds/sdk#401](https://codeberg.org/blocksds/sdk/issues/401)).
> 이 문서는 당시 인수인계용으로 쓴 조사 과정 기록이다. 작업 방법의 최신판은 [DEVELOPMENT.md](DEVELOPMENT.md).

## 1. 개요

- **증상**: YouTube DSi HQ2(`client-hq2/`)로 영상을 재생하면 수 초~6분 사이에 멈춘다. 멈추는 모습은 매번 다르다.
  - 빨간 화면(ARM9 Data abort) + 소리 반복
  - 화면 그대로 굳음(소리 반복 또는 무음)
  - 최근 빌드: 네트워크만 죽음(Buffering → "Stream interrupted" / "Can't reach the server"). 이때 PC에서 DSi로 ping도 안 됨.
- **에뮬레이터(melonDS)에서는 한 번도 재현되지 않는다.** 실기 전용 문제다.
- **구성**
  - DSi: TWiLight에서 DSi 모드로 `roms/tools/YouTubeDSiHQ2.nds` 실행. Wi-Fi는 DS 호환 모드(`WIFI_DS_MODE_ONLY`, NTR 드라이버).
  - 서버: 갤럭시 S8(Termux, `192.168.0.8:8767`)의 `server/server_quality.py`. 부팅하면 Termux:Boot로 자동 시작.
  - 툴체인: WSL Ubuntu-22.04, BlocksDS v1.24.0(`/opt/wonderful/...`). DSWiFi는 미리 빌드된 라이브러리.

## 2. 작업 방법 (자주 쓰는 명령)

- **빌드**: `wsl -d Ubuntu-22.04 -- sh /mnt/c/Users/hanbi/Desktop/NDSi/youtube-dsi/tools/build_hq2.sh`
  → 결과물 `client-hq2/YouTubeDSiHQ2.nds`. `sd-root/`와 `../SD-copy/`에 복사해 둔다.
- **DSi에 올리기**: DSi에서 `roms/tools/ftpd.nds`를 켠 뒤 FTP `192.168.0.24:5000`(익명)으로
  `/roms/tools/YouTubeDSiHQ2.nds`를 교체한다. `.new`로 올리고, 내려받아 비교한 다음 이름을 바꾼다.
- **S8 서버**: USB adb(`C:\Users\hanbi\AppData\Local\Android\Sdk\platform-tools\adb.exe`),
  `run-as com.termux`로 `~/youtube-dsi/server/`에 접근한다. Git Bash에서는 `MSYS_NO_PATHCONV=1`이 필요하다.
  - 서버 파일 교체: `/data/local/tmp`에 push → `run-as com.termux cp`
  - 재시작: `tools/termux/stop.sh` 실행 후
    `am broadcast -a android.intent.action.BOOT_COMPLETED -n com.termux.boot/.BootReceiver`
  - 기록: `relay.log`, 그리고 `~/youtube-dsi/dumps/*.yds`(보낸 스트림), `*.tsv`(패킷별 기록 + DSi 블랙박스 줄)
    - 덤프는 `relay.env`의 `YTDSI_DUMP`로 켠다. 지금 켜져 있다.
  - **재생 중에 서버를 재시작하면 DSi 스트림이 끊긴다.** 재시작 전에 사용자에게 알릴 것.
- **에뮬레이터 시험**: `dsi-dashboard/verification/emutest/emutest`(SD 이미지·`lid`·`capture` 명령 지원). PC에서 시험용 릴레이를 띄운다.
  - 시험 스크립트는 세션 scratchpad에 있었다(`seek_relay.py`, `stall_relay.py`, `replay_relay.py`, `hq2lid/*_test.py`). 새 세션에서는 다시 만들어야 한다.

## 3. 지금까지 해본 것과 결과

| # | 가설 / 조치 | 결과 |
|---|---|---|
| 1 | 수신 스레드 스택 부족 → 24KB에서 64KB(정적)로 | 실기 측정 최대 사용량은 **1KB**. 원인 아님 |
| 2 | 버퍼가 가득 차서 TCP 창이 닫힘 → 서버가 DSi 버퍼를 4.5초로 유지(`Pacer`) | 버퍼가 낮게 유지된 상태에서도 멈춤. 원인 아님(유지 중, 해롭지 않음) |
| 3 | 영상 데이터 자체(JPEG 디코딩) | 실기에서 터진 스트림을 녹화해 에뮬레이터에 똑같이 넣었지만 재현 안 됨. 원인 아님 |
| 4 | DSWiFi tcpip 스레드 스택 4KB 초과 → `--wrap=cothread_create`로 모든 스레드 32KB | 실기 측정: tcpip 236B, 업데이트 스레드 약 1KB. 원인 아님(유지 중) |
| 5 | 배터리 확인(`getBatteryLevel`)이 ARM7 응답을 막고 기다림 → ARM7이 볼륨·배터리를 메인 루프에서 캐시, ARM9은 기다리지 않음 | 그 지점에서 굳는 일은 사라짐. 근본 원인은 아님 |
| 6 | 생존 신호를 IPC_SYNC 레지스터로 보냄 | **실기에서 1초 만에 굳음**(로더가 IPC_SYNC를 감시하는 것으로 추정). 공유 메모리 방식으로 바꿈 |
| 7 | 깨진 컨텍스트 주소 추적 | 깨진 곳은 늘 **DSWiFi 업데이트 스레드 컨텍스트**(`0x028291A0` / `0x028331A0`). 내용이 전부 0 |
| 8 | Wi-Fi 공유 구조체(`WifiData`, `0x02820F40`) 뒤에 8KB 감시 영역(`--wrap=aligned_alloc`) | **감시 영역 2048워드 전부 0으로 덮임**, 그 뒤 스레드 스택들도. 72KB 넘게 한 번에 0으로 덮임 |
| 9 | ARM7 `Wifi_MACRead()`가 길이를 검사 없이 DMA 개수로 씀. 길이 0이면 DMA 개수 0 = 65536 하프워드(128KB) → ARM7에서 `--wrap=Wifi_MACRead`로 0·음수·2400 초과 길이 거부, 거부 횟수를 블랙박스 `mr`로 보냄 | 이후 **빨간 화면은 안 나옴.** 다만 실기 기록에서 `mr`은 계속 0이라 이 경로였다는 증거는 아직 없음 |
| 10 | DSWiFi `select()` 타임아웃 버그: 시간 제한 대기가 신호가 와야만 시간을 확인함 → 데이터가 없으면 영원히 대기 | 에뮬레이터에서 재현(네트워크가 멈춘 뒤 B를 누르면 굳음). `select` 대신 `cothread_yield_irq(IRQ_RECV_FIFO)`로 바꿔 해결. 15초 무수신이면 오류로 종료 |
| 11 | 자동 복구: 8초 무수신이면 같은 위치로 다시 연결(최대 5회), 연결 대기 8초 제한 | 에뮬레이터에서 동작 확인. **실기에서는 DSi 네트워크 전체가 죽어 있어서 "Can't reach the server"** |
| 12 | 서버 쪽 부수 수정: 영상 이동(`PLAY4`) 1~2초로 단축, HTTPS 형식 우선, 한국어 제목(`YTDSI_LANG`), 재생 이어받기 | 동작 확인 |
| 13 | **근본 원인**: ARM7 FIFO 처리기가 `REG_IME=1` 상태로 `Wifi_Update()`를 실행 → VBlank·Wi-Fi 인터럽트가 끼어들어 `Wifi_Update` 중첩 → 송신 크기 0을 읽고 `Wifi_MACWrite(0)`(128KB DMA로 MAC RAM 전체 덮어씀) + 송신 읽기 위치 4바이트 어긋남 → ARM9 송신 영구 중단. `--wrap=Wifi_Update`(인터럽트 끈 채 실행), `--wrap=Wifi_MACWrite`(길이 0 거부) | **실기 12분 넘게 끊김 없음**(같은 부하에서 예전엔 109초·203초에 끊김). `mr`·`mw`·`resets` 모두 0 |

### 실기 블랙박스로 확인한 사실 (최근)

- 네트워크가 죽기 직전까지 모두 정상이었다: ARM7 생존 신호 증가, FIFO 레지스터 `8501`, 감시 영역 `cn 0`, `mr 0`, Wi-Fi 상태 `5`(Associated), rx/tx 패킷 수 증가.
- 그러다 **갑자기 양방향 통신이 끊긴다.** 서버 쪽에서는 DSi가 ACK를 하지 않고(재전송만 반복), ping도 실패한다. 이후 새 TCP 연결도 안 된다.
- 마지막 실기 사례(커밋 `3d571c1`): 60fps 영상을 재생한 지 109초에 네트워크가 죽었다(버퍼 1~2초로 낮은 상태). 자동 복구가 "Can't reach the server"로 실패했다.

## 4. 현재 상태

- **해결 (2026-10-03, 표의 13번)**: 근본 원인은 ARM7에서 `Wifi_Update()`가 겹쳐 실행되는 것이었다. 자세한 설명은 [HQ2-CHANGELOG.md](HQ2-CHANGELOG.md) 9차 변경.
  - DSi에는 이 수정이 들어간 빌드(md5 `f1490512…`)가 올라가 있다. 이전 ROM은 `device-backups/hq2/`에 있다.
  - 실기 시험: `dumps/20261003-205841`에서 약 98KiB/s로 12분 넘게 재생했고 `mr`·`mw`·`resets`·`rej`·`lost`가 모두 0이었다.
- `eac9e94`의 자동 복구(같은 위치로 다시 연결, Wi-Fi 재접속)는 안전장치로 그대로 둔다. 실기에서 재접속 경로가 실행된 적은 아직 없다.
- S8 서버는 최신 상태다(`server_quality.py`, `server.py` 한국어 설정). 덤프 기록이 켜져 있다.
- 진단 장치(2026-10-04 11차 변경에서 정리함. 다시 필요하면 커밋 `11774f8` 이전 코드를 참고)
  - 오류 화면: stdio·힙을 쓰지 않음. r0/r4 주변 메모리, 감시 영역, 스레드 스택 최대 사용량 표시
  - 워치독: VBlank 인터럽트에서 동작. 메인/수신 단계, ARM7 생존, FIFO 응답 표시
  - 블랙박스: 매초 `DBG ...` 줄을 서버로 보내고, 서버가 `.tsv`의 `dsi_debug` 칸에 기록
  - 화면 경고: 5초 무수신이면 Wi-Fi 상태와 통계를 빨간 글씨로 표시

## 5. 다음에 할 일

1. 몇 번 더 장시간 재생(60fps 영상 포함)해서 확인한다. 예전에는 길어도 6분 안에 끊겼다.
   - 다시 끊기면 화면·오류 메시지의 `dma` 값을 본다. 0보다 크면 다른 경합 경로가 남아 있다는 뜻이다.
2. ~~진단 코드 정리~~ 완료(11차 변경). 서버 덤프 `YTDSI_DUMP`는 선택 기능으로 남겨 두고 S8에서는 꺼 두었다.
   - DSWiFi 우회 수정(`--wrap=Wifi_Update`, `Wifi_MACRead`/`Wifi_MACWrite` 길이 검사)은 남긴다.
   - 32KB 스레드 스택(`--wrap=cothread_create`)과 서버 `Pacer`는 해롭지 않으니 남겨도 된다.
3. DSWiFi 상류(BlocksDS)에 알릴 만한 버그다: ARM7 FIFO 처리기에서 `Wifi_Update()` 재진입,
   `Wifi_TxArm9QueueFlush()`가 임계 구역 밖에서 크기를 읽음, `Wifi_MACRead/Write`가 길이 0을 DMA 개수 0(=65536)으로 넘김.
4. ~~[TODO.md](TODO.md)의 검색 기능~~ 완료(10차 변경).

## 6. 관련 파일

- `client-hq2/source/hq_player.c`: 재생 루프, 수신 스레드, 예외 화면
- `client-hq2/source/net_stacks.c`: `cothread_create`/`aligned_alloc` 래퍼(32KB 스택, 감시 영역)
- `client-hq2/source/main.c`: 연결, 자동 복구, Wi-Fi 재접속
- `tools/prepare_hq2_arm7.py`: HQ2 전용 ARM7 생성(볼륨·배터리 캐시, 생존 신호, `Wifi_MACRead` 길이 검사)
- `tools/build_hq2.sh`: ARM7(`--wrap=Wifi_MACRead`)과 ARM9 빌드
- `server/server_quality.py`: `PLAY4`, `Pacer`, 이어받기, 덤프·블랙박스 기록
- [HQ2-CHANGELOG.md](HQ2-CHANGELOG.md): 변경 이력
