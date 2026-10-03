# YouTubeDSi 실기기 해결·복원 기록

작성일: 2026-10-01. 대상: 일본판 Nintendo DSi 1.4.5J, DSi 모드,
TWiLight Menu++ / GodMode9i. NAND·시스템 소프트웨어는 변경하지 않았다.

## 실기기에서 확인된 기준판

`YouTubeDSiCompat.nds` 실행을 요청한 뒤 사용자가 “잘 되네.”라고 확인했다.
다음 보고는 영상·소리가 조금 끊긴다는 것이었다. 따라서 부팅과 검색·재생
경로가 동작하는 기준판이며, 무결한 연속 재생까지 확인된 판으로 기록하지 않는다.

기준판 SHA-256:

```text
b230264e8ce51541bd45c85e29f9dfabd3be2de204eeeeaf715eef55921bf84c
```

동일 파일이 `../roms/tools/YouTubeDSiCompat.nds`,
`client/YouTubeDSiCompat.nds`, `sd-root/YouTubeDSiCompat.nds`에 보존되어 있다.
수정하기 전 소스·ROM·ELF·ARM7 map·CRT·링커·패치·서버·설정·검증 기록은
`releases/YouTubeDSiCompat-20261001.zip`에 백업했다. 각 파일 해시는 같은 이름의
폴더 안 `manifest.json`에서 확인할 수 있다.

기준판은 **DSi 모드로 실행하면서 DS/NTR Wi-Fi를 사용한다.** 저장된 DS 호환
Open/WEP AP를 사용한다. TWL 네이티브 Wi-Fi 또는 WPA 접속을 해결한 것으로
해석하면 안 된다. 실기기에서 동작한 접속 경로를 유지한다.

## 흰 화면 문제를 어떻게 해결했는가

1. 최소 Boot ROM과 Maxmod Audio ROM은 부팅했다. 정상 DSWiFi ARM7을 넣으면
   ARM9가 Wi-Fi API를 호출하기도 전에 두 화면이 하얗게 멈췄다. 따라서 검색
   프로토콜이나 DHCP가 최초 원인이 아니었다.
2. SDK 기본 ARM7 TWL 실행 주소는 `0x03000000`이다. Audio의 TWL 코드와 BSS는
   `0x03001F84`까지, Wi-Fi는 `0x03009938`까지 사용한다. Wi-Fi만 32 KiB를 넘는다.
3. 로더가 기존 `MBK6=0x080037C0` 매핑을 유지하면 NWRAM은 `0x037C0000`에 있고,
   `0x03000000`은 32 KiB 공유 WRAM의 반복 주소가 될 수 있다. 이 조건에서
   `0x03008000` 이후 Wi-Fi BSS를 지우는 작업이 `0x03000000`의 실행 코드를
   지운다. 같은 조건을 melonDS의 공개 매핑 API로 설정했을 때 Boot/Audio 성공,
   Wi-Fi 부팅 실패와 실제 코드 영역의 0 초기화를 재현했다.
4. **실기기의 MBK 레지스터 값은 직접 덤프하지 않았다.** 이 메커니즘은 재현된
   사실이고 실기기 원인에 대해서는 강한 근거가 있는 추론이다. 일본어·지역
   설정이 원인이라는 증거는 없다.
5. ARM7 TWL 코드·데이터·BSS·힙을 DSi 메인 RAM의
   `0x02D00000..0x02D40000`으로 옮겼다. ARM9 힙 상한도 생성자 실행 전 CRT에서
   `0x02D00000`으로 제한했다. 로더의 잠긴 MBK를 다시 설정할 필요가 없고,
   두 CPU의 힙이 겹치지 않는다.
6. SDK 1.24.0의 깨끗한 Wi-Fi+Maxmod ARM7 코어를 사용했다. Timer0는 Maxmod,
   Timer2는 Wi-Fi, Timer3는 RTC가 사용한다. 이전 임시 진단용 ARM7을 최종 앱에
   섞지 않았다.
7. 접속 경로를 `WIFI_DS_MODE_ONLY`로 고정하고 초기화·자동 접속을 작업
   코루틴에서 진행해 UI를 유지했다. 이 조합으로 실기기에서 검색·재생이
   동작했다.

관련 파일은 `tools/prepare_compat_runtime.py`, `tools/build_compat.sh`,
`compat-runtime/runtime.patch`, `client/source/main.c`이다. 설치된 SDK 자체는
덮어쓰지 않고 프로젝트의 사설 CRT·링커를 사용한다.

