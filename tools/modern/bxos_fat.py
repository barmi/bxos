#!/usr/bin/env python3
"""bxos_fat.py — host-side FAT16 editor for BxOS data.img.

Supported layout: raw FAT12/16 image with a fixed root directory, as produced
by tools/modern/mkfat12.py. Phase 7 needs FAT16 data.img editing, but ls also
works on FAT12 images.

Examples:
    bxos_fat.py create build/cmake/data.img --size 32M
    bxos_fat.py ls build/cmake/data.img:/
    bxos_fat.py cp HOST:build/cmake/he2/bin/tetris.he2 build/cmake/data.img:/tetris.he2
    bxos_fat.py cp build/cmake/data.img:/tetris.he2 HOST:/tmp/tetris.he2
    bxos_fat.py rm build/cmake/data.img:/tetris.he2
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import mkfat12

SECTOR = 512


def split_8_3(name: str) -> bytes:
    name = name.strip().replace("\\", "/").split("/")[-1]
    if name in ("", ".", ".."):
        raise SystemExit(f"[error] invalid FAT name: {name!r}")
    base, _, ext = name.rpartition(".")
    if not base:
        base, ext = ext, ""
    return (
        base.upper()[:8].encode("ascii", errors="replace").ljust(8, b" ")
        + ext.upper()[:3].encode("ascii", errors="replace").ljust(3, b" ")
    )


def display_name(raw11: bytes) -> str:
    base = raw11[:8].decode("ascii", errors="replace").rstrip()
    ext = raw11[8:11].decode("ascii", errors="replace").rstrip()
    return base if not ext else f"{base}.{ext}"


@dataclass
class DirEntry:
    index: int
    raw_name: bytes
    attr: int
    cluster: int
    size: int

    @property
    def name(self) -> str:
        return display_name(self.raw_name)


class FatImage:
    def __init__(self, path: Path):
        self.path = path
        self.data = bytearray(path.read_bytes())
        if len(self.data) < SECTOR:
            raise SystemExit(f"[error] image too small: {path}")
        self._parse_bpb()

    def _parse_bpb(self) -> None:
        d = self.data
        if d[510:512] != b"\x55\xaa":
            raise SystemExit(f"[error] not a FAT boot sector: {self.path}")
        self.bytes_per_sector = struct.unpack_from("<H", d, 11)[0]
        if self.bytes_per_sector != SECTOR:
            raise SystemExit("[error] only 512-byte sectors are supported")
        self.sectors_per_cluster = d[13]
        self.reserved = struct.unpack_from("<H", d, 14)[0]
        self.fat_count = d[16]
        self.root_entries = struct.unpack_from("<H", d, 17)[0]
        total16 = struct.unpack_from("<H", d, 19)[0]
        total32 = struct.unpack_from("<I", d, 32)[0]
        self.total_sectors = total16 or total32
        self.sectors_per_fat = struct.unpack_from("<H", d, 22)[0]
        if self.sectors_per_cluster == 0 or self.fat_count == 0 or self.sectors_per_fat == 0:
            raise SystemExit("[error] unsupported FAT BPB")
        self.root_sectors = (self.root_entries * 32 + SECTOR - 1) // SECTOR
        self.fat_lba = self.reserved
        self.root_lba = self.reserved + self.fat_count * self.sectors_per_fat
        self.data_lba = self.root_lba + self.root_sectors
        self.cluster_count = (self.total_sectors - self.data_lba) // self.sectors_per_cluster
        self.fs_type = 12 if self.cluster_count < 4085 else 16
        if self.fs_type not in (12, 16):
            raise SystemExit("[error] unsupported FAT type")

    @property
    def cluster_bytes(self) -> int:
        return self.sectors_per_cluster * SECTOR

    def fat_offset(self, copy: int = 0) -> int:
        return (self.fat_lba + copy * self.sectors_per_fat) * SECTOR

    def root_offset(self) -> int:
        return self.root_lba * SECTOR

    def cluster_offset(self, cluster: int) -> int:
        return (self.data_lba + (cluster - 2) * self.sectors_per_cluster) * SECTOR

    def eoc(self) -> int:
        return 0x0FFF if self.fs_type == 12 else 0xFFFF

    def is_eoc(self, value: int) -> bool:
        return value >= (0x0FF8 if self.fs_type == 12 else 0xFFF8)

    def fat_get(self, cluster: int) -> int:
        fat = self.fat_offset(0)
        if self.fs_type == 16:
            return struct.unpack_from("<H", self.data, fat + cluster * 2)[0]
        off = fat + cluster + cluster // 2
        v = self.data[off] | (self.data[off + 1] << 8)
        return (v >> 4) if cluster & 1 else (v & 0x0FFF)

    def fat_set_one(self, copy: int, cluster: int, value: int) -> None:
        fat = self.fat_offset(copy)
        if self.fs_type == 16:
            struct.pack_into("<H", self.data, fat + cluster * 2, value & 0xFFFF)
            return
        off = fat + cluster + cluster // 2
        v = self.data[off] | (self.data[off + 1] << 8)
        if cluster & 1:
            v = (v & 0x000F) | ((value & 0x0FFF) << 4)
        else:
            v = (v & 0xF000) | (value & 0x0FFF)
        self.data[off] = v & 0xFF
        self.data[off + 1] = (v >> 8) & 0xFF

    def fat_set(self, cluster: int, value: int) -> None:
        for copy in range(self.fat_count):
            self.fat_set_one(copy, cluster, value)

    def entries(self) -> list[DirEntry]:
        out: list[DirEntry] = []
        root = self.root_offset()
        for i in range(self.root_entries):
            off = root + i * 32
            first = self.data[off]
            if first == 0x00:
                break
            if first == 0xE5:
                continue
            attr = self.data[off + 11]
            if attr & 0x08:
                continue
            raw_name = bytes(self.data[off : off + 11])
            cluster = struct.unpack_from("<H", self.data, off + 26)[0]
            size = struct.unpack_from("<I", self.data, off + 28)[0]
            out.append(DirEntry(i, raw_name, attr, cluster, size))
        return out

    def find(self, name: str) -> DirEntry | None:
        raw = split_8_3(name)
        for ent in self.entries():
            if ent.raw_name == raw and (ent.attr & 0x18) == 0:
                return ent
        return None

    def free_slot_index(self) -> int:
        root = self.root_offset()
        for i in range(self.root_entries):
            first = self.data[root + i * 32]
            if first in (0x00, 0xE5):
                return i
        raise SystemExit("[error] root directory is full")

    def cluster_chain(self, start: int) -> list[int]:
        chain: list[int] = []
        cur = start
        limit = self.cluster_count + 2
        while 2 <= cur < limit:
            chain.append(cur)
            nxt = self.fat_get(cur)
            if self.is_eoc(nxt):
                break
            cur = nxt
        return chain

    def read_file(self, name: str) -> bytes:
        ent = self.find(name)
        if ent is None:
            raise SystemExit(f"[error] not found: {name}")
        if ent.size == 0:
            return b""
        buf = bytearray()
        for c in self.cluster_chain(ent.cluster):
            off = self.cluster_offset(c)
            buf.extend(self.data[off : off + self.cluster_bytes])
            if len(buf) >= ent.size:
                break
        return bytes(buf[: ent.size])

    def free_chain(self, start: int) -> None:
        for c in self.cluster_chain(start):
            self.fat_set(c, 0)

    def find_free_clusters(self, count: int) -> list[int]:
        out: list[int] = []
        for c in range(2, self.cluster_count + 2):
            if self.fat_get(c) == 0:
                out.append(c)
                if len(out) == count:
                    return out
        raise SystemExit("[error] disk full")

    def write_file(self, name: str, payload: bytes) -> None:
        old = self.find(name)
        if old is not None:
            if old.cluster:
                self.free_chain(old.cluster)
            slot = old.index
        else:
            slot = self.free_slot_index()

        cluster_count = 0 if len(payload) == 0 else (len(payload) + self.cluster_bytes - 1) // self.cluster_bytes
        chain = self.find_free_clusters(cluster_count) if cluster_count else []
        for i, c in enumerate(chain):
            chunk = payload[i * self.cluster_bytes : (i + 1) * self.cluster_bytes]
            off = self.cluster_offset(c)
            self.data[off : off + self.cluster_bytes] = b"\x00" * self.cluster_bytes
            self.data[off : off + len(chunk)] = chunk
            self.fat_set(c, chain[i + 1] if i + 1 < len(chain) else self.eoc())

        entry = bytearray(32)
        entry[0:11] = split_8_3(name)
        entry[11] = 0x20
        struct.pack_into("<H", entry, 26, chain[0] if chain else 0)
        struct.pack_into("<I", entry, 28, len(payload))
        root = self.root_offset()
        self.data[root + slot * 32 : root + (slot + 1) * 32] = entry

    def delete_file(self, name: str) -> None:
        ent = self.find(name)
        if ent is None:
            raise SystemExit(f"[error] not found: {name}")
        if ent.cluster:
            self.free_chain(ent.cluster)
        self.data[self.root_offset() + ent.index * 32] = 0xE5

    def save(self) -> None:
        self.path.write_bytes(self.data)


@dataclass
class Spec:
    kind: str
    path: Path
    member: str | None = None


def parse_spec(s: str) -> Spec:
    if s.startswith("HOST:"):
        return Spec("host", Path(s[5:]))
    marker = ":/"
    if marker in s:
        image, member = s.split(marker, 1)
        return Spec("image", Path(image), "/" + member)
    return Spec("host", Path(s))


def parse_image_spec(s: str) -> tuple[Path, str]:
    spec = parse_spec(s)
    if spec.kind == "image":
        return spec.path, spec.member or "/"
    return spec.path, "/"


def cmd_create(args: argparse.Namespace) -> int:
    fp = mkfat12._params_fat16(mkfat12._parse_size(args.size))
    image = bytearray(fp.total_sectors * SECTOR)
    image[:SECTOR] = mkfat12.make_boot_sector(fp, args.label, args.oem)
    fat = mkfat12.build_fat(fp, [])
    for i in range(fp.fat_count):
        off = (fp.reserved + i * fp.sectors_per_fat) * SECTOR
        image[off : off + len(fat)] = fat
    Path(args.image).write_bytes(image)
    print(f"[bxos_fat.py] created {args.image} ({fp.total_sectors * SECTOR // 1024} KB, fat16)")
    return 0


def cmd_ls(args: argparse.Namespace) -> int:
    image, _ = parse_image_spec(args.target)
    fat = FatImage(image)
    for ent in fat.entries():
        if (ent.attr & 0x18) == 0:
            print(f"{ent.name:<12} {ent.size:>8}")
    return 0


def cmd_rm(args: argparse.Namespace) -> int:
    image, member = parse_image_spec(args.target)
    fat = FatImage(image)
    fat.delete_file(member)
    fat.save()
    print(f"[bxos_fat.py] removed {image}:{member}")
    return 0


def cmd_cp(args: argparse.Namespace) -> int:
    src = parse_spec(args.src)
    dst = parse_spec(args.dst)
    if src.kind == "host" and dst.kind == "image":
        payload = src.path.read_bytes()
        fat = FatImage(dst.path)
        fat.write_file(dst.member or ("/" + src.path.name), payload)
        fat.save()
        print(f"[bxos_fat.py] copied {src.path} -> {dst.path}:{dst.member}")
        return 0
    if src.kind == "image" and dst.kind == "host":
        fat = FatImage(src.path)
        payload = fat.read_file(src.member or "/")
        dst.path.write_bytes(payload)
        print(f"[bxos_fat.py] copied {src.path}:{src.member} -> {dst.path}")
        return 0
    if src.kind == "image" and dst.kind == "image" and src.path == dst.path:
        fat = FatImage(src.path)
        payload = fat.read_file(src.member or "/")
        fat.write_file(dst.member or "/", payload)
        fat.save()
        print(f"[bxos_fat.py] copied {src.path}:{src.member} -> {dst.member}")
        return 0
    raise SystemExit("[error] cp supports HOST:<file> <image>:/name, <image>:/name HOST:<file>, or same-image copy")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_create = sub.add_parser("create")
    p_create.add_argument("image")
    p_create.add_argument("--size", default="32M")
    p_create.add_argument("--label", default="BXOSDATA")
    p_create.add_argument("--oem", default="HARIBOTE")
    p_create.set_defaults(func=cmd_create)

    p_ls = sub.add_parser("ls")
    p_ls.add_argument("target")
    p_ls.set_defaults(func=cmd_ls)

    p_cp = sub.add_parser("cp")
    p_cp.add_argument("src")
    p_cp.add_argument("dst")
    p_cp.set_defaults(func=cmd_cp)

    p_rm = sub.add_parser("rm")
    p_rm.add_argument("target")
    p_rm.set_defaults(func=cmd_rm)

    args = parser.parse_args(argv[1:])
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
