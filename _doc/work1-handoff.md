# work1 인수인계 — 다음 세션용 컨텍스트

이 문서는 [_doc/work1.md](work1.md) 작업을 **새 세션에서 처음 보는 사람(또는 새 Claude)** 이 끊김 없이 이어받기 위한 단일 진입점이다. 먼저 work1.md 본문을 읽고, 그다음 이 문서로 현재 위치와 다음 행동을 파악하면 된다.

---

## 1. 한 줄 요약

BxOS(haribote 계열 취미 OS) 에 **쓰기 가능한 디스크 파일시스템**을 도입해, OS 커널 이미지와 사용자 앱 디스크를 **빌드 시점에 분리**하고, 호스트에서 앱을 개별 복사·삭제할 수 있게 만드는 것이 목표.

## 2. 현재 위치 (2026-04-28 기준)

- **Phase 0 완료**. Phase 1 착수 직전.
- HE2(.he2) 포맷이 새로 도입되어, 빌드 이미지에 **.hrb 는 더 이상 포함되지 않고 .he2 앱 21개만** 들어감 ([CMakeLists.txt](../CMakeLists.txt) Phase 11/11b 참고).
- 현재 빌드 이미지는 여전히 단일 1.44MB FAT12 플로피이고, **모든 파일은 read-only** (부팅 시 메모리로 통째로 로드).

## 3. Phase 0 에서 실제로 바뀐 것

| 파일 | 변경 |
|---|---|
| [_doc/work1.md](work1.md) | 2장 결정 사항 표를 "권장" → "확정" 으로 잠금. Phase 0 체크박스 닫음. |
| [run-qemu.sh](../run-qemu.sh) | 데이터 디스크 인자 추가 (`--data`, `--no-data`, `BXOS_DATA_IMG`). 기본 경로 `build/cmake/data.img` 가 있으면 자동 `-hda` 부착. 부팅 이미지 기본도 `build/cmake/haribote.img` 우선, 없으면 `harib27f/haribote.img` 폴백. |
| _doc/work1-handoff.md (이 파일) | 신규. |

**아직 손대지 않은 것** (의도적):
- 커널 코드 ([harib27f/haribote/](../harib27f/haribote/)) — 변경 0
- [tools/modern/mkfat12.py](../tools/modern/mkfat12.py) — 변경 0 (Phase 1 에서 `--fs fat16` 추가 예정)
- CMake 타겟 분리 — Phase 1 에서 진행

## 4. 확정된 핵심 결정 (재확인용)

work1.md §2 표가 정본. 요약:

- 블록 장치: **ATA PIO 28-bit LBA**
- 부팅: **FDD 유지**, HDD 는 데이터 전용
- FS: **FAT16** (FAT12 호환 한 갈래에 포함). FAT32 는 범위 밖.
- 데이터 디스크 크기: **32MB**, 기본 경로 **`build/cmake/data.img`**
- 쓰기 정책: **write-through**
- 게스트 드라이브: **`A:` = FDD, `C:` = HDD**, 콘솔 기본은 `C:` (Phase 3 도입)
- 호스트 도구: **자체 Python** (외부 mtools 의존 없음)

## 5. 다음 작업 (Phase 1)

work1.md §3 Phase 1 그대로. 요지:

1. [tools/modern/mkfat12.py](../tools/modern/mkfat12.py) 에 `--fs fat16` 옵션 추가
   - 분기점: BPB 레이아웃, FAT 엔트리 폭(12bit packed → 16bit), 클러스터 수, 루트 디렉터리 크기
   - FAT12 동작은 그대로 보존 (기본값)
   - 32MB FAT16 이미지를 빈 상태(또는 파일 목록과 함께)로 생성할 수 있어야 함
2. [CMakeLists.txt](../CMakeLists.txt) 타겟 정리:
   - `kernel-img` : `haribote.sys` + IPL 만 든 최소 부팅 플로피 (기존 `image` 타겟 분리)
   - `data-img`   : 32MB FAT16 HDD 이미지. 현재 IMG 에 들어가는 모든 .he2/데이터 파일 이동.
   - `all`        : 둘 다 (기본 타겟)
   - 호환성: 단일 IMG 생성 경로는 깔끔히 폐기 (work1.md §5 위험요소 결정 사항).
3. 검증: `./run-qemu.sh` 가 두 이미지를 동시에 띄움 (Phase 0 에서 만든 자동 부착 사용).
   **이 시점에는 게스트가 HDD 를 인식하지 않아도 OK** — 부팅 자체만 확인.

