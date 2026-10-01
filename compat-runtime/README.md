# YouTubeDSiCompat build

이 빌드는 DSi 모드를 유지하고 NTR Wi-Fi 경로를 사용합니다. 저장된
DS 호환 AP(열린 네트워크/WEP)가 필요합니다. WPA 전용 설정은 지원하지 않습니다.
ARM7 TWL 코드·데이터·힙을 메인 RAM에 예약하고 ARM9 CRT에서 해당 영역을
할당 대상에서 제외합니다. NAND, 시스템 소프트웨어, 설치된 SDK는 변경하지 않습니다.
2026-10-01에 사용자가 실기기 전체 앱 시험 후 “잘 되네.”라고 성공을 확인했습니다.

WSL Ubuntu-22.04에서 프로젝트 폴더로 이동한 뒤 실행합니다.

```sh
sh tools/build_compat.sh
python3 tools/verify_compat_static.py
```

출력은 `client/YouTubeDSiCompat.nds`입니다. `runtime.patch`는 SDK v1.24.0 대비
CRT/링커 변경입니다. 생성 스크립트는 설치된 SDK 버전과 링커 원문을 검사합니다.
ARM7 main은 같은 릴리스에서 가져옵니다. 다른 SDK 전체를 업데이트하지 않습니다.

기존 YouTubeDSi ROM 이름을 덮어쓰지 마세요. 새 파일 하나를 SD 카드에 복사하고
현재와 같은 DSi 모드로 실행합니다. PC 릴레이는 `start-server.cmd`로 실행할 수
있으며 현재 기본 주소는 `192.168.0.4:8765`입니다. `youtube-dsi.ini`가 있으면
그 파일의 설정이 우선합니다. 메뉴 Y로 서버 IP를 바꿀 수 있습니다.

메뉴 A로 검색하고 결과에서 A로 재생합니다. X는12초 영상·440Hz 소리 시험입니다.
재생 중 B는 돌아가기, 메뉴 START는 종료입니다. 자세한 분석과 실기기 검증의
한계는 `investigation/HARDWARE_FINDINGS.md`에 기록되어 있습니다.
