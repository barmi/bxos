/* fs_fat.c — FAT12/FAT16 마운트 + ATA-기반 read/write 경로.
 *
 * 부팅 시 fs_mount_data() 가 호출되어 ATA drive 0 의 BPB 를 읽고,
 * FAT 한 카피와 루트 디렉터리를 메모리에 캐시한다.
 * 클러스터 데이터 자체는 on-demand 로 ata_read_sectors() 호출.
 *
 * 기존 file.c (FDD 메모리 이미지 기반) 는 nihongo.fnt 부팅 로딩에만 쓰이고,
 * 콘솔/앱 파일 접근은 모두 이 모듈을 통한다. Phase 4 의 쓰기 경로는
 * 데이터 디스크(FAT16)만 대상으로 하며, 모든 변경은 즉시 ATA 로 flush 한다.
 */

#include "bootpack.h"

struct FS_MOUNT g_data_mount;
int g_data_mounted = 0;

static int pack_83_name(char *name, unsigned char out[11]);
static int sync_finfo_entry(struct FS_MOUNT *m, struct FILEINFO *finfo);
static int zero_cluster(struct FS_MOUNT *m, unsigned int clus);
static unsigned int find_free_cluster(struct FS_MOUNT *m);
static unsigned int find_tail_cluster(struct FS_MOUNT *m, unsigned int clus);

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
	static struct FILEINFO found;
	unsigned char name83[11];
	struct DIR_SLOT slot;

	if (!g_data_mounted) return 0;
	if (pack_83_name(name, name83) != 0) {
		return 0;
	}
	if (dir_find(0, name83, &found, &slot) != 0) {
		return 0;
	}
	if ((found.type & 0x18) != 0) {
		return 0;
	}
	if (slot.cache_entry != 0) {
		return slot.cache_entry;
	}
	return &found;
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

static unsigned int eoc_value(const struct FS_MOUNT *m)
{
	return (m->fs_type == 16) ? 0xFFFF : 0x0FFF;
}

static unsigned int cluster_lba(const struct FS_MOUNT *m, unsigned int clus)
{
	return m->data_lba + (clus - 2) * m->sectors_per_cluster;
}

static int valid_data_cluster(const struct FS_MOUNT *m, unsigned int clus)
{
	return clus >= 2 && clus < m->cluster_count + 2;
}

static void fat_set(struct FS_MOUNT *m, unsigned int clus, unsigned int val)
{
	if (m->fs_type == 16) {
		unsigned int off = clus * 2;
		m->fat_cache[off]     = val & 0xff;
		m->fat_cache[off + 1] = (val >> 8) & 0xff;
	}
}

static int sync_fat_entry(struct FS_MOUNT *m, unsigned int clus)
{
	unsigned int off, sec, lba;
	int i;

	if (m->fs_type != 16) {
		return -1;
	}
	off = clus * 2;
	sec = off / 512;
	if (sec >= (unsigned int) m->sectors_per_fat) {
		return -1;
	}
	for (i = 0; i < m->num_fats; i++) {
		lba = m->fat_lba + i * m->sectors_per_fat + sec;
		if (ata_write_sectors(m->drive, lba, 1, m->fat_cache + sec * 512) != 1) {
			return -1;
		}
	}
	return 0;
}

int dir_iter_open(struct DIR_ITER *it, unsigned int dir_clus)
{
	struct FS_MOUNT *m = &g_data_mount;

	if (!g_data_mounted || it == 0) {
		return -1;
	}
	if (dir_clus != 0 && !valid_data_cluster(m, dir_clus)) {
		return -1;
	}
	it->dir_clus = dir_clus;
	it->cur_lba = (dir_clus == 0) ? m->root_lba : cluster_lba(m, dir_clus);
	it->cur_offset_in_sector = 0;
	it->cur_cluster_offset = 0;
	it->cur_clus = dir_clus;
	it->entry_index = 0;
	it->sector_loaded = 0;
	it->at_end = 0;
	return 0;
}

