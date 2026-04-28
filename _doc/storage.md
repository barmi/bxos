# BxOS 스토리지 — 드라이브 모델 / FAT16 레이아웃 / ATA 사용

work1 작업으로 BxOS 의 디스크 구성이 단일 FDD 이미지에서 **부팅 FDD + 데이터 HDD** 두 갈래로 분리됐고, work2 작업으로 데이터 HDD 에 **서브디렉터리 / 다단계 경로 / 현재 작업 디렉터리(cwd)** 가 추가됐다. 이 문서는 게스트(BxOS) 와 호스트(QEMU/CMake) 양쪽에서 그 구조를 어떻게 인식하고 다루는지 정리한다.

관련 코드:
- 드라이버: [harib27f/haribote/ata.c](../harib27f/haribote/ata.c)
- FAT12/16 마운트 + 읽기/쓰기: [harib27f/haribote/fs_fat.c](../harib27f/haribote/fs_fat.c)
- 호스트 도구: [tools/modern/mkfat12.py](../tools/modern/mkfat12.py), [tools/modern/bxos_fat.py](../tools/modern/bxos_fat.py)
- 빌드: [CMakeLists.txt](../CMakeLists.txt) §12a/12b, [run-qemu.sh](../run-qemu.sh)

---

## 1. 드라이브 모델

| 호스트 파일 | 게스트 인식 | 용도 | 파일시스템 | 크기 |
|---|---|---|---|---|
| `build/cmake/haribote.img` | `A:` (FDD, `-fda`) | 부팅 디스크 — `HARIBOTE.SYS` + `NIHONGO.FNT` | FAT12 | 1.44 MiB |
| `build/cmake/data.img`     | `C:` (HDD, `-hda`, ATA Primary Master) | 사용자 디스크 — HE2 앱 + 데이터 파일 | FAT16 | 32 MiB |

- 부팅 흐름은 그대로 유지된다. IPL 이 FDD 전체를 `ADR_DISKIMG` 로 메모리 로드 → `HariMain` 진입 → `nihongo.fnt` 만 메모리 이미지에서 읽는다. (file.c 의 `file_loadfile2()` 가 FDD 메모리 캐시 전용으로 남아 있음.)
- HDD 는 부팅에 관여하지 않는다. `HariMain` 의 PIC 초기화 직후 `ata_init()` 으로 IDENTIFY 결과를 캐시하고, memman 초기화 직후 `fs_mount_data(0)` 으로 BPB/FAT/루트를 마운트한다.
- 콘솔의 기본 드라이브는 `C:` (HDD). `dir`, 앱 검색, `type`, syscall `api_fopen` 등 사용자 경로 전부 data.img 만 본다. 드라이브 prefix 는 없고 `/` 가 data.img 루트다.
- HDD 가 마운트되지 않으면 `g_data_mounted = 0` 상태로 남고, `dir` / 앱 실행은 모두 실패한다 (옛 ADR_DISKIMG 폴백은 없음).

### `run-qemu.sh` 의 자동 부착

```
./run-qemu.sh                    # haribote.img(-fda) + data.img(-hda) 둘 다
./run-qemu.sh --data path.img    # 데이터 디스크 명시
./run-qemu.sh --no-data          # FDD 만 (HDD 없이 부팅)
```

`build/cmake/data.img` 가 존재하면 자동으로 `-hda` 로 붙는다. 환경 변수 `BXOS_DATA_IMG` 로 다른 경로를 지정할 수 있고, `--data` / `--no-data` 가 항상 우선한다.

---

## 2. FAT16 데이터 디스크 레이아웃

`mkfat12.py --fs fat16 --size 32M` 가 만든 BPB 기준 (실측):

| 필드 | 값 | 비고 |
|---|---|---|
| OEM ID | `HARIBOTE` | mkfat12.py 가 박는 마커 |
| Bytes/sector | 512 | 고정 |
| Sectors/cluster | 4 | → cluster size = 2 KiB |
| Reserved sectors | 1 | BPB 한 섹터만 |
| Number of FATs | 2 | FAT1 + FAT2 동기화 |
| Sectors/FAT | 64 | 16-bit FAT × 16344 entries |
| Root entries | 512 | 32 섹터 = 16 KiB |
| Total sectors | 65536 | 32 MiB |
| Cluster count | 16343 | FAT16 범위(4085 ≤ N < 65525) |