Phase 1 완료 기준: `cmake --build build/cmake --target kernel-img` 가 데이터 파일 없이 부팅 가능한 작은 IMG 만 만들고, `--target data-img` 가 32MB `data.img` 를 따로 만든다. `./run-qemu.sh` 가 두 디스크를 들고 부팅 화면까지 도달.

## 6. 빠른 빌드/실행 치트시트

```bash
# 첫 설정 (한 번)
cmake -S . -B build/cmake

# 전체 빌드
cmake --build build/cmake

# 부팅 (build/cmake/data.img 이 있으면 자동으로 -hda 부착)
./run-qemu.sh

# 데이터 디스크 명시 / 무시
./run-qemu.sh --data path/to/data.img
./run-qemu.sh --no-data
```

빌드 후 이미지 안에 든 파일 목록을 빠르게 확인하려면:

```bash
python3 -c "
import struct
d = open('build/cmake/haribote.img','rb').read()
root = d[19*512:(19+14)*512]   # FAT12 floppy: root dir at sector 19, 14 sectors
for i in range(0, len(root), 32):
    e = root[i:i+32]
    if e[0] in (0, 0xE5) or (e[11] & 0x08): continue
    name = e[0:8].decode('ascii','replace').rstrip()
    ext  = e[8:11].decode('ascii','replace').rstrip()
    sz   = struct.unpack('<I', e[28:32])[0]
    print(f'{name:8s}.{ext:3s}  {sz:>8d}')"
```

(Phase 1 이후에는 32MB FAT16 이미지가 별도로 생기므로 위 스크립트는 BPB 파싱 버전으로 교체해야 함 — Phase 7 호스트 도구의 `bxos_fat.py ls` 가 그 역할.)

## 7. 코드 길잡이 (어디를 손대게 되는가)

| 영역 | 파일 | Phase 1~7 에서 할 일 |
|---|---|---|
| 호스트 이미지 빌더 | [tools/modern/mkfat12.py](../tools/modern/mkfat12.py) | Phase 1: FAT16 모드 추가. Phase 7: 별도 도구로 분화. |
| 빌드 시스템 | [CMakeLists.txt](../CMakeLists.txt) | Phase 1: `kernel-img` / `data-img` 타겟 분리. Phase 7: `install-app` 헬퍼. |
| 부팅 스크립트 | [run-qemu.sh](../run-qemu.sh) | Phase 0 에서 완료. 추가 변경 불필요. |
| 디스크 드라이버 | (신규) `harib27f/haribote/ata.c` | Phase 2. |
| 파일시스템 | [harib27f/haribote/file.c](../harib27f/haribote/file.c) | Phase 3 에서 `fs_fat.c` 로 리팩터링. |
| 콘솔 명령 / API | [harib27f/haribote/console.c](../harib27f/haribote/console.c), [harib27f/haribote/bootpack.c](../harib27f/haribote/bootpack.c) | Phase 3, 5, 6. |
| 사용자 API (HE2) | [he2/libbxos/](../he2/libbxos/) | Phase 5. |
| 문서 | [BXOS-COMMANDS.md](../BXOS-COMMANDS.md), [README.utf8.md](../README.utf8.md), [SETUP-MAC.md](../SETUP-MAC.md) | Phase 8. |

## 8. 이 작업에서 함정으로 미리 알아둘 것

work1.md §5 가 정본. 강조 사항만 추리면:

- **ATA PIO 폴링은 hang 가능** — 타임아웃 + 디버그 창 로깅 필수.
- **FAT 쓰기 트랜잭션** — FAT1/FAT2 동기화 + 디렉터리 엔트리 갱신 순서 잘못되면 디스크가 부팅 후 깨진 채 보임.
- **기본 드라이브 변경** — `A:` → `C:` 전환은 호환성 깨짐. Phase 3 에서 `BXOS-COMMANDS.md` 갱신 같이.
- **LFN, 캐시, 멀티유저, 권한, MBR 파티션, ext2 등은 범위 밖**. 욕심내지 말 것.

## 9. 작업하지 말아야 할 것

- 새 README/문서를 만들지 말 것. 기존 [_doc/work1.md](work1.md), [BXOS-COMMANDS.md](../BXOS-COMMANDS.md), [README.utf8.md](../README.utf8.md), [SETUP-MAC.md](../SETUP-MAC.md) 를 갱신.
- HRB 빌드 경로(.hrb 앱들) 는 더 이상 IMG 에 안 들어가므로, 깨져도 신경 쓰지 말 것. 단 [cmake/HariboteApp.cmake](../cmake/HariboteApp.cmake) 의 헬퍼 함수는 호환성을 위해 그대로 둠.
- 기존에 삭제된 `build-mac.sh` / `build-modern.sh` 를 부활시키지 말 것 (의도된 삭제, 곧 커밋 예정).
