# Design of the FAT File System

A comprehensive technical reference for the File Allocation Table (FAT) file system architecture.

---

## Table of Contents

1. [Overview](#overview)
2. [Volume Structure](#volume-structure)
3. [Boot Sector and BIOS Parameter Block (BPB)](#boot-sector-and-bios-parameter-block-bpb)
   - [Common BPB Fields](#common-bpb-fields-fat12fat16fat32)
   - [Extended BPB for FAT12/FAT16](#extended-bpb-for-fat12fat16)
   - [Extended BPB for FAT32](#extended-bpb-for-fat32)
4. [File Allocation Table](#file-allocation-table)
   - [FAT Entry Structure](#fat-entry-structure)
   - [Reserved FAT Entries](#reserved-fat-entries)
   - [Cluster Chain Traversal](#cluster-chain-traversal)
5. [FS Information Sector (FAT32)](#fs-information-sector-fat32)
6. [Directory Structure](#directory-structure)
   - [Directory Entry Format](#directory-entry-format)
   - [File Attributes](#file-attributes)
   - [Date and Time Encoding](#date-and-time-encoding)
7. [Long File Name (VFAT) Support](#long-file-name-vfat-support)
8. [FAT Type Determination](#fat-type-determination)
9. [Cluster and Sector Calculations](#cluster-and-sector-calculations)
10. [File System Limits](#file-system-limits)
11. [References](#references)

---

## Overview

The FAT (File Allocation Table) file system was originally developed for MS-DOS and has evolved through several variants: FAT12, FAT16, and FAT32. Despite its age, FAT remains ubiquitous due to its simplicity and near-universal support across operating systems, embedded systems, and removable media.

The distinguishing characteristic of each variant is the bit-width of entries in the File Allocation Table:

| Variant | FAT Entry Size | Maximum Clusters | Typical Use Case |
|---------|---------------|------------------|------------------|
| FAT12   | 12 bits       | 4,084            | Floppy disks, small volumes |
| FAT16   | 16 bits       | 65,524           | Small to medium hard disk partitions |
| FAT32   | 28 bits (of 32) | 268,435,444    | Large volumes up to ~2 TB |

All FAT variants use **little-endian** byte ordering for multi-byte fields.

---

## Volume Structure

A FAT volume is divided into four contiguous regions, laid out in this order:

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Reserved Region                              │
│  (Boot Sector, BPB, optional FSInfo sector for FAT32)               │
├─────────────────────────────────────────────────────────────────────┤
│                           FAT Region                                 │
│  (Primary FAT, optional backup FAT copy/copies)                     │
├─────────────────────────────────────────────────────────────────────┤
│                    Root Directory Region                             │
│  (FAT12/FAT16 only - fixed size, does not exist on FAT32)          │
├─────────────────────────────────────────────────────────────────────┤
│                    Data Region (Clusters)                            │
│  (Files, subdirectories, and FAT32 root directory)                  │
└─────────────────────────────────────────────────────────────────────┘
```

| Region | Description | Notes |
|--------|-------------|-------|
| Reserved Region | Contains boot sector with BPB; for FAT32, includes FSInfo sector and backup boot sector | Sector count specified in `BPB_RsvdSecCnt` |
| FAT Region | One or more copies of the File Allocation Table | Typically 2 copies for redundancy |
| Root Directory Region | Fixed-size root directory (FAT12/FAT16 only) | Does not exist on FAT32 |
| Data Region | File and directory data organized into clusters | First data cluster is cluster #2 |

---

## Boot Sector and BIOS Parameter Block (BPB)

The boot sector resides at logical sector 0 of the volume. It contains the BIOS Parameter Block (BPB), which describes the volume's geometry and file system parameters.

### Common BPB Fields (FAT12/FAT16/FAT32)

These fields are present in all FAT variants and occupy the first 36 bytes of the boot sector:

| Offset | Size | Field Name | Description |
|--------|------|------------|-------------|
| 0x000 | 3 | `BS_jmpBoot` | Jump instruction to boot code. Must be `0xEB xx 0x90` (short jump + NOP) or `0xE9 xx xx` (near jump). |
| 0x003 | 8 | `BS_OEMName` | OEM identifier string (e.g., "MSDOS5.0", "mkdosfs"). Informational only. |
| 0x00B | 2 | `BPB_BytsPerSec` | Bytes per logical sector. Valid values: 512, 1024, 2048, 4096. |
| 0x00D | 1 | `BPB_SecPerClus` | Sectors per cluster. Must be a power of 2: 1, 2, 4, 8, 16, 32, 64, or 128. |
| 0x00E | 2 | `BPB_RsvdSecCnt` | Number of reserved sectors (including boot sector). FAT12/16: typically 1. FAT32: typically 32. |
| 0x010 | 1 | `BPB_NumFATs` | Number of FAT copies. Typically 2 for redundancy. |
| 0x011 | 2 | `BPB_RootEntCnt` | Number of 32-byte root directory entries (FAT12/16). Must be 0 for FAT32. Typical: 512 for FAT16. |
| 0x013 | 2 | `BPB_TotSec16` | Total sectors (16-bit). If 0, use `BPB_TotSec32`. Must be 0 for FAT32. |
| 0x015 | 1 | `BPB_Media` | Media descriptor byte. 0xF8 for fixed disks, 0xF0 for removable media. |
| 0x016 | 2 | `BPB_FATSz16` | Sectors per FAT (FAT12/16). Must be 0 for FAT32 (use `BPB_FATSz32`). |
| 0x018 | 2 | `BPB_SecPerTrk` | Sectors per track (for INT 13h geometry). |
| 0x01A | 2 | `BPB_NumHeads` | Number of heads (for INT 13h geometry). |
| 0x01C | 4 | `BPB_HiddSec` | Hidden sectors preceding this volume (partition offset). |
| 0x020 | 4 | `BPB_TotSec32` | Total sectors (32-bit). Used if `BPB_TotSec16` is 0. |

### Extended BPB for FAT12/FAT16

Starting at offset 0x024 (after the common BPB), FAT12/FAT16 volumes have:

| Offset | Size | Field Name | Description |
|--------|------|------------|-------------|
| 0x024 | 1 | `BS_DrvNum` | BIOS drive number (0x00 for floppy, 0x80 for hard disk). |
| 0x025 | 1 | `BS_Reserved1` | Reserved (used by Windows NT for dirty volume flags). |
| 0x026 | 1 | `BS_BootSig` | Extended boot signature. 0x29 indicates the following three fields are valid. 0x28 indicates only `BS_VolID` is valid. |
| 0x027 | 4 | `BS_VolID` | Volume serial number (typically date/time of format). |
| 0x02B | 11 | `BS_VolLab` | Volume label, padded with spaces (e.g., "NO NAME    "). |
| 0x036 | 8 | `BS_FilSysType` | File system type string: "FAT12   ", "FAT16   ", or "FAT     ". **Informational only—do not use to determine FAT type.** |
| 0x03E | 448 | Boot Code | Bootstrap code area. |
| 0x1FE | 2 | `Signature_word` | Boot sector signature: 0x55 at offset 510, 0xAA at offset 511. |

### Extended BPB for FAT32

FAT32 inserts additional fields before the extended boot signature:

| Offset | Size | Field Name | Description |
|--------|------|------------|-------------|
| 0x024 | 4 | `BPB_FATSz32` | Sectors per FAT (32-bit). `BPB_FATSz16` must be 0. |
| 0x028 | 2 | `BPB_ExtFlags` | Extended flags. Bits 0-3: active FAT (if mirroring disabled). Bit 7: 0=mirrored, 1=single active FAT. |
| 0x02A | 2 | `BPB_FSVer` | FAT32 version (high byte = major, low byte = minor). Must be 0x0000. |
| 0x02C | 4 | `BPB_RootClus` | First cluster of root directory (typically 2). |
| 0x030 | 2 | `BPB_FSInfo` | Sector number of FSInfo structure (typically 1). |
| 0x032 | 2 | `BPB_BkBootSec` | Sector number of backup boot sector (typically 6). 0 = no backup. |
| 0x034 | 12 | `BPB_Reserved` | Reserved, must be 0. |
| 0x040 | 1 | `BS_DrvNum` | BIOS drive number. |
| 0x041 | 1 | `BS_Reserved1` | Reserved (dirty volume flags on some implementations). |
| 0x042 | 1 | `BS_BootSig` | Extended boot signature (0x29 or 0x28). |
| 0x043 | 4 | `BS_VolID` | Volume serial number. |
| 0x047 | 11 | `BS_VolLab` | Volume label. |
| 0x052 | 8 | `BS_FilSysType` | "FAT32   " (informational only). |
| 0x05A | 420 | Boot Code | Bootstrap code area. |
| 0x1FE | 2 | `Signature_word` | 0x55 0xAA |

---

## File Allocation Table

The FAT is the heart of the file system—a flat array that maps cluster numbers to either:
- The next cluster in a file's chain
- An end-of-chain marker
- A bad cluster marker
- Zero (free cluster)

### FAT Entry Structure

#### FAT12 Entry Layout

FAT12 packs two 12-bit entries into three bytes. Given cluster index N:

```
If N is even:
    Entry = (byte[1] & 0x0F) << 8 | byte[0]
If N is odd:
    Entry = byte[2] << 4 | (byte[1] >> 4)
```

Example layout (first 16 bytes, clusters 0-9):

| Byte | Clusters Stored |
|------|-----------------|
| 0-2  | Cluster 0 (low 8 bits in byte 0, high 4 bits in low nibble of byte 1), Cluster 1 (high nibble of byte 1 + byte 2) |
| 3-5  | Clusters 2 and 3 |
| ... | ... |

#### FAT16 Entry Layout

Each entry is a 16-bit little-endian word:

| Offset | Cluster |
|--------|---------|
| 0x0000 | Cluster 0 (reserved) |
| 0x0002 | Cluster 1 (reserved) |
| 0x0004 | Cluster 2 |
| 0x0006 | Cluster 3 |
| ... | ... |

#### FAT32 Entry Layout

Each entry is a 32-bit little-endian double-word, but only the low 28 bits are used:

| Offset | Cluster |
|--------|---------|
| 0x0000 | Cluster 0 (reserved) |
| 0x0004 | Cluster 1 (reserved) |
| 0x0008 | Cluster 2 |
| 0x000C | Cluster 3 |
| ... | ... |

**Important**: The high 4 bits of FAT32 entries are reserved and must be preserved when modifying entries.

### FAT Entry Values

| FAT12 | FAT16 | FAT32 | Meaning |
|-------|-------|-------|---------|
| 0x000 | 0x0000 | 0x00000000 | Free cluster |
| 0x002–0xFF5 | 0x0002–0xFFF5 | 0x00000002–0x0FFFFFF5 | Allocated; value is next cluster number |
| 0xFF6 | 0xFFF6 | 0x0FFFFFF6 | Reserved |
| 0xFF7 | 0xFFF7 | 0x0FFFFFF7 | Bad cluster |
| 0xFF8–0xFFF | 0xFFF8–0xFFFF | 0x0FFFFFF8–0x0FFFFFFF | End of chain (EOF) |

### Reserved FAT Entries

**FAT[0]** (Cluster 0): Contains the media descriptor in the low byte, with remaining bits set to 1:
- FAT12: `0x0FF8` if media is 0xF8
- FAT16: `0xFFF8` if media is 0xF8  
- FAT32: `0x0FFFFFF8` if media is 0xF8

**FAT[1]** (Cluster 1): Nominally stores the end-of-chain marker. On FAT16/FAT32, the high bits may be used as volume status flags:

| Variant | Bit | Meaning when clear |
|---------|-----|-------------------|
| FAT16 | 15 | Volume was not properly unmounted ("dirty") |
| FAT16 | 14 | Disk I/O errors encountered |
| FAT32 | 27 | Volume was not properly unmounted |
| FAT32 | 26 | Disk I/O errors encountered |

### Cluster Chain Traversal

To read a file, start with the first cluster number from the directory entry, then follow the chain:

```
current_cluster = directory_entry.first_cluster;
while (current_cluster < END_OF_CHAIN_MARKER) {
    read_cluster_data(current_cluster);
    current_cluster = FAT[current_cluster];
}
```

---

## FS Information Sector (FAT32)

FAT32 volumes typically include an FS Information Sector at the sector specified by `BPB_FSInfo` (usually sector 1). This structure caches free cluster information to speed up allocation:

| Offset | Size | Field Name | Description |
|--------|------|------------|-------------|
| 0x000 | 4 | `FSI_LeadSig` | Lead signature: 0x41615252 ("RRaA") |
| 0x004 | 480 | `FSI_Reserved1` | Reserved (must be 0) |
| 0x1E4 | 4 | `FSI_StrucSig` | Structure signature: 0x61417272 ("rrAa") |
| 0x1E8 | 4 | `FSI_Free_Count` | Last known free cluster count. 0xFFFFFFFF if unknown. |
| 0x1EC | 4 | `FSI_Nxt_Free` | Hint for next free cluster (where to start searching). 0xFFFFFFFF if unknown. |
| 0x1F0 | 12 | `FSI_Reserved2` | Reserved (must be 0) |
| 0x1FC | 4 | `FSI_TrailSig` | Trail signature: 0xAA550000 |

**Caution**: The values in this structure are advisory only. They may be stale if the volume was not properly unmounted. Implementations should validate these values against the actual FAT on mount.

---

## Directory Structure

Directories are special files containing 32-byte entries that describe files and subdirectories.

### Directory Entry Format

| Offset | Size | Field Name | Description |
|--------|------|------------|-------------|
| 0x00 | 8 | `DIR_Name` | Short name (8 characters, space-padded) |
| 0x08 | 3 | `DIR_Ext` | Extension (3 characters, space-padded) |
| 0x0B | 1 | `DIR_Attr` | File attributes |
| 0x0C | 1 | `DIR_NTRes` | Reserved for Windows NT (lowercase flags) |
| 0x0D | 1 | `DIR_CrtTimeTenth` | Creation time, tenths of seconds (0–199) |
| 0x0E | 2 | `DIR_CrtTime` | Creation time |
| 0x10 | 2 | `DIR_CrtDate` | Creation date |
| 0x12 | 2 | `DIR_LstAccDate` | Last access date |
| 0x14 | 2 | `DIR_FstClusHI` | High 16 bits of first cluster (FAT32 only; 0 for FAT12/16) |
| 0x16 | 2 | `DIR_WrtTime` | Last modification time |
| 0x18 | 2 | `DIR_WrtDate` | Last modification date |
| 0x1A | 2 | `DIR_FstClusLO` | Low 16 bits of first cluster |
| 0x1C | 4 | `DIR_FileSize` | File size in bytes (0 for directories) |

#### Special Values in DIR_Name[0]

| Value | Meaning |
|-------|---------|
| 0x00 | Entry is free; all following entries are also free |
| 0x05 | First character is actually 0xE5 (KANJI compatibility) |
| 0x2E | Dot entry: "." (current directory) or ".." (parent directory) |
| 0xE5 | Entry has been deleted (available for reuse) |

### File Attributes

The `DIR_Attr` byte is a bitmask:

| Bit | Mask | Name | Description |
|-----|------|------|-------------|
| 0 | 0x01 | ATTR_READ_ONLY | File is read-only |
| 1 | 0x02 | ATTR_HIDDEN | File is hidden |
| 2 | 0x04 | ATTR_SYSTEM | File is a system file |
| 3 | 0x08 | ATTR_VOLUME_ID | Entry is the volume label |
| 4 | 0x10 | ATTR_DIRECTORY | Entry is a subdirectory |
| 5 | 0x20 | ATTR_ARCHIVE | File has been modified since last backup |
| 6-7 | 0xC0 | Reserved | Must be 0 |

The combination `ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID` (0x0F) indicates a **long filename entry** (see VFAT section).

### Date and Time Encoding

**Date Format** (16-bit word):

| Bits | Field | Range |
|------|-------|-------|
| 0–4 | Day | 1–31 |
| 5–8 | Month | 1–12 |
| 9–15 | Year | 0–127 (relative to 1980, so 1980–2107) |

**Time Format** (16-bit word):

| Bits | Field | Range |
|------|-------|-------|
| 0–4 | Seconds / 2 | 0–29 (representing 0–58 seconds) |
| 5–10 | Minutes | 0–59 |
| 11–15 | Hours | 0–23 |

Granularity: 2 seconds for time fields. The `DIR_CrtTimeTenth` field provides additional resolution (0–199 representing 0–1.99 seconds) for creation time only.

---

## Long File Name (VFAT) Support

VFAT (Virtual FAT) extends the FAT directory structure to support long file names (up to 255 Unicode characters) while maintaining backward compatibility with 8.3 short names.

### LFN Directory Entry Format

Long names are stored in one or more 32-byte "fake" directory entries immediately preceding the standard 8.3 entry. Each LFN entry stores 13 Unicode characters:

| Offset | Size | Field Name | Description |
|--------|------|------------|-------------|
| 0x00 | 1 | `LDIR_Ord` | Sequence number (1-based). Bit 6 (0x40) set on last (first stored) entry. |
| 0x01 | 10 | `LDIR_Name1` | Characters 1–5 (Unicode, 2 bytes each) |
| 0x0B | 1 | `LDIR_Attr` | Attributes = 0x0F (ATTR_LONG_NAME) |
| 0x0C | 1 | `LDIR_Type` | Must be 0 |
| 0x0D | 1 | `LDIR_Chksum` | Checksum of short name |
| 0x0E | 12 | `LDIR_Name2` | Characters 6–11 (Unicode) |
| 0x1A | 2 | `LDIR_FstClusLO` | Must be 0 |
| 0x1C | 4 | `LDIR_Name3` | Characters 12–13 (Unicode) |

### LFN Storage Order

LFN entries are stored in **reverse order**—the last portion of the name appears first on disk:

```
Entry with LDIR_Ord = 0x43  (last entry, contains chars 27-39)
Entry with LDIR_Ord = 0x02  (contains chars 14-26)
Entry with LDIR_Ord = 0x01  (contains chars 1-13)
Standard 8.3 directory entry
```

### Checksum Calculation

The checksum protects against orphaned LFN entries. It's computed from the 11-byte short name:

```c
uint8_t lfn_checksum(const uint8_t *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i];
    }
    return sum;
}
```

---

## FAT Type Determination

**The FAT type is determined solely by the count of data clusters—not by any string field or BPB signature.**

### Calculation Algorithm

```c
// Step 1: Calculate root directory sectors (0 for FAT32)
uint32_t RootDirSectors = ((BPB_RootEntCnt * 32) + (BPB_BytsPerSec - 1)) 
                          / BPB_BytsPerSec;

// Step 2: Determine FAT size
uint32_t FATSz = (BPB_FATSz16 != 0) ? BPB_FATSz16 : BPB_FATSz32;

// Step 3: Determine total sectors
uint32_t TotSec = (BPB_TotSec16 != 0) ? BPB_TotSec16 : BPB_TotSec32;

// Step 4: Calculate data sectors
uint32_t DataSec = TotSec - (BPB_RsvdSecCnt + (BPB_NumFATs * FATSz) 
                   + RootDirSectors);

// Step 5: Calculate cluster count
uint32_t CountOfClusters = DataSec / BPB_SecPerClus;

// Step 6: Determine FAT type
if (CountOfClusters < 4085) {
    // FAT12
} else if (CountOfClusters < 65525) {
    // FAT16
} else {
    // FAT32
}
```

### Cluster Count Boundaries

| FAT Type | Cluster Count Range |
|----------|-------------------|
| FAT12 | 1 – 4,084 |
| FAT16 | 4,085 – 65,524 |
| FAT32 | 65,525 – 268,435,444 |

**Recommendation**: Avoid creating volumes with cluster counts exactly at these boundaries (4,085 or 65,525) to prevent edge-case bugs in implementations.

---

## Cluster and Sector Calculations

### First Sector of Data Region

```c
uint32_t FirstDataSector = BPB_RsvdSecCnt 
                         + (BPB_NumFATs * FATSz) 
                         + RootDirSectors;
```

### First Sector of a Cluster

Given cluster number N (where N ≥ 2):

```c
uint32_t FirstSectorOfCluster(uint32_t N) {
    return ((N - 2) * BPB_SecPerClus) + FirstDataSector;
}
```

### FAT Entry Location

Given cluster number N:

```c
// For FAT16:
uint32_t FATOffset = N * 2;
uint32_t ThisFATSecNum = BPB_RsvdSecCnt + (FATOffset / BPB_BytsPerSec);
uint32_t ThisFATEntOffset = FATOffset % BPB_BytsPerSec;

// For FAT32:
uint32_t FATOffset = N * 4;
// Same sector/offset calculation as FAT16

// For FAT12 (more complex due to 1.5-byte entries):
uint32_t FATOffset = N + (N / 2);  // N * 1.5, integer math
// May span sector boundary—handle appropriately
```

---

## File System Limits

### Capacity Limits by FAT Type

| Parameter | FAT12 | FAT16 | FAT32 |
|-----------|-------|-------|-------|
| Max clusters | 4,084 | 65,524 | 268,435,444 |
| FAT entry size | 12 bits | 16 bits | 28 bits (of 32) |
| Min cluster size | 512 B | 512 B | 512 B |
| Max cluster size (standard) | 32 KB | 32 KB | 32 KB |
| Max cluster size (extended) | 64 KB | 64 KB | 64 KB |
| Max volume size (32 KB clusters) | ~127 MB | ~2 GB | ~8 TB |
| Max volume size (practical) | ~32 MB | ~2 GB | ~2 TB |

### Other Limits

| Parameter | Limit |
|-----------|-------|
| Max file size | 4 GB – 1 byte (0xFFFFFFFF bytes) |
| Max files per directory | 65,536 (limited by cluster chain length) |
| Max path length | 260 characters (Windows); varies by OS |
| Max root directory entries (FAT12/16) | Defined by `BPB_RootEntCnt` (typically 512 for FAT16) |
| Max directory size | 2 MB (65,536 entries × 32 bytes) |

### Cluster Size Recommendations

The Microsoft specification provides these cluster size defaults based on volume size:

**FAT16 (512-byte sectors):**

| Volume Size | Sectors per Cluster | Cluster Size |
|-------------|--------------------:|-------------:|
| ≤ 16 MB | 2 | 1 KB |
| ≤ 128 MB | 4 | 2 KB |
| ≤ 256 MB | 8 | 4 KB |
| ≤ 512 MB | 16 | 8 KB |
| ≤ 1 GB | 32 | 16 KB |
| ≤ 2 GB | 64 | 32 KB |

**FAT32 (512-byte sectors):**

| Volume Size | Sectors per Cluster | Cluster Size |
|-------------|--------------------:|-------------:|
| ≤ 260 MB | 1 | 512 B |
| ≤ 8 GB | 8 | 4 KB |
| ≤ 16 GB | 16 | 8 KB |
| ≤ 32 GB | 32 | 16 KB |
| > 32 GB | 64 | 32 KB |

---

## References

1. Microsoft Corporation. "Microsoft FAT Specification." August 30, 2005. (Contributed to SD Card Association)

2. ECMA-107: "Volume and File Structure of Disk Cartridges for Information Interchange." [https://www.ecma-international.org/publications-and-standards/standards/ecma-107/](https://www.ecma-international.org/publications-and-standards/standards/ecma-107/)

3. ISO 9293:1987 / ISO/IEC 9293:1994: "Information processing — Volume and file structure of flexible disk cartridges for information interchange."

4. Wikipedia. "Design of the FAT file system." [https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system](https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system)

5. Microsoft Support. "Description of the FAT32 File System." KB154997.

---

## Appendix A: Example FAT16 BPB (128 MB Volume)

```
Offset  Value       Field
------  ----------  ---------------------
0x000   0xEB 0x3C 0x90  Jump instruction
0x003   "MSDOS5.0"  OEM Name
0x00B   0x0200      Bytes per sector (512)
0x00D   0x08        Sectors per cluster (8)
0x00E   0x0004      Reserved sectors (4)
0x010   0x02        Number of FATs (2)
0x011   0x0200      Root entry count (512)
0x013   0x0000      Total sectors 16-bit (0 = use 32-bit)
0x015   0xF8        Media descriptor (fixed disk)
0x016   0x0086      Sectors per FAT (134)
0x018   0x003F      Sectors per track (63)
0x01A   0x00FF      Number of heads (255)
0x01C   0x0000003F  Hidden sectors (63)
0x020   0x00042A92  Total sectors 32-bit (273,042)
0x024   0x80        Drive number
0x025   0x00        Reserved
0x026   0x29        Extended boot signature
0x027   0xA0309F1E  Volume serial number
0x02B   "NO NAME    " Volume label
0x036   "FAT16   "  File system type
```

## Appendix B: Example FAT32 BPB (128 MB Volume)

```
Offset  Value       Field
------  ----------  ---------------------
0x000   0xEB 0x58 0x90  Jump instruction
0x003   "MSDOS5.0"  OEM Name
0x00B   0x0200      Bytes per sector (512)
0x00D   0x04        Sectors per cluster (4)
0x00E   0x0020      Reserved sectors (32)
0x010   0x02        Number of FATs (2)
0x011   0x0000      Root entry count (0 for FAT32)
0x013   0x0000      Total sectors 16-bit (0)
0x015   0xF8        Media descriptor
0x016   0x0000      Sectors per FAT 16-bit (0 for FAT32)
0x018   0x003F      Sectors per track (63)
0x01A   0x00FF      Number of heads (255)
0x01C   0x0000003F  Hidden sectors (63)
0x020   0x00042A92  Total sectors 32-bit (273,042)
0x024   0x00000214  Sectors per FAT 32-bit (532)
0x028   0x0000      Extended flags
0x02A   0x0000      File system version
0x02C   0x00000002  Root directory cluster (2)
0x030   0x0001      FSInfo sector (1)
0x032   0x0006      Backup boot sector (6)
0x034   (12 bytes)  Reserved
0x040   0x80        Drive number
0x041   0x00        Reserved
0x042   0x29        Extended boot signature
0x043   0x20BCCD50  Volume serial number
0x047   "NO NAME    " Volume label
0x052   "FAT32   "  File system type
```

---

*Document generated from Microsoft FAT Specification (2005) and Wikipedia sources.*