LBA 배치:

```
LBA  0       1                129       161                       65535
     ┌──┬─────────────┬─────────────┬──────────────────────────────┐
     │BS│  FAT1 (64)  │  FAT2 (64)  │ ROOT  │     DATA AREA        │
     │  │             │             │ (32)  │    (16343 * 4 sec)   │
     └──┴─────────────┴─────────────┴───────┴──────────────────────┘
```

- BS(LBA 0) = BPB + 부트 서명 (0xAA55). BxOS HDD 는 부팅하지 않으므로 BS 의 IPL 영역은 더미.
- FAT 두 카피가 연속 배치. `fs_fat.c` 의 `sync_fat_entry()` 가 항상 FAT1/FAT2 양쪽에 같이 쓴다.
- 루트 디렉터리는 고정 32 섹터(=512 entries × 32B). FAT16 이라 root_lba 가 미리 정해진다 (FAT32 와 다름).
- 데이터 영역은 cluster #2 부터 시작. cluster N 의 LBA = `data_lba + (N-2) * spc`.

`fs_fat.c::FS_MOUNT` 가 위 값을 통째로 캐시하고, FAT 64 KiB + ROOT 16 KiB = 80 KiB 만 RAM 에 상주시킨다. 데이터 클러스터는 ATA on-demand read.

### 쓰기 정책 (write-through)

`fs_data_create_path` / `fs_file_write` / `fs_file_truncate` / `fs_file_unlink` 및 디렉터리 생성/삭제는 모두 동일한 절차:

1. 메모리 캐시(`fat_cache`, `root_cache`) 또는 디렉터리 클러스터 갱신
2. **즉시** 영향받은 섹터를 ATA 로 flush (`sync_fat_entry`, `dir_write_slot`, `write_cluster`)
3. FAT 변경은 FAT1 + FAT2 같은 섹터 두 번 write

캐시/지연 쓰기 없음. 부팅 후 디스크 상태 = 마지막 syscall 직후 상태.

### 경로 / 8.3 이름 정책

- 호스트 도구(`mkfat12.py`, `bxos_fat.py`) 와 게스트 코드(`pack_83_name`) 모두 **단순 절단 + 대문자 변환**.
- LFN 미지원. 컴포넌트 하나마다 FAT 8.3 이름으로 변환한다.
- 경로 구분자는 `/`. `/foo`, `foo/bar`, `./foo`, `../foo` 를 지원한다. DOS 스타일 `C:/foo` prefix 는 없다.
- `.`/`..` 는 표준 FAT 디렉터리 entry 로 저장한다. 서브디렉터리의 `..` first cluster 는 부모가 root 일 때 `0` 이다.
- `mkdir` 는 부모 디렉터리가 이미 있어야 한다. `rmdir` 는 빈 디렉터리만 삭제한다.
- 호스트에서 8자 초과 이름을 던지면 잘려서 충돌할 수 있음 — 의도적 단순화.

---

## 3. ATA PIO 드라이버 사용

### I/O 포트 (Primary IDE)

| 포트 | 의미 |
|---|---|
| `0x1F0` | Data (16-bit) |
| `0x1F1` | Error / Features |
| `0x1F2` | Sector count |
| `0x1F3-5` | LBA low/mid/high |
| `0x1F6` | Drive/Head — drive select + LBA[27:24] |
| `0x1F7` | Status (read) / Command (write) |
| `0x3F6` | Alt status / Device control |

명령어 4개만 사용:

| Cmd | 용도 | 함수 |
|---|---|---|
| `0xEC` IDENTIFY | 모델명/섹터수 조회 | `ata_identify()` |
| `0x20` READ SECTORS | 28-bit LBA read | `ata_read_sectors()` |
| `0x30` WRITE SECTORS | 28-bit LBA write | `ata_write_sectors()` |
| `0xE7` CACHE FLUSH | write 직후 강제 flush | `ata_write_sectors()` 내부 |

