# YouTube DSi

DSi에서 YouTube를 검색하고 재생하는 홈브류입니다. YouTube 접속과 영상 변환은 같은 Wi-Fi에 있는 서버(Windows PC 또는 안드로이드 폰)가 맡고, DSi는 서버가 보내는 그림(256×192 JPEG, 최대 16fps)과 소리(32kHz 스테레오)를 재생합니다.

- 검색 결과 24개(제목, 채널, 영상 길이). 한국어·일본어 제목도 표시됩니다.
- 최근 검색어 저장, 위치 이동(방향키·터치), 덮개를 닫으면 소리만 재생
- 서버 자동 찾기, 네트워크가 끊기면 같은 위치로 자동 재연결

## 빠른 시작

1. [릴리스](https://github.com/birowsi/youtube-dsi/releases)에서 `YouTubeDSiHQ2.nds`를 받아 SD 카드의 `/roms/tools/`에 넣습니다.
2. 서버를 켭니다.
   - **PC**: `start-quality-server.cmd`
   - **안드로이드 폰**: `tools/termux/setup.sh`
3. TWiLight Menu++에서 **DSi 모드, 133MHz**로 실행합니다. DSi에는 DS 호환(Open/WEP) Wi-Fi 접속이 저장돼 있어야 합니다.

자세한 내용은 [docs/HQ2-GUIDE.md](docs/HQ2-GUIDE.md)를 보세요.

## 문서

| 문서 | 내용 |
|---|---|
| [docs/HQ2-GUIDE.md](docs/HQ2-GUIDE.md) | 사용 설명서: 설치, 서버, 조작, 화질, 제한 |
| [docs/HQ2-CHANGELOG.md](docs/HQ2-CHANGELOG.md) | HQ2 변경 이력(1~11차) |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 빌드, DSi·서버 배포, 에뮬레이터 시험, 서버 명령, 릴리스 |
| [docs/TODO.md](docs/TODO.md) | 할 일과 해결된 일 |
| [docs/LONG-PLAYBACK-INVESTIGATION.md](docs/LONG-PLAYBACK-INVESTIGATION.md) | 장시간 재생 멈춤 조사 기록(해결됨) |
| [docs/TROUBLESHOOTING-LOG.md](docs/TROUBLESHOOTING-LOG.md) | 흰 화면·실행 환경 문제 해결 기록, TWL Wi-Fi 현황 |
| [docs/legacy/](docs/legacy/) | 예전 HQ·Compat 판과 초기 시험 기록 |

## 판 종류

| ROM | 상태 |
|---|---|
| `YouTubeDSiHQ2.nds` | **현재 판.** 소스는 `client-hq2/` |
| `YouTubeDSiHQ.nds`, `YouTubeDSiCompat.nds` | 예전 판(소스는 `client/`, 수정하지 않음) |

## 알려진 제한

- DS 호환 무선(NTR)만 씁니다. WPA/WPA2 공유기에는 붙지 못합니다([TWL Wi-Fi 현황](docs/TROUBLESHOOTING-LOG.md)).
- 한 번에 DSi 한 대만 재생합니다.
- BlocksDS DSWiFi 버그 두 개를 ROM에서 직접 피해 갑니다: [#401](https://codeberg.org/blocksds/sdk/issues/401), [#402](https://codeberg.org/blocksds/sdk/issues/402)

YouTube 로고나 상표는 쓰지 않습니다. 글꼴은 Galmuri9([라이선스](server/fonts/Galmuri-LICENSE.txt))를 씁니다.
