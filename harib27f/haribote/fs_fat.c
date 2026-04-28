/* fs_fat.c — FAT12/FAT16 마운트 + ATA-기반 read 경로 (work1 Phase 3).
 *
 * 부팅 시 fs_mount_data() 가 호출되어 ATA drive 0 의 BPB 를 읽고,
 * FAT 한 카피와 루트 디렉터리를 메모리에 캐시한다.
 * 클러스터 데이터 자체는 on-demand 로 ata_read_sectors() 호출.
 *
 * 기존 file.c (FDD 메모리 이미지 기반) 는 nihongo.fnt 부팅 로딩에만 쓰이고,
 * 콘솔/앱 파일 접근은 모두 이 모듈을 통한다.
 */

#include "bootpack.h"

struct FS_MOUNT g_data_mount;
int g_data_mounted = 0;

/* BPB(첫 섹터) 한 장만 읽어 마운트 정보를 채운다. */
static int read_bpb(int drive, struct FS_MOUNT *m)
{
	unsigned char bs[512];
	int sec_per_clus, reserved, num_fats, root_ent, fat_sz;
	unsigned int tot_sec;
	unsigned int data_sectors, cluster_count;

	if (ata_read_sectors(drive, 0, 1, bs) != 1) {
		return -1;
	}
	if (bs[510] != 0x55 || bs[511] != 0xAA) {
		return -1;
	}
	if ((bs[11] | (bs[12] << 8)) != 512) {
		return -1;	/* bytes/sector != 512 는 지원 안 함 */
	}
	sec_per_clus = bs[13];
	reserved     = bs[14] | (bs[15] << 8);
	num_fats     = bs[16];
	root_ent     = bs[17] | (bs[18] << 8);
	fat_sz       = bs[22] | (bs[23] << 8);
	tot_sec      = bs[19] | (bs[20] << 8);
	if (tot_sec == 0) {
		tot_sec = (unsigned int) bs[32]
		        | ((unsigned int) bs[33] << 8)
		        | ((unsigned int) bs[34] << 16)
		        | ((unsigned int) bs[35] << 24);
	}
	if (sec_per_clus == 0 || num_fats == 0 || fat_sz == 0 || root_ent == 0) {
		return -1;
	}

	m->drive               = drive;
	m->total_sectors       = tot_sec;
	m->sectors_per_cluster = sec_per_clus;
	m->reserved_sectors    = reserved;
	m->num_fats            = num_fats;
	m->sectors_per_fat     = fat_sz;
	m->root_entries        = root_ent;
	m->fat_lba             = reserved;
	m->root_lba            = reserved + num_fats * fat_sz;
	m->root_sectors        = (root_ent * 32 + 511) / 512;
	m->data_lba            = m->root_lba + m->root_sectors;

	data_sectors  = tot_sec - m->data_lba;
	cluster_count = data_sectors / sec_per_clus;
	m->cluster_count = cluster_count;

	/* FAT12: cluster_count < 4085. FAT16: 4085 <= cluster_count < 65525. */
	m->fs_type = (cluster_count < 4085) ? 12 : 16;
	return 0;
}

int fs_mount_data(int drive)
{
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	struct FS_MOUNT *m = &g_data_mount;
	unsigned int fat_bytes, root_bytes;

	g_data_mounted = 0;
	if (!ata_drive_info[drive].present) {
		return -1;
	}
	if (read_bpb(drive, m) != 0) {
		return -1;
	}

	fat_bytes  = (unsigned int) m->sectors_per_fat * 512;
	root_bytes = (unsigned int) m->root_sectors * 512;

	m->fat_cache  = (unsigned char *)  memman_alloc_4k(memman, fat_bytes);
	m->root_cache = (struct FILEINFO *) memman_alloc_4k(memman, root_bytes);
	if (m->fat_cache == 0 || m->root_cache == 0) {
		return -1;
	}

	if (ata_read_sectors(drive, m->fat_lba,  m->sectors_per_fat, m->fat_cache) < 0) {
		return -1;
	}
	if (ata_read_sectors(drive, m->root_lba, m->root_sectors,    (void *) m->root_cache) < 0) {
		return -1;
	}

	g_data_mounted = 1;
	return 0;
}