int dir_iter_next(struct DIR_ITER *it, struct FILEINFO *entry, struct DIR_SLOT *slot_addr)
{
	struct FS_MOUNT *m = &g_data_mount;
	struct FILEINFO *src;
	unsigned int next;

	if (!g_data_mounted || it == 0 || entry == 0 || slot_addr == 0 || it->at_end) {
		return 0;
	}
	if (it->dir_clus == 0) {
		if (it->entry_index >= (unsigned int) m->root_entries) {
			it->at_end = 1;
			return 0;
		}
		src = &m->root_cache[it->entry_index];
		*entry = *src;
		slot_addr->lba = m->root_lba + (it->entry_index * 32) / 512;
		slot_addr->offset = (unsigned short) ((it->entry_index * 32) % 512);
		slot_addr->cache_entry = src;
		it->cur_lba = slot_addr->lba;
		it->cur_offset_in_sector = slot_addr->offset;
		it->entry_index++;
		return 1;
	}

	if (!valid_data_cluster(m, it->cur_clus)) {
		return -1;
	}
	if (!it->sector_loaded) {
		if (ata_read_sectors(m->drive, it->cur_lba, 1, it->sector) != 1) {
			return -1;
		}
		it->sector_loaded = 1;
	}
	src = (struct FILEINFO *) (it->sector + it->cur_offset_in_sector);
	*entry = *src;
	slot_addr->lba = it->cur_lba;
	slot_addr->offset = (unsigned short) it->cur_offset_in_sector;
	slot_addr->cache_entry = 0;

	it->cur_offset_in_sector += 32;
	it->entry_index++;
	if (it->cur_offset_in_sector >= 512) {
		it->cur_offset_in_sector = 0;
		it->sector_loaded = 0;
		it->cur_lba++;
		it->cur_cluster_offset++;
		if (it->cur_cluster_offset >= (unsigned int) m->sectors_per_cluster) {
			next = fat_get(m, it->cur_clus);
			if (is_eoc(m, next)) {
				it->at_end = 1;
			} else if (!valid_data_cluster(m, next)) {
				return -1;
			} else {
				it->cur_clus = next;
				it->cur_lba = cluster_lba(m, next);
				it->cur_cluster_offset = 0;
			}
		}
	}
	return 1;
}

void dir_iter_close(struct DIR_ITER *it)
{
	if (it != 0) {
		it->at_end = 1;
	}
	return;
}

int dir_find(unsigned int parent_clus, unsigned char name83[11],
		struct FILEINFO *finfo, struct DIR_SLOT *slot_addr)
{
	struct DIR_ITER it;
	struct FILEINFO cur;
	struct DIR_SLOT slot;
	unsigned char *entry;
	int i, r;

	if (name83 == 0 || finfo == 0 || slot_addr == 0) {
		return -1;
	}
	if (dir_iter_open(&it, parent_clus) != 0) {
		return -1;
	}
	for (;;) {
		r = dir_iter_next(&it, &cur, &slot);
		if (r <= 0) {
			break;
		}
		if (cur.name[0] == 0x00) {
			break;
		}
		if (cur.name[0] == 0xe5 || (cur.type & 0x08) != 0) {
			continue;
		}
		entry = (unsigned char *) &cur;
		for (i = 0; i < 11; i++) {
			if (entry[i] != name83[i]) {
				goto next_entry;
			}
		}
		*finfo = cur;
		*slot_addr = slot;
		dir_iter_close(&it);
		return 0;
next_entry:
		;
	}
	dir_iter_close(&it);
	return -1;
}

int dir_write_slot(struct DIR_SLOT *slot_addr, struct FILEINFO *entry)
{
	struct FS_MOUNT *m = &g_data_mount;
	unsigned char secbuf[512];
	struct FILEINFO *dst;

	if (!g_data_mounted || slot_addr == 0 || entry == 0 ||
			slot_addr->offset > 512 - 32) {
		return -1;
	}
	if (slot_addr->cache_entry != 0) {
		*slot_addr->cache_entry = *entry;
	}
	if (ata_read_sectors(m->drive, slot_addr->lba, 1, secbuf) != 1) {
		return -1;
	}
	dst = (struct FILEINFO *) (secbuf + slot_addr->offset);
	*dst = *entry;
	if (ata_write_sectors(m->drive, slot_addr->lba, 1, secbuf) != 1) {
		return -1;
	}
	return 0;
}

static int dir_slot_from_finfo(struct FS_MOUNT *m, struct FILEINFO *finfo,
		struct DIR_SLOT *slot_addr)
{
	unsigned int idx;

	if (m == 0 || finfo == 0 || slot_addr == 0) {
		return -1;
	}
	idx = finfo - m->root_cache;
	if (idx >= (unsigned int) m->root_entries) {
		return -1;
	}
	slot_addr->lba = m->root_lba + (idx * 32) / 512;
	slot_addr->offset = (unsigned short) ((idx * 32) % 512);
	slot_addr->cache_entry = finfo;
	return 0;
}

