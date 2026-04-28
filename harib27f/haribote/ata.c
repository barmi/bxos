/* ata.c — Primary IDE / ATA PIO 드라이버 (28-bit LBA).
 *
 * work1 Phase 2: HDD 데이터 디스크(data.img) 인식과 섹터 단위 read/write.
 * 인터럽트 없이 polling 방식. write-through 정책.
 *
 * I/O 포트:
 *   0x1F0  Data        (16-bit)
 *   0x1F1  Error / Features
 *   0x1F2  Sector count
 *   0x1F3  LBA low
 *   0x1F4  LBA mid
 *   0x1F5  LBA high
 *   0x1F6  Drive/Head  (LBA bits 24-27, drive select)
 *   0x1F7  Status / Command
 *   0x3F6  Alt status / Device control
 *
 * 드라이브 번호: 0 = master, 1 = slave.
 */

#include "bootpack.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_FEATURES    0x1F1
#define ATA_SECCOUNT    0x1F2
#define ATA_LBA_LO      0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HI      0x1F5
#define ATA_DRIVE       0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7
#define ATA_ALTSTATUS   0x3F6

#define ATA_SR_BSY      0x80
#define ATA_SR_DRDY     0x40
#define ATA_SR_DF       0x20
#define ATA_SR_DRQ      0x08
#define ATA_SR_ERR      0x01

#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_CACHE_FLUSH     0xE7
#define ATA_CMD_IDENTIFY        0xEC

#define ATA_TIMEOUT_LOOPS       1000000

static int ata_io_wait(int port, int mask, int want)
{
	/* status 의 mask 비트들이 want 와 같아질 때까지 대기. 타임아웃 시 -1. */
	int i;
	for (i = 0; i < ATA_TIMEOUT_LOOPS; i++) {
		int s = io_in8(port);
		if ((s & mask) == want) {
			return s;
		}
		if (s & ATA_SR_ERR) {
			return -1;
		}
	}
	return -1;
}

static void ata_io_delay(void)
{
	/* alt status 4번 read = 약 400ns. ATA 스펙이 요구하는 셀렉트 후 안정화 시간. */
	io_in8(ATA_ALTSTATUS);
	io_in8(ATA_ALTSTATUS);
	io_in8(ATA_ALTSTATUS);
	io_in8(ATA_ALTSTATUS);
}

static void ata_select(int drive, unsigned int lba)
{
	/* bit 7,5 = 1 (LBA mode 사용 시 옛 호환), bit 6 = 1 (LBA), bit 4 = drive. */
	int v = 0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F);
	io_out8(ATA_DRIVE, v);
	ata_io_delay();
}

int ata_identify(int drive, struct ATA_INFO *out)
{
	int i, status;
	unsigned short id[256];

	/* 결과 초기화 */
	for (i = 0; i < 41; i++) out->model[i] = 0;
	out->total_sectors = 0;
	out->present = 0;

	ata_select(drive, 0);
	io_out8(ATA_SECCOUNT, 0);
	io_out8(ATA_LBA_LO, 0);
	io_out8(ATA_LBA_MID, 0);
	io_out8(ATA_LBA_HI, 0);
	io_out8(ATA_COMMAND, ATA_CMD_IDENTIFY);

	status = io_in8(ATA_STATUS);
	if (status == 0) {
		return 0;	/* 디바이스 없음 */
	}

	/* BSY 떨어질 때까지 대기 */
	if (ata_io_wait(ATA_STATUS, ATA_SR_BSY, 0) < 0) {
		return -1;
	}

	/* ATAPI/SATA 등 비-ATA 디바이스는 IDENTIFY 응답 시 LBA mid/hi 가 0 이 아님. */
	if (io_in8(ATA_LBA_MID) != 0 || io_in8(ATA_LBA_HI) != 0) {
		return -1;
	}

	/* DRQ 또는 ERR 까지 대기 */
	for (i = 0; i < ATA_TIMEOUT_LOOPS; i++) {
		status = io_in8(ATA_STATUS);
		if (status & ATA_SR_ERR) return -1;
		if (status & ATA_SR_DRQ) break;
	}
	if (i == ATA_TIMEOUT_LOOPS) return -1;

	for (i = 0; i < 256; i++) {
		id[i] = (unsigned short) io_in16(ATA_DATA);
	}

	/* model: words 27-46 (40 ASCII bytes, byte-pair swapped) */
	for (i = 0; i < 20; i++) {
		out->model[i * 2]     = (char) (id[27 + i] >> 8);
		out->model[i * 2 + 1] = (char) (id[27 + i] & 0xFF);
	}
	out->model[40] = 0;
	/* 끝의 공백 트림 */
	for (i = 39; i >= 0 && out->model[i] == ' '; i--) {
		out->model[i] = 0;
	}

	/* total LBA28 sectors: words 60-61 */
	out->total_sectors = ((unsigned int) id[61] << 16) | (unsigned int) id[60];
	out->present = 1;
	return 1;
}