struct FILEINFO *fs_data_root(void)
{
	return g_data_mounted ? g_data_mount.root_cache : 0;
}

int fs_data_root_max(void)
{
	return g_data_mounted ? g_data_mount.root_entries : 0;
}

struct FILEINFO *fs_data_search(char *name)
{
	if (!g_data_mounted) return 0;
	return file_search(name, g_data_mount.root_cache, g_data_mount.root_entries);
}

/* FAT 한 엔트리 읽기. FAT12 는 12bit packed, FAT16 은 16bit LE. */
static unsigned int fat_get(const struct FS_MOUNT *m, unsigned int clus)
{
	if (m->fs_type == 16) {
		unsigned int off = clus * 2;
		return (unsigned int) m->fat_cache[off]
		     | ((unsigned int) m->fat_cache[off + 1] << 8);
	} else {
		unsigned int off = clus + (clus / 2);
		unsigned int v   = (unsigned int) m->fat_cache[off]
		                 | ((unsigned int) m->fat_cache[off + 1] << 8);
		return (clus & 1) ? (v >> 4) : (v & 0x0FFF);
	}
}

static int is_eoc(const struct FS_MOUNT *m, unsigned int clus)
{
	if (m->fs_type == 16) {
		return clus >= 0xFFF8;
	} else {
		return clus >= 0x0FF8;
	}
}

/* clustno 부터 시작하는 체인을 따라 size 바이트만큼 읽어 buf 에 채운다.
 * 실패 시 -1, 성공 시 읽은 바이트 수 (size 그대로) 반환. */
static int fs_read_chain(struct FS_MOUNT *m, unsigned int clustno, int size, char *buf)
{
	int cluster_bytes = m->sectors_per_cluster * 512;
	int remaining = size;
	while (remaining > 0) {
		unsigned int lba = m->data_lba + (clustno - 2) * m->sectors_per_cluster;
		if (remaining >= cluster_bytes) {
			if (ata_read_sectors(m->drive, lba, m->sectors_per_cluster, buf) < 0) {
				return -1;
			}
			buf += cluster_bytes;
			remaining -= cluster_bytes;
		} else {
			/* 마지막 클러스터: 클러스터 전체를 임시 버퍼로 받고 필요한 만큼만 복사. */
			unsigned char tmp[8 * 512]; /* sec_per_clus 최대 8 가정 (32MB FAT16 = 4) */
			int i;
			if (m->sectors_per_cluster > 8) {
				/* 더 큰 클러스터가 필요하면 동적 alloc 으로 대체. 현재는 미지원. */
				return -1;
			}
			if (ata_read_sectors(m->drive, lba, m->sectors_per_cluster, tmp) < 0) {
				return -1;
			}
			for (i = 0; i < remaining; i++) {
				buf[i] = (char) tmp[i];
			}
			remaining = 0;
			break;
		}
		clustno = fat_get(m, clustno);
		if (is_eoc(m, clustno)) {
			break;
		}
	}
	return size - remaining;
}

/* file.c 의 file_loadfile2 와 동일한 인터페이스: alloc + read + tek 압축 풀기.
 * 데이터 드라이브 마운트가 안 되어 있으면 0 반환. */
char *fs_data_loadfile(int clustno, int *psize)
{
	int size = *psize, size2;
	struct MEMMAN *memman = (struct MEMMAN *) MEMMAN_ADDR;
	char *buf, *buf2;

	if (!g_data_mounted) return 0;
	buf = (char *) memman_alloc_4k(memman, size);
	if (buf == 0) return 0;

	if (fs_read_chain(&g_data_mount, (unsigned int) clustno, size, buf) < 0) {
		memman_free_4k(memman, (int) buf, size);
		return 0;
	}
	if (size >= 17) {
		size2 = tek_getsize(buf);
		if (size2 > 0) {
			buf2 = (char *) memman_alloc_4k(memman, size2);
			tek_decomp(buf, buf2, size2);
			memman_free_4k(memman, (int) buf, size);
			buf = buf2;
			*psize = size2;
		}
	}
	return buf;
}
