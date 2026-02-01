# Canonical FAT32 Field Names

A harmonized reference for all on-disk FAT32 structures used by
`SectorWriter`. Each table maps a **canonical name** to the
corresponding specification name from both source documents, the
on-disk offset, size, and a concise description.

Derived from:
- **[MS]** — Microsoft FAT Specification, August 30, 2005
- **[WP]** — Wikipedia, "Design of the FAT file system"
- **[MBR]** — `docs/mbr_x86_design.md`, compiled from
  [OSDev Wiki](https://wiki.osdev.org/MBR_(x86)) and IBM PC/AT
  Technical References

All multi-byte values are **little-endian**. All offsets are
relative to the start of their containing structure unless noted
otherwise.

---

## Table of Contents

1. [Naming Conventions](#naming-conventions)
2. [Sectors and LBA](#sectors-and-lba)
3. [Master Boot Record (MBR)](#master-boot-record-mbr)
4. [Volume Boot Record (VBR)](#volume-boot-record-vbr)
   - [VBR Structure Overview](#vbr-structure-overview)
   - [Common BPB Fields (0x00B–0x023)](#common-bpb-fields)
   - [FAT32 Extended BPB Fields (0x024–0x033)](#fat32-extended-bpb-fields)
   - [FAT32 VBR Fields (0x034–0x059)](#fat32-vbr-fields)
   - [VBR Tail (0x05A–0x1FF)](#vbr-tail)
   - [Backup VBR](#backup-vbr)
5. [FS Information Sector (FSInfo)](#fs-information-sector-fsinfo)
6. [FAT Region](#fat-region)
   - [FAT Region Layout](#fat-region-layout)
   - [Reserved Entries](#reserved-entries)
   - [Data Entries](#data-entries)
7. [Directory Entry](#directory-entry)
   - [File Attributes](#file-attributes)
   - [Date and Time Encoding](#date-and-time-encoding)
8. [Derived Layout Values](#derived-layout-values)
9. [Constants](#constants)
10. [References](#references)

---

## Naming Conventions

Canonical names use a **prefix** identifying which on-disk
structure they belong to, followed by a **camelCase** field name.
The prefixes map to (and replace) the Microsoft specification's
naming convention as follows:

| Prefix | Scope | MS Spec Prefix | Examples |
|--------|-------|----------------|----------|
| `VBR_` | Volume Boot Record fields outside the BPB | `BS_` | `VBR_jmpBoot`, `VBR_oemName` |
| `BPB_` | BIOS Parameter Block fields within the VBR | `BPB_` | `BPB_bytesPerSector`, `BPB_fatSize32` |
| `FSI_` | FS Information Sector fields | `FSI_` | `FSI_freeCount`, `FSI_nextFree` |
| `DIR_` | Directory entry fields | `DIR_` | `DIR_name`, `DIR_attributes` |
| `MBR_` | Master Boot Record fields | *(none in spec)* | `MBR_signature` |
| `PE_`  | Partition Entry fields | *(none in spec)* | `PE_lbaStart`, `PE_type` |

The `BS_` prefix from the Microsoft specification is replaced by
`VBR_` throughout. This avoids visual collision with the `BPB_`
prefix and more clearly names the containing structure — the
Volume Boot Record.

---

## Sectors and LBA

A **sector** is the smallest addressable unit on a block device.
For the SD cards this project targets, a sector is always 512
bytes. Every read or write to the disk operates on whole sectors.

**LBA (Logical Block Addressing)** numbers each sector on the
disk sequentially, starting from zero. LBA 0 is the very first
sector — the Master Boot Record. LBA 1 is the next 512 bytes,
and so on. The byte offset of any sector on disk is simply:

```text
byteOffset = LBA * 512
```

LBA replaced the older **CHS (Cylinder-Head-Sector)** scheme,
which described disk locations using the physical geometry of
spinning platters: cylinder (track), head (platter side), and
sector (arc within a track). CHS could only address about 8 GB
and is obsolete on modern media. The MBR partition table still
contains CHS fields for historical reasons; this project fills
them with `0xFF, 0xFF, 0xFF` to signal "use LBA instead" (a
requirement on macOS).

Two kinds of LBA appear in this document:

- **Absolute LBA** — offset from the start of the disk
  (LBA 0 = MBR). Used by the MBR partition table (`PE_lbaStart`)
  and by `SectorWriter`'s I/O helpers.
- **Volume-relative sector** — offset from the start of the
  partition. Used by BPB fields (`BPB_reservedSectorCount`,
  `BPB_fsInfoSector`, `BPB_backupBootSector`). To convert to
  absolute LBA, add `PE_lbaStart`.

---

## Master Boot Record (MBR)

The MBR occupies **absolute sector 0** of the disk. It contains a
bootstrap code area, four 16-byte partition table entries, and a
2-byte signature. The MBR is not part of any partition — it
precedes the first partition.

| Offset | Size | Canonical Name | Description |
|--------|------|----------------|-------------|
| 0x000 | 446 | `MBR_bootstrap` | Bootstrap code area (zeroed when unused) |
| 0x1BE | 16 | `MBR_partitions[0]` | First partition table entry |
| 0x1CE | 16 | `MBR_partitions[1]` | Second partition table entry |
| 0x1DE | 16 | `MBR_partitions[2]` | Third partition table entry |
| 0x1EE | 16 | `MBR_partitions[3]` | Fourth partition table entry |
| 0x1FE | 2 | `MBR_signature` | Must be `0xAA55` |

### Partition Entry (16 bytes)

| Offset | Size | Canonical Name | Description |
|--------|------|----------------|-------------|
| 0x00 | 1 | `PE_status` | Boot indicator: `0x80` = active, `0x00` = inactive |
| 0x01 | 3 | `PE_chsStart` | CHS of first sector. `0xFF 0xFF 0xFF` for LBA (macOS requires this). |
| 0x04 | 1 | `PE_type` | Partition type: `0x0C` = FAT32 with LBA |
| 0x05 | 3 | `PE_chsEnd` | CHS of last sector. `0xFF 0xFF 0xFF` for LBA. |
| 0x08 | 4 | `PE_lbaStart` | LBA of first sector in partition |
| 0x0C | 4 | `PE_sectorCount` | Number of sectors in partition |

---

## Volume Boot Record (VBR)

### VBR Structure Overview

The **Volume Boot Record** (VBR) is the first sector of the
partition — sector 0 of the volume, located at the absolute LBA
given by `PE_lbaStart`. It is also called the "boot sector" or
"0th sector" in the literature.

The VBR **contains** the BIOS Parameter Block (BPB) as a
sub-structure. The BPB describes the volume's geometry and file
system parameters. The remaining fields of the VBR — the jump
instruction, OEM name, drive number, volume label, file system
type string, boot code, and signature — are part of the boot
sector but are **not** part of the BPB proper.

The Microsoft specification distinguishes these two categories
using naming prefixes:
- `BPB_` — fields within the BIOS Parameter Block
- `BS_` — boot sector fields outside the BPB (renamed to `VBR_`
  in our canonical naming)

```
Sector layout of the VBR (512 bytes):
┌──────────────────────────────────────────────────┐
│ 0x000  VBR_jmpBoot (3 bytes)                     │ VBR
│ 0x003  VBR_oemName (8 bytes)                     │ VBR
├──────────────────────────────────────────────────┤
│ 0x00B  BPB_bytesPerSector ... BPB_totalSectors32 │ BPB (common)
│        (offsets 0x00B – 0x023, 25 bytes)         │
├──────────────────────────────────────────────────┤
│ 0x024  BPB_fatSize32 ... BPB_backupBootSector    │ BPB (FAT32)
│        (offsets 0x024 – 0x033, 16 bytes)         │
├──────────────────────────────────────────────────┤
│ 0x034  BPB_reserved (12 bytes)                   │ BPB (FAT32)
│ 0x040  VBR_driveNumber ... VBR_fsType            │ VBR
│        (offsets 0x040 – 0x059, 26 bytes)         │
├──────────────────────────────────────────────────┤
│ 0x05A  VBR_bootCode (420 bytes)                  │ VBR
│ 0x1FE  VBR_signature (2 bytes)                   │ VBR
└──────────────────────────────────────────────────┘
```

### Common BPB Fields

These fields (offsets 0x00B–0x023) are shared across FAT12, FAT16,
and FAT32. Only FAT32 values are documented here since this project
exclusively formats FAT32.

| Offset | Size | Canonical Name | MS Spec | WP | Description |
|--------|------|----------------|---------|-----|-------------|
| 0x000 | 3 | `VBR_jmpBoot` | `BS_jmpBoot` | `BS_jmpBoot` | Jump instruction to boot code. `0xEB 0x58 0x90` for FAT32. |
| 0x003 | 8 | `VBR_oemName` | `BS_OEMName` | `BS_OEMName` | OEM identifier (e.g., `"MSWIN4.1"`). Informational only. |
| 0x00B | 2 | `BPB_bytesPerSector` | `BPB_BytsPerSec` | `BPB_BytsPerSec` | Bytes per logical sector. Valid: 512, 1024, 2048, 4096. |
| 0x00D | 1 | `BPB_sectorsPerCluster` | `BPB_SecPerClus` | `BPB_SecPerClus` | Sectors per allocation unit. Must be power of 2: 1–128. |
| 0x00E | 2 | `BPB_reservedSectorCount` | `BPB_RsvdSecCnt` | `BPB_RsvdSecCnt` | Reserved sectors including boot sector. FAT32 typical: 32. Must not be 0. |
| 0x010 | 1 | `BPB_fatCount` | `BPB_NumFATs` | `BPB_NumFATs` | Number of FAT copies. Recommended: 2. |
| 0x011 | 2 | `BPB_rootEntryCount` | `BPB_RootEntCnt` | `BPB_RootEntCnt` | Root directory entry count. **Must be 0 for FAT32.** |
| 0x013 | 2 | `BPB_totalSectors16` | `BPB_TotSec16` | `BPB_TotSec16` | 16-bit total sector count. **Must be 0 for FAT32.** |
| 0x015 | 1 | `BPB_mediaDescriptor` | `BPB_Media` | `BPB_Media` | Media type: `0xF8` fixed, `0xF0` removable. |
| 0x016 | 2 | `BPB_fatSize16` | `BPB_FATSz16` | `BPB_FATSz16` | 16-bit FAT size. **Must be 0 for FAT32.** |
| 0x018 | 2 | `BPB_sectorsPerTrack` | `BPB_SecPerTrk` | `BPB_SecPerTrk` | Sectors per track (INT 13h geometry). |
| 0x01A | 2 | `BPB_headCount` | `BPB_NumHeads` | `BPB_NumHeads` | Number of heads (INT 13h geometry). |
| 0x01C | 4 | `BPB_hiddenSectors` | `BPB_HiddSec` | `BPB_HiddSec` | Sectors preceding this partition (= `PE_lbaStart`). |
| 0x020 | 4 | `BPB_totalSectors32` | `BPB_TotSec32` | `BPB_TotSec32` | 32-bit total sector count. Must be non-zero for FAT32. |

### FAT32 Extended BPB Fields

These fields (offsets 0x024–0x033) are unique to FAT32 and extend
the BPB:

| Offset | Size | Canonical Name | MS Spec | WP | Description |
|--------|------|----------------|---------|-----|-------------|
| 0x024 | 4 | `BPB_fatSize32` | `BPB_FATSz32` | `BPB_FATSz32` | 32-bit sectors per FAT. |
| 0x028 | 2 | `BPB_extFlags` | `BPB_ExtFlags` | `BPB_ExtFlags` | Bits 0–3: active FAT (if mirroring disabled). Bit 7: 0 = mirrored, 1 = single active FAT. |
| 0x02A | 2 | `BPB_fsVersion` | `BPB_FSVer` | `BPB_FSVer` | FAT32 version. **Must be 0x0000.** |
| 0x02C | 4 | `BPB_rootCluster` | `BPB_RootClus` | `BPB_RootClus` | First cluster of root directory. Typically 2. |
| 0x030 | 2 | `BPB_fsInfoSector` | `BPB_FSInfo` | `BPB_FSInfo` | Sector number of FSInfo structure. Typically 1. |
| 0x032 | 2 | `BPB_backupBootSector` | `BPB_BkBootSec` | `BPB_BkBootSec` | Sector number of backup boot sector. Typically 6. |
| 0x034 | 12 | `BPB_reserved` | `BPB_Reserved` | `BPB_Reserved` | Reserved. Must be zero. |

### FAT32 VBR Fields

These fields (offsets 0x040–0x059) reside in the VBR but are
**outside** the BPB. The Microsoft specification prefixes them
with `BS_`; we use `VBR_` instead.

| Offset | Size | Canonical Name | MS Spec | WP | Description |
|--------|------|----------------|---------|-----|-------------|
| 0x040 | 1 | `VBR_driveNumber` | `BS_DrvNum` | `BS_DrvNum` | BIOS drive number: `0x80` for hard disk, `0x00` for floppy. |
| 0x041 | 1 | `VBR_reserved1` | `BS_Reserved1` | `BS_Reserved1` | Reserved. Must be zero. |
| 0x042 | 1 | `VBR_bootSignature` | `BS_BootSig` | `BS_BootSig` | Extended boot signature: `0x29` indicates the next three fields are valid. |
| 0x043 | 4 | `VBR_volumeId` | `BS_VolID` | `BS_VolID` | Volume serial number (typically date/time of format). |
| 0x047 | 11 | `VBR_volumeLabel` | `BS_VolLab` | `BS_VolLab` | Volume label (11 bytes, space-padded, uppercase). Must match root directory volume label entry. |
| 0x052 | 8 | `VBR_fsType` | `BS_FilSysType` | `BS_FilSysType` | `"FAT32   "`. **Informational only — never use to determine FAT type.** |

### VBR Tail

| Offset | Size | Canonical Name | MS Spec | WP | Description |
|--------|------|----------------|---------|-----|-------------|
| 0x05A | 420 | `VBR_bootCode` | *(Boot Code)* | *(Boot Code)* | Bootstrap code area. Zeroed when unused. |
| 0x1FE | 2 | `VBR_signature` | `Signature_word` | `Signature_word` | `0x55` at offset 510, `0xAA` at offset 511 (reads as `0xAA55` little-endian). |

### Backup VBR

FAT32 requires a backup copy of the Volume Boot Record to guard
against catastrophic data loss if sector 0 becomes unreadable.

Key facts from the Microsoft specification:

- The FAT32 "boot sector" is actually **three sectors** long
  (sectors 0–2 of the reserved region).
- A complete copy of all three sectors resides at the sector
  given by `BPB_backupBootSector` (typically **sector 6**, so
  sectors 6–8).
- The `BPB_backupBootSector` field contains the value **6** in
  **both** the primary (sector 0) and backup (sector 6) copies.
- A backup copy of the FSInfo sector also exists at **sector 7**
  (one sector after the backup boot sector), though
  `BPB_fsInfoSector` points to the same primary FSInfo sector
  (sector 1) in both VBR copies.
- Volume repair utilities should retrieve BPB contents from
  sector 6 when sector 0 is unreadable.

```
Reserved region layout (first 32 sectors):
┌──────────────────────────────────────────────┐
│ Sector 0   Primary VBR (boot sector)         │
│ Sector 1   Primary FSInfo                    │
│ Sector 2   Primary VBR continuation          │
│ Sectors 3–5  (unused, zeroed)                │
│ Sector 6   Backup VBR (boot sector copy)     │
│ Sector 7   Backup FSInfo (copy)              │
│ Sector 8   Backup VBR continuation           │
│ Sectors 9–31  (unused, zeroed)               │
└──────────────────────────────────────────────┘
```

---

## FS Information Sector (FSInfo)

The FSInfo sector resides at the sector specified by
`BPB_fsInfoSector` (typically sector 1) within the reserved
region. It caches free cluster information to accelerate
allocation. A backup copy exists at sector 7.

| Offset | Size | Canonical Name | MS Spec | WP | Description |
|--------|------|----------------|---------|-----|-------------|
| 0x000 | 4 | `FSI_leadSignature` | `FSI_LeadSig` | `FSI_LeadSig` | `0x41615252` ("RRaA") |
| 0x004 | 480 | `FSI_reserved1` | `FSI_Reserved1` | `FSI_Reserved1` | Reserved. Must be zero. |
| 0x1E4 | 4 | `FSI_structSignature` | `FSI_StrucSig` | `FSI_StrucSig` | `0x61417272` ("rrAa") |
| 0x1E8 | 4 | `FSI_freeCount` | `FSI_Free_Count` | `FSI_Free_Count` | Last known free cluster count. `0xFFFFFFFF` = unknown. |
| 0x1EC | 4 | `FSI_nextFree` | `FSI_Nxt_Free` | `FSI_Nxt_Free` | Hint for next free cluster. `0xFFFFFFFF` = unknown. |
| 0x1F0 | 12 | `FSI_reserved2` | `FSI_Reserved2` | `FSI_Reserved2` | Reserved. Must be zero. |
| 0x1FC | 4 | `FSI_trailSignature` | `FSI_TrailSig` | `FSI_TrailSig` | `0xAA550000` |

**Caution**: These values are advisory only. Implementations
should validate against the actual FAT on mount.

---

## FAT Region

The File Allocation Table is a flat array of 32-bit entries (only
the low 28 bits are used) mapping cluster numbers to allocation
state. The FAT region begins immediately after the reserved
region.

### FAT Region Layout

`BPB_fatCount` (typically 2) determines how many copies of the
FAT are stored. The first copy — the **primary FAT** — begins
at `fatStartSector`. The second copy — the **backup FAT** —
follows immediately after, at
`fatStartSector + fatSizeSectors`. Both copies are identical in
content and size.

The backup FAT exists for data recovery: if the primary FAT
becomes corrupted, repair utilities can restore it from the
backup. During normal operation the two copies are kept in sync
(mirrored). The `BPB_extFlags` field controls mirroring: when
bit 7 is clear (the default, and the only mode this project
uses), all FAT copies are mirrored on every write. When bit 7
is set, only the FAT indicated by bits 0–3 is active and the
other copies are stale.

```text
FAT region layout (absolute LBAs):
┌──────────────────────────────────────────────────────┐
│ fatStartSector         Primary FAT                   │
│   (fatSizeSectors sectors)                           │
├──────────────────────────────────────────────────────┤
│ fatStartSector         Backup FAT (identical copy)   │
│  + fatSizeSectors                                    │
│   (fatSizeSectors sectors)                           │
├──────────────────────────────────────────────────────┤
│ dataStartSector        First data cluster (cluster 2)│
└──────────────────────────────────────────────────────┘
```

### Reserved Entries

The first two entries (clusters 0 and 1) are reserved and set
during format:

| Entry | Canonical Name | Value | Description |
|-------|----------------|-------|-------------|
| FAT[0] | `FAT_mediaEntry` | `0x0FFFFFF8` | Media descriptor (`0xF8`) in low byte, remaining bits set to 1. |
| FAT[1] | `FAT_eocEntry` | `0xFFFFFFFF` | End-of-chain marker with dirty volume flags in high bits. |

**Dirty volume flags** in FAT[1]:

| Bit | Mask | Meaning when clear |
|-----|------|--------------------|
| 27 | `0x08000000` | Volume was not properly unmounted ("dirty") |
| 26 | `0x04000000` | Disk I/O errors encountered |

Setting both bits to 1 (as in `0xFFFFFFFF`) indicates a clean
volume with no errors.

### Data Entries

For cluster N (where N >= 2):

| FAT32 Value | Meaning |
|-------------|---------|
| `0x00000000` | Free cluster |
| `0x00000002` – `0x0FFFFFF5` | Allocated; value is next cluster number |
| `0x0FFFFFF6` | Reserved |
| `0x0FFFFFF7` | Bad cluster |
| `0x0FFFFFF8` – `0x0FFFFFFF` | End of chain (EOF) |

**Critical**: The high 4 bits of each FAT32 entry are reserved.
They must be preserved when modifying entries, except during
initial format when the entire FAT is zeroed first.

---

## Directory Entry

A 32-byte structure describing a file, subdirectory, or volume
label. The root directory's first entry is typically the volume
label.

| Offset | Size | Canonical Name | MS Spec | WP | Description |
|--------|------|----------------|---------|-----|-------------|
| 0x00 | 11 | `DIR_name` | `DIR_Name` | `DIR_Name` | Short name: 8-char base + 3-char extension, space-padded, uppercase. |
| 0x0B | 1 | `DIR_attributes` | `DIR_Attr` | `DIR_Attr` | File attribute bitmask (see below). |
| 0x0C | 1 | `DIR_ntReserved` | `DIR_NTRes` | `DIR_NTRes` | Reserved for Windows NT lowercase flags. Must be 0. |
| 0x0D | 1 | `DIR_creationTimeTenths` | `DIR_CrtTimeTenth` | `DIR_CrtTimeTenth` | Creation time sub-second (0–199 = 0–1.99 s). |
| 0x0E | 2 | `DIR_creationTime` | `DIR_CrtTime` | `DIR_CrtTime` | Creation time (2-second granularity). |
| 0x10 | 2 | `DIR_creationDate` | `DIR_CrtDate` | `DIR_CrtDate` | Creation date. |
| 0x12 | 2 | `DIR_lastAccessDate` | `DIR_LstAccDate` | `DIR_LstAccDate` | Last access date. |
| 0x14 | 2 | `DIR_firstClusterHigh` | `DIR_FstClusHI` | `DIR_FstClusHI` | High 16 bits of first data cluster. |
| 0x16 | 2 | `DIR_writeTime` | `DIR_WrtTime` | `DIR_WrtTime` | Last modification time. |
| 0x18 | 2 | `DIR_writeDate` | `DIR_WrtDate` | `DIR_WrtDate` | Last modification date. |
| 0x1A | 2 | `DIR_firstClusterLow` | `DIR_FstClusLO` | `DIR_FstClusLO` | Low 16 bits of first data cluster. |
| 0x1C | 4 | `DIR_fileSize` | `DIR_FileSize` | `DIR_FileSize` | File size in bytes. Must be 0 for directories. |

### Special Values in DIR_name[0]

| Value | Meaning |
|-------|---------|
| `0x00` | Entry is free; **all following entries are also free** |
| `0x05` | First character is actually `0xE5` (Kanji compatibility) |
| `0x2E` | Dot entry: `.` (current directory) or `..` (parent) |
| `0xE5` | Entry has been deleted (available for reuse) |

### File Attributes

The `DIR_attributes` byte is a bitmask:

| Bit | Mask | Canonical Name | Description |
|-----|------|----------------|-------------|
| 0 | `0x01` | `ATTR_READ_ONLY` | File is read-only |
| 1 | `0x02` | `ATTR_HIDDEN` | File is hidden |
| 2 | `0x04` | `ATTR_SYSTEM` | System file |
| 3 | `0x08` | `ATTR_VOLUME_ID` | Volume label entry |
| 4 | `0x10` | `ATTR_DIRECTORY` | Subdirectory |
| 5 | `0x20` | `ATTR_ARCHIVE` | Modified since last backup |
| 6–7 | `0xC0` | *(reserved)* | Must be 0 |

The combination `0x0F` (`ATTR_READ_ONLY | ATTR_HIDDEN |
ATTR_SYSTEM | ATTR_VOLUME_ID`) indicates a VFAT long filename
entry (`ATTR_LONG_NAME`).

### Date Encoding (16-bit word)

| Bits | Field | Range |
|------|-------|-------|
| 0–4 | Day | 1–31 |
| 5–8 | Month | 1–12 |
| 9–15 | Year | 0–127 (relative to 1980, so 1980–2107) |

### Time Encoding (16-bit word)

| Bits | Field | Range |
|------|-------|-------|
| 0–4 | Seconds / 2 | 0–29 (representing 0–58 seconds) |
| 5–10 | Minutes | 0–59 |
| 11–15 | Hours | 0–23 |

Granularity: 2 seconds. The `DIR_creationTimeTenths` field adds
sub-second resolution (0–199 = 0–1.99 s) for creation time only.

---

## Derived Layout Values

These values are computed from BPB fields during formatting. They
appear as `SectorWriter` members and are used to locate structures
on disk.

| Canonical Name | Formula | Description |
|----------------|---------|-------------|
| `partitionStart` | `kPartitionAlignmentSectors` | Absolute LBA of partition start (= `PE_lbaStart`). |
| `partitionSectorCount` | `diskSectorCount - partitionStart` | Total sectors in the partition (= `PE_sectorCount`). |
| `fatSizeSectors` | See note 1 | Sectors per FAT (= `BPB_fatSize32`). |
| `fatStartSector` | `partitionStart + BPB_reservedSectorCount` | Absolute LBA of first FAT. |
| `dataStartSector` | `fatStartSector + (BPB_fatCount * fatSizeSectors)` | Absolute LBA of first data cluster. |
| `totalDataSectors` | `partitionSectorCount - BPB_reservedSectorCount - (BPB_fatCount * fatSizeSectors)` | Sectors available for file data. |
| `totalClusters` | `totalDataSectors / BPB_sectorsPerCluster` | Total allocatable clusters. |
| `freeClusterCount` | `totalClusters - 1` | Free clusters (minus root directory cluster). |

**Note 1** — FAT size calculation from the Microsoft
specification, with the original variable names replaced by
descriptive ones:

```text
sectorsToAllocate = partitionSectorCount - BPB_reservedSectorCount
sectorsPerFatEntry = (256 * BPB_sectorsPerCluster + BPB_fatCount) / 2
fatSizeSectors = ceil(sectorsToAllocate / sectorsPerFatEntry)
```

The ceiling division is implemented as integer arithmetic:

```text
fatSizeSectors = (sectorsToAllocate + (sectorsPerFatEntry - 1))
                / sectorsPerFatEntry
```

**Why 256, and why divide by 2?** The formula is written to
handle both FAT16 and FAT32 with the same structure. The
constant **256** is the number of FAT16 entries that fit in
one 512-byte sector (`bytesPerSector / sizeof(FAT16 entry)`
= 512 / 2 = 256). So for FAT16, one FAT sector covers
`256 * sectorsPerCluster` data sectors. The **`/ 2`** adapts
this to FAT32: a FAT32 entry is 4 bytes — twice the size of a
FAT16 entry — so half as many entries fit per sector. Dividing
by 2 converts the FAT16 entry density to FAT32 entry density:
256 / 2 = 128 entries per sector, which is indeed
`bytesPerSector / sizeof(FAT32 entry)` = 512 / 4 = 128.

The `+ BPB_fatCount` term accounts for the fact that each
additional FAT sector must be replicated across all FAT copies.
Adding one sector to each of `fatCount` FATs consumes
`fatCount` sectors of the partition, slightly reducing the
number of data sectors that need FAT entries. Because the
`/ 2` is applied to the entire expression
`(256 * sectorsPerCluster + fatCount)` rather than just the
`256 * sectorsPerCluster` term, the correction is not
mathematically exact — it makes the denominator one smaller
than the precise value `(128 * sectorsPerCluster + fatCount)`.
The result is a safe over-estimate: the FAT may be up to about
8 sectors larger than strictly necessary, but never too small.

---

## Constants

Fixed values used throughout the implementation:

| Canonical Name | Value | Description |
|----------------|-------|-------------|
| `kSectorSize` | 512 | Bytes per sector |
| `kSectorsPerCluster` | 64 | Sectors per cluster (= 32 KB) |
| `kClusterSize` | 32,768 | Bytes per cluster |
| `kPartitionAlignmentSectors` | 8,192 | Partition alignment (= 4 MB for NAND flash) |
| `kReservedSectors` | 32 | Reserved sector count (FAT32 standard) |
| `kFatCount` | 2 | Number of FAT copies |
| `kRootCluster` | 2 | First cluster of root directory |
| `kMediaDescriptor` | `0xF8` | Fixed disk media type |
| `kFsInfoSector` | 1 | FSInfo sector within reserved region |
| `kBackupBootSector` | 6 | Backup VBR sector within reserved region |
| `kPartitionTypeFat32Lba` | `0x0C` | MBR partition type for FAT32 with LBA |
| `kMbrSignature` | `0xAA55` | MBR signature |
| `kVbrSignature` | `0xAA55` | VBR signature (same value, different structure) |
| `kAttrVolumeId` | `0x08` | Volume label attribute |

---

## References

1. Microsoft Corporation. "Microsoft FAT Specification."
   August 30, 2005. Contributed to SD Card Association under
   SDA IPR Policy.

2. Wikipedia. "Design of the FAT file system."
   https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system

---

*Canonical names use `PREFIX_camelCase` for struct fields and
`kPascalCase` for constants, matching the project's C++ naming
conventions. The `BS_` prefix from the Microsoft specification
is replaced by `VBR_` throughout.*