int ata_read_sectors(int drive, unsigned int lba, int count, void *buf)
{
	int s, w;
	unsigned short *p = (unsigned short *) buf;

	if (count <= 0 || count > 256) return -1;

	ata_select(drive, lba);
	io_out8(ATA_FEATURES, 0);
	io_out8(ATA_SECCOUNT, count == 256 ? 0 : count);
	io_out8(ATA_LBA_LO,  (lba >>  0) & 0xFF);
	io_out8(ATA_LBA_MID, (lba >>  8) & 0xFF);
	io_out8(ATA_LBA_HI,  (lba >> 16) & 0xFF);
	io_out8(ATA_COMMAND, ATA_CMD_READ_SECTORS);

	for (s = 0; s < count; s++) {
		if (ata_io_wait(ATA_STATUS, ATA_SR_BSY | ATA_SR_DRQ, ATA_SR_DRQ) < 0) {
			return -1;
		}
		for (w = 0; w < 256; w++) {
			*p++ = (unsigned short) io_in16(ATA_DATA);
		}
		ata_io_delay();
	}
	return count;
}

int ata_write_sectors(int drive, unsigned int lba, int count, const void *buf)
{
	int s, w;
	const unsigned short *p = (const unsigned short *) buf;

	if (count <= 0 || count > 256) return -1;

	ata_select(drive, lba);
	io_out8(ATA_FEATURES, 0);
	io_out8(ATA_SECCOUNT, count == 256 ? 0 : count);
	io_out8(ATA_LBA_LO,  (lba >>  0) & 0xFF);
	io_out8(ATA_LBA_MID, (lba >>  8) & 0xFF);
	io_out8(ATA_LBA_HI,  (lba >> 16) & 0xFF);
	io_out8(ATA_COMMAND, ATA_CMD_WRITE_SECTORS);

	for (s = 0; s < count; s++) {
		if (ata_io_wait(ATA_STATUS, ATA_SR_BSY | ATA_SR_DRQ, ATA_SR_DRQ) < 0) {
			return -1;
		}
		for (w = 0; w < 256; w++) {
			io_out16(ATA_DATA, *p++);
		}
		ata_io_delay();
	}

	/* write-through: 캐시 flush 명령 후 BSY 해제 대기. */
	io_out8(ATA_COMMAND, ATA_CMD_CACHE_FLUSH);
	if (ata_io_wait(ATA_STATUS, ATA_SR_BSY, 0) < 0) {
		return -1;
	}
	return count;
}

/* ─── boot 시 한 번 호출. drive 0/1 검사하고 결과를 캐시. ──────────── */
struct ATA_INFO ata_drive_info[2];

void ata_init(void)
{
	ata_identify(0, &ata_drive_info[0]);
	ata_identify(1, &ata_drive_info[1]);
}