## stage 0x32의 정확한 의미와 남은 TWL 문제

Connect RAM2의 마지막 `0x32`는 SDIO controller wrapper가 반환했다는 뜻이다.
그 다음 명령은 `wifi_card_device_init()` 직접 호출이다. 중간에 별도의 대기는
없다. 같은 C 파일 안에서 해결된 호출은 GNU `--wrap`이 가로채지 않으므로,
당시 `0x33` 훅은 실제 함수 진입을 관측하지 못했다. 따라서 “0x32와 0x33 사이에서
멈췄다”라고 단정할 수 없다. BMI/디바이스/라디오 준비 과정의 정확한 실기기
정지 위치는 아직 확인하지 못했다.

해당 DSWiFi 버전에서 Status 1은 `ASSOCSTATUS_SEARCHING`이다. DHCP 완료를 뜻하지
않는다. SPI `0x1F800 / len512 / op2`는 래핑한 펌웨어 설정 읽기가 반환했다는
뜻이며, 성공 여부나 무선 칩 펌웨어 업로드를 증명하지 않는다. SCFG 읽기 0도
접근이 닫힌 상태와 구분해야 한다.

공식 ftpd v3.2.1은 ARM7 TWL을 `0x037C0000`에 배치하고 NTR Wi-Fi를 사용한다.
소스와 공식 배포 바이너리를 함께 비교했다. 사용자의 실기기 ftpd 파일 자체의
버전·해시는 아직 확인하지 않았으므로 공식 비교판과 동일하다고 단정하지 않는다.
melonDS의 직접 부팅은 헤더에 맞춰 NWRAM을 설정하고 네이티브 무선 펌웨어도
모델링하므로, 일반 에뮬레이터 성공이 실기기 성공의 증거가 될 수 없다.

ELF 주소·호출 명령·타임아웃·ftpd·DSiDL·상위 프로젝트 조사와 출처는
`investigation/HARDWARE_FINDINGS.md`에 있다. 재현용 자료는
`investigation/alias-tests`, `compat-static.json`, `connect2-disasm.txt`,
`ftpd-module-layout.json`, `compat-verification.json`에 보존했다.

## 고정한 개발 환경

| 구성 요소 | 버전 / 커밋 |
|---|---|
| BlocksDS SDK | v1.24.0, package 1.24.0-1 |
| DSWiFi | `02f193979c945b41386f5b5050f7cc426a999484` |
| libnds | `7fd8ccbe781ed48a06fe063a2dcbb69035fa97d3` |
| Maxmod | `a797317e5bb4eceebe5f41b85b620d68ef534b79` |
| Wonderful GCC | 16.2.0, package commit `be93d9d35bf` |
| Binutils | 2.47 |
| melonDS 회귀 코어 | `906e9ebb27da8c6a715cd7abab4abfe8a8d29427` |
| Windows 릴레이 런타임 | Python 3.11, FFmpeg `C:/ffmpeg/bin/ffmpeg.exe` |

앱 작업 폴더는 애플리케이션 Git 커밋이 없는 로컬 프로젝트다. 존재하지 않는
앱 커밋을 기준으로 재현하지 않는다. 소스 백업과 manifest 해시를 기준으로 한다.
SDK를 무조건 최신으로 바꾸면 사설 CRT와 링크할 라이브러리의 일치가 깨질 수 있다.

## 기준판으로 즉시 복원

- DSi에는 보존된 `YouTubeDSiCompat.nds`를 복사한다. 이미 성공한 동일한 DSi 모드
  실행 설정과 저장된 AP를 유지한다.
- PC는 `start-server.cmd`로 기존 릴레이 포트 8765를 실행한다.
- SD의 `youtube-dsi.ini`에 PC의 현재 IP를 `host=`로 기록한다. 현재 시험 IP는
  `192.168.0.4`다. PC의 주소가 바뀌면 이 값만 갱신한다.
- 새 소스 작업과 분리해 복원하려면 ZIP을 **새 폴더에** 푼다. 작동하는 ROM이나
  NAND 위에 덮어쓰지 않는다.
- PowerShell `Get-FileHash -Algorithm SHA256 <ROM 경로>`로 위 해시와 대조한다.

