# YouTube DSi HQ2 사용 설명서

2026-10-04 기준(`hq2-20261004-2`)의 사용법입니다. 바뀐 내용의 자세한 기록은 [HQ2-CHANGELOG.md](HQ2-CHANGELOG.md)에 있습니다.

## 무엇인가

DSi에서 YouTube를 검색하고 재생하는 홈브류입니다. YouTube 접속과 변환은 같은 Wi-Fi에 있는 서버(PC 또는 안드로이드 폰)가 맡습니다. DSi는 서버가 보내 주는 그림과 소리를 재생합니다.

- **화면**: 256×192, 초당 최대 16장, JPEG. 16:9 영상은 위아래에 검은 띠가 생겨 256×144로 보입니다.
- **소리**: 32kHz 스테레오
- **실기 확인**:
  - 일본판 DSi, TWiLight Menu++, DSi 모드
  - 12분 넘게 연속 재생해도 멈추지 않음
  - 검색, 위치 이동, 최근 검색어

## 준비

### 1. SD 카드
- `YouTubeDSiHQ2.nds`를 SD 카드의 `/roms/tools/`에 넣습니다.
  - [GitHub 릴리스](https://github.com/birowsi/youtube-dsi/releases)에서 받거나, 저장소의 `sd-root/YouTubeDSiHQ2.nds`를 씁니다.
- (선택) SD 루트의 `/youtube-dsi.ini`에 서버 주소를 적습니다.
  ```
  host=192.168.0.4
  quality_port=8767
  ```
  - 적지 않아도 같은 Wi-Fi에 있는 서버를 자동으로 찾습니다. 찾은 주소는 `/youtube-dsi.cache`에 기억합니다.
- 최근 검색어는 `/youtube-dsi-recent.txt`에 저장됩니다.

### 2. Wi-Fi
- DSi 본체 설정에 **DS 호환 접속(Open 또는 WEP)**이 저장돼 있어야 합니다.
- 이 ROM은 DS 호환 무선(NTR)만 씁니다. WPA/WPA2 공유기에는 붙지 못합니다. 이유는 아래 "제한"을 보세요.

### 3. 서버 (둘 중 하나만 켭니다)
- **Windows PC**: `start-quality-server.cmd`를 실행합니다(Python 3.11, Pillow, ffmpeg 필요). TCP/UDP 8767을 씁니다.
- **안드로이드 폰(Termux)**:
  1. Termux와 Termux:Boot를 설치합니다.
  2. `server/server.py`, `server/server_quality.py`, `server/fonts/`, `tools/termux/`를 폰의 `~/youtube-dsi/`에 같은 구조로 복사합니다.
  3. `sh ~/youtube-dsi/tools/termux/setup.sh`를 실행합니다.
  4. 이후에는 폰을 켜면 서버가 자동으로 시작됩니다. 수동 조작은 `start.sh`와 `stop.sh`, 기록은 `~/youtube-dsi/server/relay.log`에 남습니다.
  5. 배터리 최적화에서 Termux를 "제한 없음"으로 두세요.
- 서버와 ROM은 **같은 버전**을 쓰세요. 새 서버는 예전 ROM도 지원합니다. 반대로 새 ROM을 예전 서버에 붙이면 "The server is an old version"이 뜹니다.

### 4. 실행
TWiLight Menu++에서 **DSi 모드, 133MHz**로 실행합니다.

## 조작

| 화면 | 조작 |
|---|---|
| 홈 | A·검색창 터치: 검색 / X: 시험 영상·소리 / Y: 서버 주소 입력 / START: 종료 |
| 키보드 | 터치 입력 / START·OK: 확인 / B: 취소 / SELECT: 지우기 / L: 한글↔영문 / 위·`Recent`: 최근 검색어 |
| 최근 검색어 | A·터치: 바로 검색 / X: 삭제 / B: 돌아가기 |
| 검색 결과 | A·터치: 재생 / 위아래: 선택 / L·R: 페이지 / B: 돌아가기 |
| 재생 | A: 일시정지 / 좌우: 이동할 위치 고르기(누르고 있으면 빨라짐) → A: 이동, B: 취소 / 막대 터치·끌기: 그 위치로 이동 / B: 돌아가기 / 아래쪽 터치: 상세 정보 |

- **검색 결과**: 24개까지 받아 한 화면에 3개씩 보여 줍니다. 각 항목에 제목(한·일 제목 포함), 채널 이름, 영상 길이가 나오고, 라이브는 빨간 `LIVE`로 표시됩니다.
- **재생 화면**: 경과 시간과 전체 길이, 위치 막대, 버퍼, DSi 볼륨이 나옵니다.
- **덮개를 닫으면**: 화면이 꺼지고 소리는 계속 나옵니다. 그동안 서버는 그림을 보내지 않습니다.
- **네트워크가 멈추면**:
  - 5초 동안 데이터가 없으면 화면에 경고가 뜹니다.
  - 8초가 지나면 같은 위치로 다시 연결합니다(최대 5번). 필요하면 Wi-Fi에도 다시 접속합니다.

## 화질

서버가 Wi-Fi 상태를 보고 자동으로 맞춥니다. 별도 설정은 없습니다.
- **원본 영상**: 360p 이하. DSi 화면에서는 480p와 차이가 없고, 서버 부담이 줄어듭니다.
- **JPEG 품질**: 50~80 사이에서 장면 단위로 천천히 바뀝니다. 대역폭이 모자라면 화질은 유지한 채 초당 그림 수를 줄입니다.
- **버퍼**: DSi 쪽 버퍼를 약 4.5초로 유지합니다.
- DS 호환 무선에서 실제로 나오는 속도는 약 90~100KB/s입니다.

## 제한

- **DS 호환 무선(NTR)만 씁니다.** DSi 전용 무선(TWL, WPA2 지원)은 이 기기의 실행 환경에서 칩 초기화가 멈춰서 쓰지 않습니다. 자세한 내용은 [TROUBLESHOOTING-LOG.md](TROUBLESHOOTING-LOG.md)에 있습니다.
- **한 번에 DSi 한 대만 재생합니다.** 새로 재생을 요청하면 이전 재생을 끊고 이어받습니다.
- **YouTube 쪽 변화를 따라가야 합니다.** 서버가 yt-dlp를 쓰기 때문에, YouTube가 바뀌면 서버의 yt-dlp를 업데이트해야 할 수 있습니다.

## DSWiFi 버그 우회

BlocksDS의 DSWiFi에 있는 버그 두 개를 이 ROM이 직접 피해 갑니다.
- **ARM7 `Wifi_Update()` 재진입**: 장시간 재생 중 멈추던 원인입니다. → [blocksds/sdk#401](https://codeberg.org/blocksds/sdk/issues/401)
- **`select()` 타임아웃**: → [blocksds/sdk#402](https://codeberg.org/blocksds/sdk/issues/402)

원인 추적 과정은 [LONG-PLAYBACK-INVESTIGATION.md](LONG-PLAYBACK-INVESTIGATION.md)에 있습니다.
