# work1 인수인계 — 다음 세션용 컨텍스트

이 문서는 [_doc/work1.md](work1.md) 작업을 **새 세션에서 처음 보는 사람(또는 새 Claude)** 이 끊김 없이 이어받기 위한 단일 진입점이다. 먼저 work1.md 본문을 읽고, 그다음 이 문서로 현재 위치와 다음 행동을 파악하면 된다.

---

## 1. 한 줄 요약

BxOS(haribote 계열 취미 OS) 에 **쓰기 가능한 디스크 파일시스템**을 도입해, OS 커널 이미지와 사용자 앱 디스크를 **빌드 시점에 분리**하고, 호스트에서 앱을 개별 복사·삭제할 수 있게 만드는 것이 목표.

## 2. 현재 위치 (2026-04-28 기준)

- **Phase 0, Phase 1 완료**. Phase 2 (ATA PIO 블록 드라이버) 착수 직전.
- HE2(.he2) 포맷이 새로 도입되어, 빌드 이미지에 **.hrb 는 더 이상 포함되지 않고 .he2 앱 20개만** 들어감.
- 빌드 산출물이 **두 갈래로 분리**됨:
  - `build/cmake/haribote.img` — 1.44MB FAT12 부팅 FDD (`HARIBOTE.SYS` + `NIHONGO.FNT` 만)
  - `build/cmake/data.img` — 32MB FAT16 데이터 HDD (HE2 앱 + 데모 데이터)
- `run-qemu.sh` / `cmake --build ... --target run` 둘 다 두 디스크를 함께 부착하지만, **게스트 커널은 아직 HDD 를 인식하지 못함** (Phase 2 작업 대상). 부팅 화면은 정상 도달, 콘솔에서 앱 실행은 안 됨 (앱이 FDD에 없음).

## 3. Phase 0 / Phase 1 에서 실제로 바뀐 것

**Phase 0** (분기 결정 + 부팅 스크립트):
| 파일 | 변경 |
|---|---|
| [_doc/work1.md](work1.md) | 2장 결정 사항 표를 "권장" → "확정"으로 잠금. Phase 0 체크박스 닫음. |
| [run-qemu.sh](../run-qemu.sh) | `--data`, `--no-data`, `BXOS_DATA_IMG` 추가. `build/cmake/data.img` 자동 부착. 부팅 이미지 기본도 `build/cmake/haribote.img` 우선, 없으면 `harib27f/haribote.img` 폴백. |

**Phase 1** (이미지 분리):
| 파일 | 변경 |
|---|---|
| [tools/modern/mkfat12.py](../tools/modern/mkfat12.py) | `--fs {fat12,fat16}` + `--size` 옵션. `FsParams` 데이터클래스로 BPB/FAT 폭 분기. FAT12 회귀 검증됨. |
| [CMakeLists.txt](../CMakeLists.txt) §12a/12b/13 | 단일 `image` 빌드를 `kernel-img` (FAT12 1.44MB) + `data-img` (FAT16 32MB) 로 분리. `bxos ALL`/`run` 둘 다 두 이미지 의존. |
| [_doc/work1.md](work1.md) | Phase 1 체크박스 닫음. |

**아직 손대지 않은 것** (의도적, Phase 2 이후 작업):
- 커널 코드 [harib27f/haribote/](../harib27f/haribote/) — 변경 0. ATA 드라이버, FAT 리팩터링 모두 미착수.
- HDD 인식 / 데이터 디스크에서 앱 실행 — Phase 3 이후.

## 4. 확정된 핵심 결정 (재확인용)

work1.md §2 표가 정본. 요약:

- 블록 장치: **ATA PIO 28-bit LBA**
- 부팅: **FDD 유지**, HDD 는 데이터 전용
- FS: **FAT16** (FAT12 호환 한 갈래에 포함). FAT32 는 범위 밖.
- 데이터 디스크 크기: **32MB**, 기본 경로 **`build/cmake/data.img`**
- 쓰기 정책: **write-through**
- 게스트 드라이브: **`A:` = FDD, `C:` = HDD**, 콘솔 기본은 `C:` (Phase 3 도입)
- 호스트 도구: **자체 Python** (외부 mtools 의존 없음)

## 5. 다음 작업 (Phase 2 — ATA PIO 블록 드라이버)

work1.md §3 Phase 2 그대로. 요지:

1. **`harib27f/haribote/ata.c` 신설** (헤더는 [bootpack.h](../harib27f/haribote/bootpack.h) 에 추가):
   - `int ata_identify(int drive, struct ATA_INFO *out)` — IDENTIFY 로 모델/섹터수 확인
   - `int ata_read_sectors(int drive, unsigned int lba, int count, void *buf)` — 28-bit LBA PIO read
   - `int ata_write_sectors(int drive, unsigned int lba, int count, const void *buf)` — write (Phase 4 까지 미사용이지만 같이 만들어두면 편함)
   - 인터럽트 없이 polling 부터. 타임아웃 + 디버그 창 로깅 필수 (work1.md §5 함정).
   - I/O 포트: primary IDE = 0x1F0~0x1F7, control 0x3F6.