static int sync_finfo_entry(struct FS_MOUNT *m, struct FILEINFO *finfo)
{
	struct DIR_SLOT slot;

	if (dir_slot_from_finfo(m, finfo, &slot) != 0) {
		return -1;
	}
	return dir_write_slot(&slot, finfo);
}

static int pack_83_name(char *name, unsigned char out[11])
{
	int i, j;
	for (j = 0; j < 11; j++) {
		out[j] = ' ';
	}
	j = 0;
	if (name[0] == 0 || name[0] == '.') {
		return -1;
	}
	for (i = 0; name[i] != 0; i++) {
		unsigned char c = name[i];
		if (c <= ' ' || c == '/' || c == '\\') {
			return -1;
		}
		if (c == '.' && j <= 8) {
			j = 8;
			continue;
		}
		if (j >= 11) {
			return -1;
		}
		if ('a' <= c && c <= 'z') {
			c -= 0x20;
		}
		out[j++] = c;
	}
	return 0;
}

static int write_cluster(struct FS_MOUNT *m, unsigned int clus, const void *buf)
{
	if (!valid_data_cluster(m, clus)) {
		return -1;
	}
	if (ata_write_sectors(m->drive, cluster_lba(m, clus),
			m->sectors_per_cluster, buf) != m->sectors_per_cluster) {
		return -1;
	}
	return 0;
}

static int read_cluster(struct FS_MOUNT *m, unsigned int clus, void *buf)
{
	if (!valid_data_cluster(m, clus)) {
		return -1;
	}
	if (ata_read_sectors(m->drive, cluster_lba(m, clus),
			m->sectors_per_cluster, buf) != m->sectors_per_cluster) {
		return -1;
	}
	return 0;
}

static int zero_cluster(struct FS_MOUNT *m, unsigned int clus)
{
	unsigned char zero[8 * 512];
	int i, bytes = m->sectors_per_cluster * 512;
	if (bytes > (int) sizeof zero) {
		return -1;
	}
	for (i = 0; i < bytes; i++) {
		zero[i] = 0;
	}
	return write_cluster(m, clus, zero);
}

static unsigned int find_free_cluster(struct FS_MOUNT *m)
{
	unsigned int clus, end = m->cluster_count + 2;
	for (clus = 2; clus < end; clus++) {
		if (fat_get(m, clus) == 0) {
			return clus;
		}
	}
	return 0;
}

static unsigned int append_dir_cluster(struct FS_MOUNT *m, unsigned int dir_clus)
{
	unsigned int newclus, tail;

	if (!valid_data_cluster(m, dir_clus)) {
		return 0;
	}
	newclus = find_free_cluster(m);
	if (newclus == 0) {
		return 0;
	}
	if (zero_cluster(m, newclus) != 0) {
		return 0;
	}
	fat_set(m, newclus, eoc_value(m));
	if (sync_fat_entry(m, newclus) != 0) {
		return 0;
	}
	tail = find_tail_cluster(m, dir_clus);
	if (tail == 0) {
		return 0;
	}
	fat_set(m, tail, newclus);
	if (sync_fat_entry(m, tail) != 0) {
		return 0;
	}
	return newclus;
}

int dir_alloc_slot(unsigned int parent_clus, struct DIR_SLOT *slot_addr)
{
	struct FS_MOUNT *m = &g_data_mount;
	struct DIR_ITER it;
	struct FILEINFO cur;
	struct DIR_SLOT slot;
	unsigned int newclus;
	int r;

	if (!g_data_mounted || m->fs_type != 16 || slot_addr == 0) {
		return -1;
	}
	if (dir_iter_open(&it, parent_clus) != 0) {
		return -1;
	}
	for (;;) {
		r = dir_iter_next(&it, &cur, &slot);
		if (r < 0) {
			dir_iter_close(&it);
			return -1;
		}
		if (r == 0) {
			break;
		}
		if (cur.name[0] == 0x00 || cur.name[0] == 0xe5) {
			*slot_addr = slot;
			dir_iter_close(&it);
			return 0;
		}
	}
	dir_iter_close(&it);
	if (parent_clus == 0) {
		return -1;
	}
	newclus = append_dir_cluster(m, parent_clus);
	if (newclus == 0) {
		return -1;
	}
	slot_addr->lba = cluster_lba(m, newclus);
	slot_addr->offset = 0;
	slot_addr->cache_entry = 0;
	return 0;
}