### 동작 방식

- **폴링 + 타임아웃**. 인터럽트 미사용 (PIC 마스킹 변경 없음). 타임아웃은 `ATA_TIMEOUT_LOOPS = 1,000,000` 회 status 검사.
- 28-bit LBA 까지만. 데이터 디스크 32 MiB = 65536 섹터로 충분.
- IDENTIFY 결과는 `ata_drive_info[2]` 에 캐시됨 (master/slave 둘 다). `present` 플래그로 존재 여부 표시.
- Write 마다 `0xE7` cache flush 보냄 → write-through 정책과 짝.

### 콘솔에서 확인

```
disk
```

→ IDENTIFY 캐시 결과 + LBA 0 한 섹터 read 를 보여준다. data.img 가 정상이면:

```
ata0  master  QEMU HARDDISK / sectors=65536 / size=32MB
LBA0  oem='HARIBOTE'  sig=aa55
```

### 함정

- **부트 시점에 hang** 가능 — IDENTIFY 가 응답 없는 드라이브에서 멈출 수 있다. 그래서 timeout 필수. 부팅이 안 되면 `disk` 명령 출력의 `present=0/1` 부터 확인.
- **virtio-blk 미지원** — PCI 스택이 없어 ATA 만 사용. QEMU `-drive if=virtio,...` 로 붙이면 안 보인다. `-hda` 또는 `-drive if=ide,...` 사용.
- **MBR/파티션 테이블 없음** — data.img 는 raw FAT16 볼륨. 호스트 macOS `mount -t msdos` 도 그대로 붙는다.

---

## 4. 호스트 도구

| 도구 | 역할 |
|---|---|
| [tools/modern/mkfat12.py](../tools/modern/mkfat12.py) | 빈 FAT12/FAT16 이미지 + 파일 한 번에 묶어 만들기. CMake 의 `kernel-img` / `data-img` 가 사용. |
| [tools/modern/bxos_fat.py](../tools/modern/bxos_fat.py) | 기존 FAT16 이미지에 부분 갱신 — `create / ls / cp / rm / mkdir / rmdir`. `install-<app>` 헬퍼 타겟의 백엔드. |

### 자주 쓰는 호스트 명령

```bash
# 전체 빌드 (kernel + data 둘 다)
cmake --build build/cmake

# 부분 갱신: 한 앱만 다시 빌드해서 data.img 에 박아넣기
cmake --build build/cmake --target install-tetris

# 이미지 안 들여다보기
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/

# 임의 파일 넣기/빼기
python3 tools/modern/bxos_fat.py mkdir build/cmake/data.img:/sub
python3 tools/modern/bxos_fat.py cp HOST:build/cmake/he2/bin/tetris.he2 build/cmake/data.img:/sub/tetris.he2
python3 tools/modern/bxos_fat.py ls build/cmake/data.img:/sub
python3 tools/modern/bxos_fat.py cp build/cmake/data.img:/sub/tetris.he2 HOST:/tmp/tetris.he2
python3 tools/modern/bxos_fat.py rm build/cmake/data.img:/sub/tetris.he2
python3 tools/modern/bxos_fat.py rmdir build/cmake/data.img:/sub

# 검증
fsck_msdos -n build/cmake/data.img
```

macOS 네이티브 마운트는 [_doc/fat16_check.md](fat16_check.md) 참고.

---

## 5. 알려진 제약 / 범위 외

현재 범위 밖으로 남겨 둔 항목:

- LFN 미지원 (8.3 만, 단순 절단)
- `mkdir -p`, `rm -r`, `cp -r` 같은 recursive 동작 없음
- ext2 등 다른 파일시스템 없음
- 디스크 캐시 / 버퍼 풀 없음 (write-through)
- 멀티 유저 / 권한 / 파일 잠금 없음
- HDD 부팅 안 됨 (MBR/파티션 테이블 없음)
- virtio-blk, AHCI/SATA 안 함 — PCI 스택 부재