2. **[bootpack.c](../harib27f/haribote/bootpack.c) `HariMain` 초기화 단계에 IDENTIFY 호출 추가** — 결과(모델 문자열, 섹터수)를 디버그 창에 출력해 동작 검증.
3. **콘솔 명령 `disk` 임시 추가** ([console.c](../harib27f/haribote/console.c)) — 다시 IDENTIFY 호출해서 결과를 콘솔에 찍기. Phase 3 에서는 `mount` 또는 `dir C:` 로 대체될 임시 디버그 명령.
4. **CMake 등록** — [CMakeLists.txt](../CMakeLists.txt) §4 `BXOS_KERNEL_C_NAMES` 리스트에 `ata` 추가.

**Phase 2 완료 기준**: 부팅 후 디버그 창 또는 `disk` 명령에서 `data.img` 의 IDENTIFY 결과(예: `QEMU HARDDISK`, 65536 sectors)가 보임. 데이터 읽기는 아직 사용 안 함 (Phase 3 작업).

**확장하기 좋은 출발점 코드 위치**:
- 새 .c 파일은 [harib27f/haribote/](../harib27f/haribote/) 에 추가하고 `BXOS_KERNEL_C_NAMES` 에 stem 만 더하면 자동으로 컴파일/링크됨 ([CMakeLists.txt:122-124](../CMakeLists.txt:122))
- 포트 I/O 는 기존 [naskfunc.nas](../harib27f/haribote/naskfunc.nas) 의 `_io_in8` / `_io_out8` / `_io_in16` / `_io_out16` 등을 그대로 사용 (이미 선언/구현 있음)
- `_io_insw` / `_io_outsw` 가 있으면 섹터 데이터 read/write 가 한 줄. 없으면 naskfunc.nas 에 추가 필요 — 한 줄짜리 `rep insw` / `rep outsw`.

## 6. 빠른 빌드/실행 치트시트

```bash
# 첫 설정 (한 번)
cmake -S . -B build/cmake

# 전체 빌드 (kernel-img + data-img)
cmake --build build/cmake

# 개별 빌드
cmake --build build/cmake --target kernel-img   # build/cmake/haribote.img (FAT12 1.44MB)
cmake --build build/cmake --target data-img     # build/cmake/data.img     (FAT16 32MB)

# 부팅 (build/cmake/data.img 이 있으면 자동으로 -hda 부착)
./run-qemu.sh

# 데이터 디스크 명시 / 무시
./run-qemu.sh --data path/to/data.img
./run-qemu.sh --no-data
```

이미지 안 파일 목록 확인 (FAT12/FAT16 BPB 파싱):

```bash
python3 - <<'PY'
import struct, sys
for path in ('build/cmake/haribote.img', 'build/cmake/data.img'):
    try: d = open(path,'rb').read()
    except FileNotFoundError: continue
    bps  = struct.unpack_from('<H', d, 11)[0]
    spc  = d[13]
    rsvd = struct.unpack_from('<H', d, 14)[0]
    nfat = d[16]
    rent = struct.unpack_from('<H', d, 17)[0]
    fsz  = struct.unpack_from('<H', d, 22)[0]
    fst  = d[54:62].decode().strip()
    root_start = (rsvd + nfat * fsz) * bps
    root_secs  = (rent * 32 + bps - 1) // bps
    print(f'== {path}  [{fst}]  {len(d)//1024} KB ==')
    root = d[root_start:root_start + root_secs * bps]
    for i in range(0, len(root), 32):
        e = root[i:i+32]
        if e[0] in (0, 0xE5) or (e[11] & 0x08): continue
        n = e[0:8].decode('ascii','replace').rstrip() + '.' + e[8:11].decode('ascii','replace').rstrip()
        sz = struct.unpack_from('<I', e, 28)[0]
        print(f'  {n:14s} {sz:>8d}')
PY
```

QEMU 가 두 디스크를 인식하는지 확인 (모니터):

```bash
echo -e "info block\nquit" | qemu-system-i386 -m 32 -accel tcg -display none \
  -fda build/cmake/haribote.img -boot a -hda build/cmake/data.img -monitor stdio
# floppy0 + ide0-hd0 두 줄이 나와야 OK.
```

## 7. 코드 길잡이 (어디를 손대게 되는가)

| 영역 | 파일 | Phase 1~7 에서 할 일 |
|---|---|---|
| 호스트 이미지 빌더 | [tools/modern/mkfat12.py](../tools/modern/mkfat12.py) | ☑ Phase 1 완료(FAT16 추가). Phase 7: `bxos_fat.py` 로 분화 (cp/rm/ls 부분 갱신용). |
| 빌드 시스템 | [CMakeLists.txt](../CMakeLists.txt) | ☑ Phase 1 완료(타겟 분리). Phase 7: `install-app` 헬퍼. |
| 부팅 스크립트 | [run-qemu.sh](../run-qemu.sh) | ☑ Phase 0 완료. 추가 변경 불필요. |
| 디스크 드라이버 | (신규) `harib27f/haribote/ata.c` | ← Phase 2. 다음 작업 진입점. |
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
