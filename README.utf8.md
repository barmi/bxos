# BxOS

* barmi (skshin@gmail.com, skshin@hs.ac.kr)
* 2012-09-11 처음 init

## 목표

* 운영체제 개념을 설명하기 위한 OS로 수정
* 필요한 기능을 단위별로 구현

## 할일

* 콘솔 스크롤
* 한글 출력
* 한글 입력기
* CPU 모니터

## 참여

* 어떤식으로든 참여할 사람은 연락바람

---

> 원본 `README` 파일은 EUC-KR로 저장되어 있습니다. 이 파일은 UTF-8로 변환된 사본입니다.
> 소스 트리(`harib27f/`)의 C 파일들은 EUC-KR/CP949 인코딩을 그대로 유지해야 빌드 결과(폰트/문자열) 가 정상 동작합니다 — 일괄 변환하지 마세요.

이 OS는 카와이 히데미의 *30일 OS 자작 입문* (Haribote OS, OSASK)을 기반으로 한국어 콘솔/입력기/CPU 모니터 등을 추가한 교육용 32-bit x86 OS입니다.

## 빠른 시작 (macOS Apple Silicon)

자세한 안내는 `SETUP-MAC.md`, 콘솔 명령은 `BXOS-COMMANDS.md`, 디스크 구조는 `_doc/storage.md` 참고.

```bash
# 1. 사전 도구 설치 (한 번만)
brew install qemu nasm i686-elf-gcc i686-elf-binutils
brew install --cask cmake     # 또는 brew install cmake

# 2. CMake 구성 + 빌드
cmake -S . -B build/cmake
cmake --build build/cmake     # haribote.img(FDD, 1.44MB) + data.img(HDD, 32MB)

# 3. 부팅 (FDD 부팅 + data.img 자동으로 -hda 부착)
./run-qemu.sh
```

빌드 산출물은 두 갈래로 분리되어 있습니다.

| 이미지 | 위치 | 내용 |
|---|---|---|
| 부팅 FDD (FAT12 1.44MB) | `build/cmake/haribote.img` | `HARIBOTE.SYS` + `NIHONGO.FNT` |
| 데이터 HDD (FAT16 32MB) | `build/cmake/data.img` | HE2 앱 24개 + 데모 데이터 8개 |

앱 하나만 다시 빌드한 뒤 데이터 이미지에 부분 갱신하려면:

```bash
cmake --build build/cmake --target install-tetris   # 예: tetris.he2 만 교체
```

호스트에서 데이터 이미지 안의 서브디렉터리와 파일을 직접 다룰 수도 있습니다.

```bash
python3 tools/modern/bxos_fat.py mkdir build/cmake/data.img:/sub
python3 tools/modern/bxos_fat.py cp HOST:build/cmake/he2/bin/tetris.he2 build/cmake/data.img:/sub/tetris.he2
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/sub
python3 tools/modern/bxos_fat.py rm build/cmake/data.img:/sub/tetris.he2
python3 tools/modern/bxos_fat.py rmdir build/cmake/data.img:/sub   # 빈 디렉터리일 때만
```

## 디렉터리 구조

| 경로 | 설명 |
|---|---|
| `harib27f/` | 커널 + legacy 앱 소스 + 폰트 / 데모 데이터 |
| `harib27f/haribote/` | 커널(부트팩) 소스: bootpack.c, console.c, ata.c, fs_fat.c, sheet.c, window.c … |
| `harib27f/<app>/`   | legacy 앱 소스 (HRB) — 빌드는 되지만 더 이상 이미지에 포함되지 않음 |
| `he2/`               | 새 HE2 (Haribote Executable v2) 앱 트리 — 현재 이미지에 들어가는 앱들 |
| `cmake/`             | CMake 툴체인 / HariboteApp 헬퍼 |
| `tools/modern/`      | NASM + gcc + Python 빌드 도구 (`mkfat12.py`, `bxos_fat.py`, …) |
| `z_tools/`, `z_osabin/`, `z_new_o/`, `z_new_w/` | 원본 Windows 빌드 툴체인 — 현재 빌드는 사용하지 않음 |
| `_doc/`              | 설계 문서 (`storage.md`, `work1.md`, …) |