소스를 다시 빌드하려면 같은 SDK 환경을 사용한다. HQ 소스 백업에는
`runtime-upstream`에 SDK v1.24.0 CRT·링커·ARM7 코어 원본과 해시를 고정해 두었다.
이 원본이 없는 옛 기준판 소스 백업에서는 `investigation/upstream/sdk`에 SDK
v1.24.0 Git 태그를 준비한다. **작업 폴더의 복사본에서 빌드하여 보존 ROM을
덮어쓰지 않는다.** WSL Ubuntu-22.04 안에서 복사본 프로젝트 루트로 이동해 다음을
실행한다.

```sh
sh tools/build_compat.sh
python3 tools/analyze_compat.py
```

새로 빌드된 파일은 **새 이름으로** 실기기에서 확인한 다음 채택한다. 기존
기준 ROM은 교체하지 않는다. 소스 빌드 성공만으로 하드웨어 검증 기록을 승계하지
않으며, `analyze_compat.py`도 동일 ROM 해시에만 기존 실기기 증거를 붙인다.

## 화질·버퍼 개선판

개선판은 별도 이름 `YouTubeDSiHQ.nds`, 릴레이 `start-quality-server.cmd`와 포트
8767을 사용한다. 기존 8765 릴레이와 기준판을 보존한다. 자세한 설정·품질 한계와
검증 수치는 `investigation/quality`의 기록 및 [legacy/HQ-GUIDE.md](legacy/HQ-GUIDE.md)를 참조한다.
실기기 검증 상태와 에뮬레이터 검증 상태를 구분해 기록한다.

## bunjalloo·post-title exploit 환경 추가 조사

현재 bunjalloo ROM이 공식 0.12.0과 동일함을 확인했다. 그 ROM도 ARM7 TWL을
`0x03000000`에 복사하고 BSS가 `0x0300AB88`까지 이어진다. 같은 32 KiB WRAM
미러 가정에서 코드가 지워지는 pre-main 실패를 재현했다. DS 모드는 이 TWL
초기화를 건너뛰고 NTR Wi-Fi 경로를 사용한다. 다만 이번 에뮬레이터에는 SD
이미지가 없어 앱의 FAT 초기화 메시지까지 확인했으며 브라우징 성공은 미확인이다.

Memory Pit 관련 외부 사례는 실행 환경 가설을 지지한다. 모든 title exploit의
TWL Wi-Fi가 불가능하다는 결론은 아니며, 실기기 MBK 덤프와 네이티브 정지 명령은
여전히 확인되지 않았다. 근거·공식 ROM 해시·CRT 명령·비교 화면·추가 시험의
해석은 [실행환경 분석](../investigation/bunjalloo/실행환경-분석.md)에 기록했다.
기존 기준판·HQ ROM과 배포 ZIP을 보존했다. YouTubeDSi는 DSi 모드+NTR Wi-Fi
조합을 유지하며 이 조사로 NAND나 시스템 소프트웨어를 변경하지 않았다.

## HQ2: 안정 화질 + DSi 스타일 밑 화면 (2026-10-02)

HQ 실기 재생은 됐지만, 화면이 블록처럼 뭉개지고 화질이 오락가락했다. 원인은 고정 JPEG 예산이
Decode(Wi-Fi 처리 포함) 60ms 초과 때마다 22%씩 깎이고 거의 다시 오르지 않는 구조였다
(실기 로그 budget=3650). 새 `YDS3`는 품질을 장면 단위로 고정하고, 대역폭이 모자라면 그림
수를 줄인다(0바이트 = 이전 그림 유지). 밑 화면은 DSi Desk 스타일로 새로 그렸고, 검색 제목은
PC가 Galmuri9로 렌더링해 한국어·일본어도 표시된다. 별도 이름 `YouTubeDSiHQ2.nds`,
소스 `client-hq2/`이며 기존 HQ/Compat ROM과 `client/`는 바꾸지 않았다. 서버는 PLAY2/TEST2를
그대로 유지했고, 원본은 `server/backup-20261001/`에 있다. 상세: [HQ2-CHANGELOG.md](HQ2-CHANGELOG.md).
실기 검증 전이다.

## 제작자 표기 통일 (2026-10-02)

사용자 요청으로 직접 만든 ROM 21개의 배너 제작자 줄을 모두 `hanbi`로 바꿨다(기존: Local prototype / Local diagnostic
/ DSi Desk homebrew). 바꾼 범위는 **배너 글자와 배너 CRC뿐**이고 ARM9/ARM7 코드는 바이트 단위로 같다. HQ ROM에서
달라진 바이트는 배너 안의 126바이트뿐이고, ndstool `-fb`로 다시 검사해도 CRC가 같았다. 그래서 SHA-256만 바뀌었고,
실기 검증 결과는 그대로 유효하다.