static unsigned int find_tail_cluster(struct FS_MOUNT *m, unsigned int clus)
{
	unsigned int next;
	if (!valid_data_cluster(m, clus)) {
		return 0;
	}
	for (;;) {
		next = fat_get(m, clus);
		if (is_eoc(m, next)) {
			return clus;
		}
		if (!valid_data_cluster(m, next)) {
			return 0;
		}
		clus = next;
	}
}

static unsigned int append_cluster(struct FS_MOUNT *m, struct FILEINFO *finfo)
{
	unsigned int newclus, tail;

	newclus = find_free_cluster(m);
	if (newclus == 0) {
		return 0;
	}

	/* 데이터 영역을 먼저 초기화한 뒤 FAT 에 연결한다. */
	if (zero_cluster(m, newclus) != 0) {
		return 0;
	}
	fat_set(m, newclus, eoc_value(m));
	if (sync_fat_entry(m, newclus) != 0) {
		return 0;
	}

	if (finfo->clustno == 0) {
		finfo->clustno = newclus;
		return newclus;
	}
	tail = find_tail_cluster(m, finfo->clustno);
	if (tail == 0) {
		return 0;
	}
	fat_set(m, tail, newclus);
	if (sync_fat_entry(m, tail) != 0) {
		return 0;
	}
	return newclus;
}

