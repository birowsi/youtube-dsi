# 개발·운영 안내

HQ2를 빌드하고, DSi와 서버에 올리고, 시험하고, 릴리스하는 방법입니다.

## 구성

| 경로 | 내용 |
|---|---|
| `client-hq2/` | HQ2 ROM(ARM9) 소스. `source/main.c`(메뉴·연결·자동 복구), `hq_player.c`(재생), `ui.c`(밑 화면), `net_stacks.c`(스레드 스택) |
| `compat-runtime/` | 사설 CRT·링커 설정과 ARM7 코어. HQ2용 ARM7은 `tools/prepare_hq2_arm7.py`가 `main7-hq2.c`로 만든다 |
| `server/` | `server_quality.py`(HQ/HQ2 서버, 8767), `server.py`(Compat 서버와 공용 검색·yt-dlp 부분) |
| `tools/` | 빌드·배포·시험 스크립트. `termux/`는 안드로이드 폰 서버용 |
| `sd-root/` | SD 카드에 바로 복사할 빌드 결과 |
| `client/` | 예전 HQ·Compat ROM 소스(수정하지 않음) |

## 빌드

WSL Ubuntu-22.04, BlocksDS v1.24.0(`/opt/wonderful/...`)을 씁니다.

```bash
wsl -d Ubuntu-22.04 -- sh /mnt/c/Users/hanbi/Desktop/NDSi/youtube-dsi/tools/build_hq2.sh
```

- 결과는 `client-hq2/YouTubeDSiHQ2.nds`에 나옵니다. 이것을 `sd-root/`와 `../SD-copy/`에 복사해 둡니다.
- ARM7 링크에 DSWiFi 우회 수정이 들어갑니다(`--wrap=Wifi_Update`, `Wifi_MACRead`, `Wifi_MACWrite`). 빌드 뒤 `compat-runtime/arm7-hq2.map`에 `__wrap_Wifi_*` 세 개가 있는지 확인하면 됩니다.
- ARM9 링크의 `--wrap=cothread_create`는 DSWiFi 스레드에 32KB 스택을 줍니다.

## DSi에 올리기 (FTP)

DSi에서 `roms/tools/ftpd.nds`를 켜면 `192.168.0.24:5000`(익명)으로 접속할 수 있습니다. 배터리가 나가도 ROM이 하나는 남도록 이 순서로 올립니다.
1. 이전 ROM을 PC의 `device-backups/hq2/`에 백업합니다.
2. 새 ROM을 `YouTubeDSiHQ2.nds.new`로 올리고, 내려받아 비교합니다.
3. 기존 파일은 `.old`로 이름만 바꿉니다. `.new`를 정식 이름으로 바꾸고 다시 내려받아 비교합니다.
4. 모두 맞으면 `.old`를 지웁니다.

시험하는 동안 DSi는 충전기에 꽂아 두세요.

## S8(Termux) 서버 운영

- **접속**: USB adb(`%LOCALAPPDATA%\Android\Sdk\platform-tools\adb.exe`)로 `run-as com.termux`를 써서 `/data/data/com.termux/files/home/youtube-dsi/`에 접근합니다. Git Bash에서는 `MSYS_NO_PATHCONV=1`이 필요합니다.
- **파일 교체**: `/data/local/tmp`에 push한 뒤 `run-as com.termux cp`로 복사합니다. md5로 같은지 확인합니다.
- **재시작**: `tools/termux/stop.sh`를 실행하고 `am broadcast -a android.intent.action.BOOT_COMPLETED -n com.termux.boot/.BootReceiver`를 보냅니다.
  - **재생 중에 재시작하면 DSi 재생이 끊깁니다.** `relay.log`로 재생 중이 아닌지 먼저 확인하세요.
- **기록**: `server/relay.log`에 남습니다. 스트림 녹화는 `server/relay.env`에 `export YTDSI_DUMP=<폴더>`를 넣으면 켜집니다(평소에는 꺼 둡니다).

## 에뮬레이터 시험

- **실행**: `dsi-dashboard/verification/emutest/emutest ROM 출력폴더 프레임수 dsi manual direct`로 headless melonDS를 띄웁니다. WSL에서 실행하고, S8 서버(192.168.0.8)에 직접 접속됩니다.
- **조작**: 출력 폴더의 `control.txt`에 명령을 써서 합니다.
  - `key <비트>` / `touch x y` / `release` / `capture 이름` / `lid 0|1` / `quit`
  - 키 비트: A 1, B 2, SELECT 4, START 8, 오른쪽 16, 왼쪽 32, 위 64, 아래 128, R 256, L 512, X 1024, Y 2048
- **캡처**: `.bgra` 파일(256×384)로 저장됩니다.
- **서버 주소**: 에뮬레이터 안에서는 자동 찾기가 안 됩니다. 홈의 `Server`에서 IP를 직접 입력하세요.
- **한계**: SD 카드가 없어서 최근 검색어 저장은 실기에서만 확인할 수 있습니다. 장시간 재생 멈춤 같은 타이밍 버그도 에뮬레이터에서는 재현되지 않습니다.

## 서버 명령(프로토콜)

| 명령 | 쓰는 ROM | 내용 |
|---|---|---|
| `SEARCH5 <검색어>` | `hq2-20261004-2` 이후 | 24개, `id<TAB>길이<TAB>채널<TAB>제목`, 제목 그림(232×22) + 채널 그림(232×12) |
| `SEARCH4` | `hq2-20261004` | 24개, 길이 포함 |
| `SEARCH3` | 그 이전 HQ2 | 8개, 제목 그림 |
| `PLAY4 <id> <초>` | HQ2 | 위치부터 재생, `OK STREAM4 <길이> <시작>` 뒤에 YDS3 스트림 |
| `TEST3` | HQ2 | 시험 영상·소리 |
| UDP `YTDSI?` | HQ2 | 서버 자동 찾기 |

재생 중 DSi는 매초 `BUF <버퍼ms> <상태> <디코드ms> <늦은 그림 수> [1=덮개 닫힘]`를 보냅니다. 예전 ROM이 보내는 `DBG` 줄은 서버가 받아서 무시합니다.

## 릴리스

1. 커밋과 푸시를 합니다. 태그는 `hq2-YYYYMMDD`로 붙이고, 같은 날 두 번째부터는 `-2`를 붙입니다.
2. `gh release create <태그> client-hq2/YouTubeDSiHQ2.nds`로 올리고, 한국어 릴리스 노트를 씁니다.
3. 서버가 바뀌었으면 릴리스 노트에 "서버도 업데이트"를 적습니다.

참고: 이 PC는 재부팅하면 Windows 자격 증명 관리자에 저장된 gh 토큰이 사라집니다. 그래서 `gh auth login -h github.com -w --insecure-storage`로 로그인해 토큰을 파일에 저장해 두었습니다.