| ROM | 이전 SHA-256 | 현재 SHA-256 |
|---|---|---|
| YouTubeDSiCompat.nds (기준판) | `b230264e8ce51541bd45c85e29f9dfabd3be2de204eeeeaf715eef55921bf84c` | `df5e9b87e4c02fed94e721c440dfedd6f137265ffb0ac39d2291ab753861b96d` |
| YouTubeDSiHQ.nds | `4cb26a3450b2e148bfd40663d5f2e4e9a125357e9b6885cef82bd0a0e86db879` | `8dcb6f4f4ab8053b40d6368f8f4148053c4e89e295c5e16b5855bcc29c63178d` |

원본은 `NDSi/backups/banner-backup-20261002/`에 경로 그대로 백업했고, 전체 목록과 해시는 `author-patch-log.json`에 있다.
에뮬레이터·조사 폴더의 증거용 사본과 `releases/` ZIP은 바꾸지 않았다. `client/Makefile`의 GAME_AUTHOR도 `hanbi`로 바꿨다.

## TWL(DSi 전용) Wi-Fi 현황 정리 (2026-10-04)

- **현재 결론**: YouTubeDSi는 모든 판이 DSi 모드에서 **DS 호환(NTR) Wi-Fi**를 쓴다(`WIFI_DS_MODE_ONLY`).
  TWL Wi-Fi는 이 기기(일본판 DSi, 익스플로잇으로 진입한 TWiLight Menu++, Unlaunch 없음)에서 멈춘다.
- **원인(실기 진단, 2026-10-04)**: 진단 ROM(`NDSi/bunjalloo-dsi/twl-diag/`, 기록은 그 폴더의 `FINDINGS.md`)으로 확인했다.
  - 무선칩은 **AR6002**(초기형 모듈, chip id `0x02000001`, rev `0x11`)다.
  - SDIO 전압 협상(CMD5)은 카드가 이미 초기화돼 있어 건너뛰고, 부트로더(BMI) 응답(version `0x20000188`)도 정상이다.
  - 그런데 DSWiFi가 확인하는 시점에 **무선칩에 Atheros 펌웨어가 올라가 있지 않다**(`hi_board_data_initialized` = 0).
  - DSWiFi는 시스템이 펌웨어를 미리 올려 뒀다고 가정하고 재시작(BMI_DONE)만 한다. 펌웨어 업로드 코드는 `#if 0`이고,
    그나마 AR6014용이다. 그래서 빈 칩을 시작시킨 뒤 "펌웨어 준비"(STEP 0x72)를 시간 제한 없이 기다린다.
  - 이 대기가 ARM7의 VBlank 인터럽트 안에서 일어나서 ARM7 전체가 멈춘다(입력, SD 등 ARM7이 하는 일이 모두 정지).
  - 펌웨어가 왜 없는지는 미확인이다. 원래 없었을 수도 있고(익스플로잇 호스트 앱이나 로더가 모듈을 리셋), DSWiFi가 확인 직전에
    하는 SDIO 기능 끄기→켜기(`func0 0x2`)가 지웠을 수도 있다. 그 줄에 DSWiFi 자체 주석 "adding delay after this one
    wipes the loaded firmware??"가 있다.
  - NTR 모드는 Atheros 펌웨어가 필요 없어서 동작한다.
- **고치려면**:
  1. AR6002용 펌웨어 업로드를 구현한다. 사용자 NAND의 Wi-Fi 펌웨어 타이틀(`0003000F-484E4341`)에서 추출해
     BMI로 올린다. 펌웨어는 재배포할 수 없으므로 각자 SD에서 읽게 해야 한다.
  2. 최소한 무한 대기를 시간 제한으로 바꿔 ARM7이 멈추지 않게 하고 NTR로 돌아가게 한다.
  - 이 둘은 DSWiFi 상류에도 알릴 만한 내용이다(AR6002 펌웨어 업로드 미구현, 시간 제한 없는 대기).
- 32KB 미러 문제(bunjalloo 흰 화면)는 별개다. ARM7의 DSi 전용 영역을 메인 RAM(`0x02D00000`)으로 옮기면 해결되고,
  진단 ROM도 그렇게 빌드했다.
- 참고: 장시간 재생 멈춤([blocksds/sdk#401](https://codeberg.org/blocksds/sdk/issues/401))은 NTR 드라이버 버그로, TWL과는 관계없다.