static unsigned int nth_cluster(struct FS_MOUNT *m, struct FILEINFO *finfo,
		unsigned int nth, int create)
{
	unsigned int clus, next, i;

	if (finfo->clustno == 0) {
		if (!create) {
			return 0;
		}
		if (append_cluster(m, finfo) == 0) {
			return 0;
		}
	}
	clus = finfo->clustno;
	for (i = 0; i < nth; i++) {
		next = fat_get(m, clus);
		if (is_eoc(m, next)) {
			if (!create) {
				return 0;
			}
			next = append_cluster(m, finfo);
			if (next == 0) {
				return 0;
			}
		} else if (!valid_data_cluster(m, next)) {
			return 0;
		}
		clus = next;
	}
	return clus;
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

int fs_data_read(struct FILEINFO *finfo, int pos, void *buf, int n)
{
	struct FS_MOUNT *m = &g_data_mount;
	unsigned char *dst = (unsigned char *) buf;
	unsigned char tmp[8 * 512];
	int cluster_bytes, done = 0, off, chunk, i;
	unsigned int nth, clus;

	if (!g_data_mounted || finfo == 0 || pos < 0 || n < 0) {
		return -1;
	}
	if ((unsigned int) pos >= finfo->size) {
		return 0;
	}
	if ((unsigned int) (pos + n) > finfo->size) {
		n = finfo->size - pos;
	}
	cluster_bytes = m->sectors_per_cluster * 512;
	if (cluster_bytes > (int) sizeof tmp) {
		return -1;
	}

	while (done < n) {
		nth = (pos + done) / cluster_bytes;
		off = (pos + done) % cluster_bytes;
		chunk = n - done;
		if (chunk > cluster_bytes - off) {
			chunk = cluster_bytes - off;
		}
		clus = nth_cluster(m, finfo, nth, 0);
		if (clus == 0 || read_cluster(m, clus, tmp) != 0) {
			return -1;
		}
		for (i = 0; i < chunk; i++) {
			dst[done + i] = tmp[off + i];
		}
		done += chunk;
	}
	return done;
}

int fs_data_create(char *name)
{
	struct FILEINFO entry;
	struct FILEINFO existing;
	struct DIR_SLOT slot;
	unsigned char s[11];
	unsigned char *raw;
	int j;

	if (!g_data_mounted || g_data_mount.fs_type != 16) {
		return -1;
	}
	if (pack_83_name(name, s) != 0) {
		return -1;
	}
	if (dir_find(0, s, &existing, &slot) == 0) {
		return -2;
	}
	if (dir_alloc_slot(0, &slot) != 0) {
		return -3;
	}

	raw = (unsigned char *) &entry;
	for (j = 0; j < 32; j++) {
		raw[j] = 0;
	}
	for (j = 0; j < 8; j++) entry.name[j] = s[j];
	for (j = 0; j < 3; j++) entry.ext[j]  = s[8 + j];
	entry.type = 0x20;	/* archive */
	entry.clustno = 0;
	entry.size = 0;
	if (dir_write_slot(&slot, &entry) != 0) {
		return -4;
	}
	return 0;
}

int fs_data_write(struct FILEINFO *finfo, int pos, const void *buf, int n)
{
	struct FS_MOUNT *m = &g_data_mount;
	const unsigned char *src = (const unsigned char *) buf;
	unsigned char tmp[8 * 512];
	int cluster_bytes, done = 0, off, chunk, i;
	unsigned int nth, clus;

	if (!g_data_mounted || m->fs_type != 16 || finfo == 0 || pos < 0 || n < 0) {
		return -1;
	}
	if (pos > (int) finfo->size) {
		return -1;	/* sparse write 는 아직 지원하지 않는다. */
	}
	cluster_bytes = m->sectors_per_cluster * 512;
	if (cluster_bytes > (int) sizeof tmp) {
		return -1;
	}

	while (done < n) {
		nth = (pos + done) / cluster_bytes;
		off = (pos + done) % cluster_bytes;
		chunk = n - done;
		if (chunk > cluster_bytes - off) {
			chunk = cluster_bytes - off;
		}
		clus = nth_cluster(m, finfo, nth, 1);
		if (clus == 0) {
			return -1;
		}
		if (off != 0 || chunk != cluster_bytes) {
			if (read_cluster(m, clus, tmp) != 0) {
				return -1;
			}
		}
		for (i = 0; i < chunk; i++) {
			tmp[off + i] = src[done + i];
		}
		if (write_cluster(m, clus, tmp) != 0) {
			return -1;
		}
		done += chunk;
	}
	if ((unsigned int) (pos + n) > finfo->size) {
		finfo->size = pos + n;
	}
	if (sync_finfo_entry(m, finfo) != 0) {
		return -1;
	}
	return done;
}

int fs_data_truncate(struct FILEINFO *finfo, int size)
{
	struct FS_MOUNT *m = &g_data_mount;
	int cluster_bytes;
	unsigned int keep_count, i, clus, next;

	if (!g_data_mounted || m->fs_type != 16 || finfo == 0 || size < 0) {
		return -1;
	}
	if ((unsigned int) size > finfo->size) {
		return -1;
	}
	if (size == (int) finfo->size) {
		return 0;
	}
	if (finfo->clustno == 0) {
		finfo->size = 0;
		return sync_finfo_entry(m, finfo);
	}

	cluster_bytes = m->sectors_per_cluster * 512;
	if (size == 0) {
		clus = finfo->clustno;
		finfo->clustno = 0;
		finfo->size = 0;
		if (sync_finfo_entry(m, finfo) != 0) {
			return -1;
		}
		while (valid_data_cluster(m, clus)) {
			next = fat_get(m, clus);
			fat_set(m, clus, 0);
			if (sync_fat_entry(m, clus) != 0) {
				return -1;
			}
			if (is_eoc(m, next)) {
				break;
			}
			clus = next;
		}
		return 0;
	}

	keep_count = (size + cluster_bytes - 1) / cluster_bytes;
	clus = finfo->clustno;
	for (i = 1; i < keep_count; i++) {
		clus = fat_get(m, clus);
		if (!valid_data_cluster(m, clus)) {
			return -1;
		}
	}
	next = fat_get(m, clus);
	fat_set(m, clus, eoc_value(m));
	if (sync_fat_entry(m, clus) != 0) {
		return -1;
	}
	finfo->size = size;
	if (sync_finfo_entry(m, finfo) != 0) {
		return -1;
	}
	while (valid_data_cluster(m, next)) {
		clus = next;
		next = fat_get(m, clus);
		fat_set(m, clus, 0);
		if (sync_fat_entry(m, clus) != 0) {
			return -1;
		}
		if (is_eoc(m, next)) {
			break;
		}
	}
	return 0;
}

int fs_data_unlink(struct FILEINFO *finfo)
{
	struct FS_MOUNT *m = &g_data_mount;
	unsigned int clus, next;

	if (!g_data_mounted || m->fs_type != 16 || finfo == 0) {
		return -1;
	}
	clus = finfo->clustno;
	finfo->name[0] = 0xe5;
	finfo->clustno = 0;
	finfo->size = 0;
	if (sync_finfo_entry(m, finfo) != 0) {
		return -1;
	}
	while (valid_data_cluster(m, clus)) {
		next = fat_get(m, clus);
		fat_set(m, clus, 0);
		if (sync_fat_entry(m, clus) != 0) {
			return -1;
		}
		if (is_eoc(m, next)) {
			break;
		}
		clus = next;
	}
	return 0;
}
