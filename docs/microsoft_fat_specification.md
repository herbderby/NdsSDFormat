# Microsoft FAT Specification

**Microsoft Corporation**  
**August 30, 2005**

---

## Document Provenance

This specification was contributed by Microsoft Corporation to the SD Card Association pursuant to the SDA IPR Policy. Microsoft retains ownership of its copyrights. The specification is provided "as is" without warranties of merchantability, fitness for a particular purpose, non-infringement, or title.

**Important**: This document describes the on-media FAT file system format and is intended to guide development of FAT implementations compatible with those provided by Microsoft.

---

## Table of Contents

1. [Overview and Purpose](#overview-and-purpose)
2. [Definitions and Notations](#definitions-and-notations)
3. [Volume Structure](#volume-structure)
4. [Boot Sector and BPB](#boot-sector-and-bpb)
   - [Common BPB Structure (FAT12/FAT16/FAT32)](#common-bpb-structure)
   - [Extended BPB for FAT12/FAT16](#extended-bpb-for-fat12fat16)
   - [Extended BPB for FAT32](#extended-bpb-for-fat32)
   - [Volume Initialization](#volume-initialization)
   - [FAT Type Determination](#fat-type-determination)
   - [Backup BPB Structure](#backup-bpb-structure)
5. [File Allocation Table (FAT)](#file-allocation-table)
   - [FAT Entry Determination](#fat-entry-determination)
   - [Reserved FAT Entries](#reserved-fat-entries)
   - [Free Space Determination](#free-space-determination)
6. [FSInfo Structure (FAT32)](#fsinfo-structure-fat32)
7. [Directory Structure](#directory-structure)
   - [Directory Entry Format](#directory-entry-format)
   - [File/Directory Name](#filedirectory-name)
   - [File Attributes](#file-attributes)
   - [Date and Time Encoding](#date-and-time-encoding)
   - [Directory Creation](#directory-creation)
   - [Root Directory](#root-directory)
   - [File Allocation](#file-allocation)
8. [Long File Name Implementation (VFAT)](#long-file-name-implementation-vfat)
   - [Long Name Directory Entry Structure](#long-name-directory-entry-structure)
   - [Ordinal Number Generation](#ordinal-number-generation)
   - [Checksum Generation](#checksum-generation)
   - [Storage Example](#storage-example)
   - [Name Rules and Character Sets](#name-rules-and-character-sets)
9. [Appendix A: FAT16 BPB Example](#appendix-a-fat16-bpb-example)
10. [Appendix B: FAT32 BPB Example](#appendix-b-fat32-bpb-example)

---

## Overview and Purpose

This document describes the on-media FAT file system format. It does not describe all algorithms contained in the Microsoft FAT file system driver implementation, nor does it describe all algorithms contained in associated utilities (format and chkdsk).

There are three variants of the FAT on-disk format:

| Variant | FAT Entry Size | Primary Distinguishing Feature |
|---------|----------------|-------------------------------|
| FAT12   | 12 bits        | Floppy disks, small volumes ≤4 MB |
| FAT16   | 16 bits        | Small to medium volumes |
| FAT32   | 32 bits (28 usable) | Large volumes |

Data structures for all three variants are described here, along with specific algorithms useful for implementing a FAT driver for reading and/or writing to media.

---

## Definitions and Notations

### Definitions

| Term | Definition |
|------|------------|
| **byte** | A string of binary digits operated upon as a unit |
| **bad (defective) sector** | A sector whose contents cannot be read or one that cannot be written |
| **file** | A named stream of bytes representing a collection of information |
| **sector** | A unit of data that can be accessed independently of other units on the media |
| **cluster** | A unit of allocation comprising a set of logically contiguous sectors. Each cluster is referred to by a cluster number "N". All allocation for a file must be an integral multiple of a cluster |
| **partition** | An extent of sectors within a volume |
| **volume** | A logically contiguous sector address space as specified in the relevant standard for recording |

### Numerical Notation

- Decimal numbers are represented by decimal digits (0–9)
- Hexadecimal numbers are prefixed with `0x` (e.g., `0xF8`)
- Zero represents a single bit with value 0

### Arithmetic Notation

| Notation | Meaning |
|----------|---------|
| `ip(x)` | The integer part of x |
| `ceil(x)` | The minimum integer that is greater than x |
| `rem(x,y)` | The remainder of the integer division of x by y |

---

## Volume Structure

A FAT file system volume is composed of four basic regions, laid out in this order:

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Region 0: Reserved                          │
│        (Boot Sector with BPB, optional additional sectors)          │
├─────────────────────────────────────────────────────────────────────┤
│                         Region 1: FAT Region                        │
│              (Primary FAT, optional FAT copies)                     │
├─────────────────────────────────────────────────────────────────────┤
│                   Region 2: Root Directory Region                   │
│              (FAT12/FAT16 only — does not exist on FAT32)          │
├─────────────────────────────────────────────────────────────────────┤
│                Region 3: File and Directory Data Region             │
│                      (Cluster-addressed data)                       │
└─────────────────────────────────────────────────────────────────────┘
```

**Critical**: All FAT file systems were originally developed for the IBM PC architecture. Consequently, **all on-disk data structures use little-endian byte ordering**.

### Little-Endian Storage Example

A 32-bit FAT entry stored on disk as four bytes (byte[0] through byte[3]):

```
byte[3]: bits 31-24  (most significant)
byte[2]: bits 23-16
byte[1]: bits 15-8
byte[0]: bits 7-0    (least significant)
```

---

## Boot Sector and BPB

The BPB (BIOS Parameter Block) resides in the first sector of the volume (sector 0) in the Reserved Region. This sector is commonly called the "boot sector."

**Evolution of the BPB**:
- MS-DOS 1.x did not include a BPB (introduced in MS-DOS 2.x)
- MS-DOS 3.x BPB was modified to allow >64K sectors
- FAT32 BPB differs from FAT12/FAT16 beginning at offset 36

The BPB in the boot sector must always have all BPB fields for either the FAT12/FAT16 or FAT32 BPB type to ensure maximum compatibility.

**Field Naming Convention**:
- Fields prefixed with `BPB_` are part of the BIOS Parameter Block
- Fields prefixed with `BS_` are part of the boot sector but not the BPB proper

### Common BPB Structure

These fields (offsets 0–35) are common to FAT12, FAT16, and FAT32:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x000 | 3 | `BS_jmpBoot` | Jump instruction to boot code. Two valid forms: `0xEB xx 0x90` (short jump + NOP) or `0xE9 xx xx` (near jump). The `0xEB` form is more frequently used. |
| 0x003 | 8 | `BS_OEMName` | OEM Name Identifier. Can be set to any desired value; typically indicates what system formatted the volume. |
| 0x00B | 2 | `BPB_BytsPerSec` | Bytes per sector. **Valid values: 512, 1024, 2048, or 4096 only.** |
| 0x00D | 1 | `BPB_SecPerClus` | Sectors per allocation unit. Must be a power of 2 greater than 0. **Valid values: 1, 2, 4, 8, 16, 32, 64, or 128.** |
| 0x00E | 2 | `BPB_RsvdSecCnt` | Number of reserved sectors starting at the first sector of the volume. Used to align the data area. **Must not be 0.** |
| 0x010 | 1 | `BPB_NumFATs` | Count of FATs on the volume. **Recommended: 2.** Acceptable: 1. |
| 0x011 | 2 | `BPB_RootEntCnt` | Count of 32-byte directory entries in root directory (FAT12/FAT16). **Must be 0 for FAT32.** Typical FAT16 value: 512. When multiplied by 32, should result in an even multiple of `BPB_BytsPerSec`. |
| 0x013 | 2 | `BPB_TotSec16` | Old 16-bit total sector count. If 0, use `BPB_TotSec32`. **Must be 0 for FAT32.** |
| 0x015 | 1 | `BPB_Media` | Media descriptor byte. **0xF8** for fixed (non-removable) media. **0xF0** frequently used for removable media. Legal values: 0xF0, 0xF8–0xFF. |
| 0x016 | 2 | `BPB_FATSz16` | FAT12/FAT16 sector count per FAT. **Must be 0 for FAT32** (use `BPB_FATSz32`). |
| 0x018 | 2 | `BPB_SecPerTrk` | Sectors per track for INT 0x13 geometry. Only relevant for media with CHS geometry. |
| 0x01A | 2 | `BPB_NumHeads` | Number of heads for INT 0x13. One-based count (e.g., 2 for a 1.44 MB floppy). |
| 0x01C | 4 | `BPB_HiddSec` | Count of hidden sectors preceding this FAT volume. **Must be 0 on non-partitioned media.** Do not use this field to align the data area. |
| 0x020 | 4 | `BPB_TotSec32` | New 32-bit total sector count. Used if `BPB_TotSec16` is 0. **Must be non-zero for FAT32.** |

**Recommendation**: Align the first sector of the partition (if any) to the required alignment unit.

### Extended BPB for FAT12/FAT16

For volumes formatted FAT12 or FAT16, the BPB continues at offset 36:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x024 | 1 | `BS_DrvNum` | INT 0x13 drive number. Set to **0x80** (hard disk) or **0x00** (floppy). |
| 0x025 | 1 | `BS_Reserved1` | Reserved. Set to **0x00**. |
| 0x026 | 1 | `BS_BootSig` | Extended boot signature. Set to **0x29** if the following two fields are non-zero. Indicates that `BS_VolID`, `BS_VolLab`, and `BS_FilSysType` are present. |
| 0x027 | 4 | `BS_VolID` | Volume serial number. Supports volume tracking on removable media. Generate by combining current date and time into a 32-bit value. |
| 0x02B | 11 | `BS_VolLab` | Volume label. Matches the 11-byte volume label in root directory. Default: `"NO NAME    "` (space-padded). Must be updated when root directory volume label changes. |
| 0x036 | 8 | `BS_FilSysType` | Informational string: `"FAT12   "`, `"FAT16   "`, or `"FAT     "`. **Do not use this field to determine FAT type.** |
| 0x03E | 448 | *(Boot Code)* | Bootstrap code area. Set to **0x00** if unused. |
| 0x1FE | 2 | `Signature_word` | **0x55** at offset 510, **0xAA** at offset 511. |
| 0x200+ | varies | *(Padding)* | Set to **0x00** for media where `BPB_BytsPerSec` > 512. |

### Extended BPB for FAT32

For volumes formatted FAT32, the BPB continues at offset 36 with additional fields:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x024 | 4 | `BPB_FATSz32` | 32-bit sector count per FAT. `BPB_FATSz16` must be 0. |
| 0x028 | 2 | `BPB_ExtFlags` | Extended flags. **Bits 0–3**: Zero-based number of active FAT (valid only if mirroring disabled). **Bits 4–6**: Reserved. **Bit 7**: 0 = FAT mirrored at runtime into all FATs; 1 = only one FAT is active (specified in bits 0–3). **Bits 8–15**: Reserved. |
| 0x02A | 2 | `BPB_FSVer` | FAT32 version number (high byte = major, low byte = minor). **Must be 0x0000.** |
| 0x02C | 4 | `BPB_RootClus` | Cluster number of the first cluster of the root directory. Should be **2** or the first usable (not bad) cluster thereafter. |
| 0x030 | 2 | `BPB_FSInfo` | Sector number of the FSInfo structure in the reserved area. **Usually 1.** Note: Only the copy pointed to by this field is kept up to date (both primary and backup boot records point to the same FSInfo sector). |
| 0x032 | 2 | `BPB_BkBootSec` | Sector number of the backup boot record in the reserved area. Set to **0** or **6**. |
| 0x034 | 12 | `BPB_Reserved` | Reserved. **Must be 0x00.** |
| 0x040 | 1 | `BS_DrvNum` | INT 0x13 drive number. Set to **0x80** or **0x00**. |
| 0x041 | 1 | `BS_Reserved1` | Reserved. Set to **0x00**. |
| 0x042 | 1 | `BS_BootSig` | Extended boot signature. Set to **0x29** if the following two fields are non-zero. |
| 0x043 | 4 | `BS_VolID` | Volume serial number. |
| 0x047 | 11 | `BS_VolLab` | Volume label (space-padded). Default: `"NO NAME    "`. |
| 0x052 | 8 | `BS_FilSysType` | `"FAT32   "`. **Informational only — do not use to determine FAT type.** |
| 0x05A | 420 | *(Boot Code)* | Bootstrap code area. |
| 0x1FE | 2 | `Signature_word` | **0x55** at offset 510, **0xAA** at offset 511. |
| 0x200+ | varies | *(Padding)* | Set to **0x00** for media where `BPB_BytsPerSec` > 512. |

### Volume Initialization

FAT type determination depends on the number of clusters (see next section). This section describes determination of BPB field values during volume initialization (formatting).

**General Rules**:
- Floppy disks are formatted as FAT12 (media size must be ≤4 MB)
- For 512-byte sector media: if volume size < 512 MB, format as FAT16; otherwise format as FAT32
- Default FAT type selection can be overridden

#### Cluster Size Selection Tables

Microsoft's format utility uses these tables to determine `BPB_SecPerClus`:

**FAT16 Cluster Size Table** (requires `BPB_RsvdSecCnt`=1, `BPB_NumFATs`=2, `BPB_RootEntCnt`=512):

| Disk Size (sectors) | Disk Size (approx.) | SecPerClus | Cluster Size |
|---------------------|---------------------|------------|--------------|
| ≤8,400              | ≤4.1 MB             | **Error**  | — |
| ≤32,680             | ≤16 MB              | 2          | 1 KB |
| ≤262,144            | ≤128 MB             | 4          | 2 KB |
| ≤524,288            | ≤256 MB             | 8          | 4 KB |
| ≤1,048,576          | ≤512 MB             | 16         | 8 KB |
| ≤2,097,152          | ≤1 GB               | 32         | 16 KB |
| ≤4,194,304          | ≤2 GB               | 64         | 32 KB |
| >4,194,304          | >2 GB               | **Error**  | — |

**FAT32 Cluster Size Table** (requires `BPB_RsvdSecCnt`=32, `BPB_NumFATs`=2):

| Disk Size (sectors) | Disk Size (approx.) | SecPerClus | Cluster Size |
|---------------------|---------------------|------------|--------------|
| ≤66,600             | ≤32.5 MB            | **Error**  | — |
| ≤532,480            | ≤260 MB             | 1          | 512 B |
| ≤16,777,216         | ≤8 GB               | 8          | 4 KB |
| ≤33,554,432         | ≤16 GB              | 16         | 8 KB |
| ≤67,108,864         | ≤32 GB              | 32         | 16 KB |
| >67,108,864         | >32 GB              | 64         | 32 KB |

#### FAT Size Calculation

Once `BPB_SecPerClus` is determined, calculate the FAT size using the following algorithm. Assume `BPB_RootEntCnt`, `BPB_RsvdSecCnt`, `BPB_NumFATs`, and `DskSize` are appropriately set:

```c
RootDirSectors = ((BPB_RootEntCnt * 32) + (BPB_BytsPerSec - 1)) / BPB_BytsPerSec;
TmpVal1 = DskSize - (BPB_ResvdSecCnt + RootDirSectors);
TmpVal2 = (256 * BPB_SecPerClus) + BPB_NumFATs;

if (FATType == FAT32)
    TmpVal2 = TmpVal2 / 2;

FATSz = (TmpVal1 + (TmpVal2 - 1)) / TmpVal2;

if (FATType == FAT32) {
    BPB_FATSz16 = 0;
    BPB_FATSz32 = FATSz;
} else {
    BPB_FATSz16 = LOWORD(FATSz);
    /* FAT12/FAT16 BPB has no BPB_FATSz32 field */
}
```

**Note**: This algorithm may occasionally compute a `FATSz` that is up to 2 sectors too large for FAT16, or up to 8 sectors too large for FAT32. It will never compute a value that is too small. The simplicity of the algorithm outweighs the minor space waste in edge cases.

Unused sectors within a FAT must be set to **0x00**.

### FAT Type Determination

**The FAT type is determined solely by the count of clusters on the volume — not by any string field or BPB signature.**

#### Cluster Count Calculation Algorithm

```c
// Step 1: Calculate root directory sectors (always 0 for FAT32)
RootDirSectors = ((BPB_RootEntCnt * 32) + (BPB_BytsPerSec - 1)) / BPB_BytsPerSec;

// Step 2: Determine FAT size
if (BPB_FATSz16 != 0)
    FATSz = BPB_FATSz16;
else
    FATSz = BPB_FATSz32;

// Step 3: Determine total sectors
if (BPB_TotSec16 != 0)
    TotSec = BPB_TotSec16;
else
    TotSec = BPB_TotSec32;

// Step 4: Calculate data sectors
DataSec = TotSec - (BPB_ResvdSecCnt + (BPB_NumFATs * FATSz) + RootDirSectors);

// Step 5: Calculate cluster count (rounds down)
CountOfClusters = DataSec / BPB_SecPerClus;

// Step 6: Determine FAT type
if (CountOfClusters < 4085) {
    /* Volume is FAT12 */
} else if (CountOfClusters < 65525) {
    /* Volume is FAT16 */
} else {
    /* Volume is FAT32 */
}
```

#### FAT Type Boundaries

| FAT Type | Cluster Count Range |
|----------|---------------------|
| FAT12    | 1 – 4,084 |
| FAT16    | 4,085 – 65,524 |
| FAT32    | 65,525 – (implementation-dependent maximum) |

**Recommendation**: Do not create volumes with cluster counts exactly equal to the boundary values (4,085 or 65,525). To avoid boundary condition computation errors, ensure the cluster count is at least 16 clusters away from these boundaries.

**Derived Values**:
- **Maximum Valid Cluster Number (MAX)** = `CountOfClusters + 1`
- **Count of clusters including reserved clusters** = `CountOfClusters + 2`

### Backup BPB Structure

Loss of sector 0 (containing the BPB) could lead to catastrophic data loss due to inability to mount the volume.

**FAT32 Requirement**: Sector 6 must contain a copy of the BPB.

- The `BPB_BkBootSec` field contains the value **6** for both copies (sector 0 and sector 6)
- The Microsoft FAT32 "boot sector" is actually three sectors long
- A complete copy of all three sectors starts at the `BPB_BkBootSec` sector
- A copy of the FSInfo sector also exists in the backup area, though `BPB_FSInfo` points to the same (primary) FSInfo sector in both boot records

Volume repair utilities should retrieve BPB contents from sector 6 when sector 0 is unreadable.

---

## File Allocation Table

The File Allocation Table (FAT) is the heart of the file system. Each valid entry represents the state of a cluster from the set of clusters comprising the Root Directory Region (when applicable) and the File and Directory Data Region.

### FAT Entry Sizes

| FAT Type | Entry Size |
|----------|------------|
| FAT12    | 12 bits (packed) |
| FAT16    | 16 bits |
| FAT32    | 32 bits (high 4 bits reserved) |

The FAT defines a singly linked list of the "extents" (clusters) of a file, thereby mapping the data region by cluster number. **The first data cluster in the volume is cluster #2.**

A FAT may be larger than required to describe all allocatable sectors. Extra entries at the tail of the FAT must be set to **0**.

**Note**: A FAT directory or file container is a regular file with a special attribute indicating directory type. Directory contents are a series of 32-byte directory entries.

### FAT Entry Values

| FAT12 | FAT16 | FAT32 | Meaning |
|-------|-------|-------|---------|
| 0x000 | 0x0000 | 0x00000000 | Cluster is **free** |
| 0x002 – MAX | 0x0002 – MAX | 0x00000002 – MAX | Cluster is **allocated**; value is next cluster number |
| (MAX+1) – 0xFF6 | (MAX+1) – 0xFFF6 | (MAX+1) – 0x0FFFFFF6 | **Reserved** — must not be used |
| 0xFF7 | 0xFFF7 | 0x0FFFFFF7 | **Bad (defective) cluster** |
| 0xFF8 – 0xFFE | 0xFFF8 – 0xFFFE | 0x0FFFFFF8 – 0x0FFFFFFE | **Reserved** — may be interpreted as end-of-chain |
| 0xFFF | 0xFFFF | 0x0FFFFFFF | Cluster is allocated; **end-of-chain (EOF)** |

**Critical FAT32 Note**: The high 4 bits of a FAT32 entry are reserved. All FAT implementations must preserve the current value of the high 4 bits when modifying any FAT32 entry, except during volume initialization when the entire FAT must be set to 0.

**Additional Limits**:
- No FAT32 volume should be configured with cluster numbers ≥ 0x0FFFFFF7 available for allocation
- FAT12: FAT limited to 6K sectors
- FAT16: FAT limited to 128K sectors
- FAT32: No FAT size limit

### FAT Entry Determination

Given any valid cluster number N, the following algorithms locate its FAT entry.

#### FAT16 and FAT32

```c
if (BPB_FATSz16 != 0)
    FATSz = BPB_FATSz16;
else
    FATSz = BPB_FATSz32;

if (FATType == FAT16)
    FATOffset = N * 2;
else if (FATType == FAT32)
    FATOffset = N * 4;

ThisFATSecNum = BPB_ResvdSecCnt + (FATOffset / BPB_BytsPerSec);
ThisFATEntOffset = FATOffset % BPB_BytsPerSec;
```

`ThisFATSecNum` is the sector number containing the entry for cluster N in the first FAT.

**For additional FAT copies**:
```c
SectorNumber = (FatNumber * FATSz) + ThisFATSecNum;
// FatNumber: 2 for second FAT, 3 for third FAT, etc.
```

**Reading the entry** (after loading the sector into `SecBuff`):
```c
if (FATType == FAT16)
    FAT16ClusEntryVal = *((WORD *) &SecBuff[ThisFATEntOffset]);
else
    FAT32ClusEntryVal = (*((DWORD *) &SecBuff[ThisFATEntOffset])) & 0x0FFFFFFF;
```

**Modifying the entry**:
```c
if (FATType == FAT16) {
    *((WORD *) &SecBuff[ThisFATEntOffset]) = FAT16ClusEntryVal;
} else {
    FAT32ClusEntryVal = FAT32ClusEntryVal & 0x0FFFFFFF;
    *((DWORD *) &SecBuff[ThisFATEntOffset]) =
        (*((DWORD *) &SecBuff[ThisFATEntOffset])) & 0xF0000000;
    *((DWORD *) &SecBuff[ThisFATEntOffset]) =
        (*((DWORD *) &SecBuff[ThisFATEntOffset])) | FAT32ClusEntryVal;
}
```

**Note**: FAT16/FAT32 entries cannot span a sector boundary.

#### FAT12

FAT12 entries are 1.5 bytes (12 bits) and may span sector boundaries.

```c
if (FATType == FAT12)
    FATOffset = N + (N / 2);  // Multiply by 1.5 using integer math

ThisFATSecNum = BPB_ResvdSecCnt + (FATOffset / BPB_BytsPerSec);
ThisFATEntOffset = FATOffset % BPB_BytsPerSec;
```

**Sector boundary handling**: If `ThisFATEntOffset == (BPB_BytsPerSec - 1)`, the entry spans a sector boundary. The simplest strategy is to always load FAT sectors in pairs for FAT12 volumes (load sector N and N+1 together unless N is the last FAT sector).

**Reading the entry**:
```c
FAT12ClusEntryVal = *((WORD *) &SecBuff[ThisFATEntOffset]);

if (N & 0x0001)
    FAT12ClusEntryVal = FAT12ClusEntryVal >> 4;   // Odd cluster: high 12 bits
else
    FAT12ClusEntryVal = FAT12ClusEntryVal & 0x0FFF;  // Even cluster: low 12 bits
```

**Modifying the entry**:
```c
if (N & 0x0001) {
    FAT12ClusEntryVal = FAT12ClusEntryVal << 4;   // Odd cluster
    *((WORD *) &SecBuff[ThisFATEntOffset]) =
        (*((WORD *) &SecBuff[ThisFATEntOffset])) & 0x000F;
} else {
    FAT12ClusEntryVal = FAT12ClusEntryVal & 0x0FFF;  // Even cluster
    *((WORD *) &SecBuff[ThisFATEntOffset]) =
        (*((WORD *) &SecBuff[ThisFATEntOffset])) & 0xF000;
}
*((WORD *) &SecBuff[ThisFATEntOffset]) =
    (*((WORD *) &SecBuff[ThisFATEntOffset])) | FAT12ClusEntryVal;
```

**Assumption**: The `>>` operator shifts 0 into high bits; `<<` shifts 0 into low bits.

### Reserved FAT Entries

The first two FAT entries (FAT[0] and FAT[1]) are reserved.

**FAT[0]** contains the `BPB_Media` byte in its low 8 bits, with all other bits set to 1:

| FAT Type | FAT[0] Value (if BPB_Media = 0xF8) |
|----------|------------------------------------|
| FAT12    | 0x0FF8 |
| FAT16    | 0xFFF8 |
| FAT32    | 0x0FFFFFF8 |

**FAT[1]** is set by the format utility to the end-of-chain (EOC) value.

- On FAT12: Not modified after format; always contains EOC mark
- On FAT16/FAT32: Microsoft Windows may use the high two bits for dirty volume flags

**Dirty Volume Flags** (FAT16):

| Bit Mask | Bit | Meaning When Clear |
|----------|-----|-------------------|
| 0x8000 | ClnShutBitMask | Volume is "dirty" — not properly unmounted |
| 0x4000 | HrdErrBitMask | Disk I/O errors encountered during last mount |

**Dirty Volume Flags** (FAT32):

| Bit Mask | Bit | Meaning When Clear |
|----------|-----|-------------------|
| 0x08000000 | ClnShutBitMask | Volume is "dirty" |
| 0x04000000 | HrdErrBitMask | Disk I/O errors encountered |

When ClnShutBitMask is clear: The volume contents should be scanned for file system metadata damage.

When HrdErrBitMask is clear: The volume should be scanned with a disk repair utility that performs surface analysis looking for new bad sectors.

### Free Space Determination

The file system driver must scan through all FAT entries to construct a list of free/available clusters. A free cluster is identified by:

| FAT Type | Free Cluster Value |
|----------|--------------------|
| FAT12    | 0x000 |
| FAT16    | 0x0000 |
| FAT32    | 0x00000000 (high 4 bits ignored) |

**Important**: The list of free clusters is not stored on the volume. On FAT32 volumes, the `FSI_Free_Count` field in the FSInfo sector may contain a valid count (see next section).

### Additional FAT Notes

1. The last sector of the FAT is not necessarily entirely part of the FAT. The FAT stops at the entry for cluster number `(CountOfClusters + 1)`. All bytes after this entry must be zeroed during formatting.

2. The reserved sectors per FAT (`BPB_FATSz16` or `BPB_FATSz32`) may be larger than actually required. Implementations must determine the last valid FAT sector using `CountOfClusters`.

3. All sectors reserved for the FAT beyond the last valid sector must be set to **0x00** during format.

---

## FSInfo Structure (FAT32)

The FSInfo (File System Information) structure helps optimize file system driver implementations by caching:
- Count of free clusters on the volume
- Hint for the first available (free) cluster number

**Important**: The information in this structure is **advisory only**. File system drivers must validate these values at volume mount. Keeping this structure consistent is recommended but not required.

**Location**:
- Must be present on FAT32 volumes only
- Located at sector 1 (immediately following the boot sector)
- A backup copy is maintained at sector 7

### FSInfo Structure Layout

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x000 | 4 | `FSI_LeadSig` | Lead signature: **0x41615252** ("RRaA"). Validates the beginning of the structure. |
| 0x004 | 480 | `FSI_Reserved1` | Reserved. **Must be 0.** |
| 0x1E4 | 4 | `FSI_StrucSig` | Structure signature: **0x61417272** ("rrAa"). Additional integrity validation. |
| 0x1E8 | 4 | `FSI_Free_Count` | Last known free cluster count. **0xFFFFFFFF** indicates unknown. Must be validated at mount. Recommended to update at dismount. |
| 0x1EC | 4 | `FSI_Nxt_Free` | Hint: cluster number of first available (free) cluster. **0xFFFFFFFF** indicates unknown. Must be validated at mount. Recommended to update at dismount. |
| 0x1F0 | 12 | `FSI_Reserved2` | Reserved. **Must be 0.** |
| 0x1FC | 4 | `FSI_TrailSig` | Trail signature: **0xAA550000**. Validates structure integrity. |

---

## Directory Structure

A FAT directory is a special file type serving as a container for other files and subdirectories. Directory contents (data) are a series of 32-byte directory entries.

### Directory Entry Format

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 11 | `DIR_Name` | "Short" file name (8.3 format) |
| 0x0B | 1 | `DIR_Attr` | File attributes (see below) |
| 0x0C | 1 | `DIR_NTRes` | Reserved. **Must be 0.** |
| 0x0D | 1 | `DIR_CrtTimeTenth` | Creation time, tenths of seconds (0–199) |
| 0x0E | 2 | `DIR_CrtTime` | Creation time (2-second granularity) |
| 0x10 | 2 | `DIR_CrtDate` | Creation date |
| 0x12 | 2 | `DIR_LstAccDate` | Last access date. Must be updated on file read/write. Must equal `DIR_WrtDate` on write. |
| 0x14 | 2 | `DIR_FstClusHI` | High 16 bits of first data cluster. **Only valid for FAT32; must be 0 for FAT12/FAT16.** |
| 0x16 | 2 | `DIR_WrtTime` | Last modification time. Must equal `DIR_CrtTime` at creation. |
| 0x18 | 2 | `DIR_WrtDate` | Last modification date. Must equal `DIR_CrtDate` at creation. |
| 0x1A | 2 | `DIR_FstClusLO` | Low 16 bits of first data cluster |
| 0x1C | 4 | `DIR_FileSize` | File size in bytes (32-bit) |

### File/Directory Name

The `DIR_Name` field contains the 11-character "short name" in 8.3 format:
- 8-character main part
- 3-character extension
- Both parts are space-padded (0x20) if shorter than maximum
- The implicit `.` separator is never stored

#### Special Values in DIR_Name[0]

| Value | Meaning |
|-------|---------|
| 0x00 | Entry is free; **all following entries are also free** |
| 0x05 | First character is actually 0xE5 (KANJI compatibility) |
| 0x2E | Dot entry: `.` (current directory) or `..` (parent directory) |
| 0xE5 | Entry has been deleted (available for reuse) |

#### Name Restrictions

- Lowercase characters are **not allowed**
- Illegal character values:
  - Values < 0x20 (except 0x05 in `DIR_Name[0]`)
  - 0x22 (`"`), 0x2A (`*`), 0x2B (`+`), 0x2C (`,`), 0x2E (`.`), 0x2F (`/`)
  - 0x3A (`:`), 0x3B (`;`), 0x3C (`<`), 0x3D (`=`), 0x3E (`>`), 0x3F (`?`)
  - 0x5B (`[`), 0x5C (`\`), 0x5D (`]`), 0x7C (`|`)
- Names cannot start with space (0x20)
- All names in a directory must be unique

#### Name Storage Examples

| User-Entered Name | DIR_Name Field Contents |
|-------------------|-------------------------|
| `foo.bar` | `"FOO     BAR"` |
| `FOO.BAR` | `"FOO     BAR"` |
| `Foo.Bar` | `"FOO     BAR"` |
| `foo` | `"FOO        "` |
| `foo.` | `"FOO        "` |
| `PICKLE.A` | `"PICKLE  A  "` |
| `prettybg.big` | `"PRETTYBGBIG"` |
| `.big` | `"        BIG"` |

#### Short Name Character Set

Short file names may contain:
- Letters, digits
- Characters with code point values > 127
- Special characters: `$ % ' - _ @ ~ ` ! ( ) { } ^ # &`

Names are stored in the OEM code page configured at the time of creation. They are always converted to uppercase, and original case is lost.

### File Attributes

The `DIR_Attr` byte is a bitmask. The upper two bits are reserved and must be 0.

| Attribute | Value | Description |
|-----------|-------|-------------|
| `ATTR_READ_ONLY` | 0x01 | File cannot be modified; all modification requests must fail |
| `ATTR_HIDDEN` | 0x02 | Not listed unless explicitly requesting hidden files |
| `ATTR_SYSTEM` | 0x04 | Operating system component; not listed unless explicitly requesting system files |
| `ATTR_VOLUME_ID` | 0x08 | Entry contains volume label. `DIR_FstClusHI` and `DIR_FstClusLO` must be 0. Only the root directory may contain one such entry. |
| `ATTR_DIRECTORY` | 0x10 | Entry is a subdirectory. `DIR_FileSize` must be 0 (even if clusters are allocated). |
| `ATTR_ARCHIVE` | 0x20 | Set on create, rename, or modify. Indicates file has been modified since last backup. |

**Special Combination**: `ATTR_LONG_NAME = ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID` (0x0F) indicates a long filename entry.

### Date and Time Encoding

**Optional Fields** (may be set to 0 if not supported):
- `DIR_CrtTime`, `DIR_CrtTimeTenth`, `DIR_CrtDate`, `DIR_LstAccDate`

**Required Fields** (must be maintained):
- `DIR_WrtTime`, `DIR_WrtDate`

#### Date Format (16-bit word)

| Bits | Field | Range |
|------|-------|-------|
| 0–4 | Day of month | 1–31 |
| 5–8 | Month | 1–12 (1 = January) |
| 9–15 | Year from 1980 | 0–127 (represents 1980–2107) |

#### Time Format (16-bit word)

| Bits | Field | Range |
|------|-------|-------|
| 0–4 | Seconds ÷ 2 | 0–29 (represents 0–58 seconds) |
| 5–10 | Minutes | 0–59 |
| 11–15 | Hours | 0–23 |

**Granularity**: 2 seconds. Valid time range: 00:00:00 to 23:59:58.

The `DIR_CrtTimeTenth` field provides additional resolution (0–199, representing 0–1.99 seconds) for creation time only.

### Directory Creation

When creating a new directory:

1. Set `ATTR_DIRECTORY` bit in `DIR_Attr`
2. Set `DIR_FileSize` to **0**
3. Allocate at least one cluster; set `DIR_FstClusLO` and `DIR_FstClusHI` accordingly
4. If only one cluster allocated, FAT entry must indicate end-of-file
5. Initialize cluster contents to **0**
6. **Except for root directory**, create two special entries at the beginning:

   **First Entry** (`.` — dot):
   - `DIR_Name` = `".          "` (dot followed by spaces)
   - Refers to the current directory
   - `DIR_FstClusLO`/`HI` must match the containing directory's first cluster
   - Date/time fields match containing directory

   **Second Entry** (`..` — dotdot):
   - `DIR_Name` = `"..         "` (two dots followed by spaces)
   - Refers to the parent directory
   - `DIR_FstClusLO`/`HI` must match parent directory's first cluster
   - **If parent is root directory**: `DIR_FstClusLO`/`HI` must be **0**
   - Date/time fields match containing directory

### Root Directory

The root directory is a special container created during volume initialization.

**FAT12/FAT16**:
- Immediately follows the last FAT
- First sector: `FirstRootDirSecNum = BPB_ResvdSecCnt + (BPB_NumFATs * BPB_FATSz16)`
- Size determined by `BPB_RootEntCnt`
- Fixed size (not expandable)

**FAT32**:
- Variable size (stored in data region like any directory)
- First cluster: `BPB_RootClus` (typically 2)

**Root Directory Characteristics**:
- Only directory that can contain an entry with `ATTR_VOLUME_ID`
- Has no name (OS typically uses `\` as implied name)
- Has no associated date/time stamps
- Does not contain `.` or `..` entries

### File Allocation

The first cluster of a file is recorded in `DIR_FstClusLO` and `DIR_FstClusHI`.

**Zero-length files**: First cluster number is set to **0**.

#### First Sector of Data Region

```c
FirstDataSector = BPB_ResvdSecCnt + (BPB_NumFATs * FATSz) + RootDirSectors;
```

#### First Sector of a Cluster

Given valid cluster number N (N ≥ 2):

```c
FirstSectorOfCluster = ((N - 2) * BPB_SecPerClus) + FirstDataSector;
```

#### Size Limits

| Item | Maximum Size |
|------|--------------|
| File size | 0xFFFFFFFF bytes (4 GB − 1 byte) |
| Cluster chain | ≤ 0x100000000 bytes (last byte cannot be part of file) |
| Directory size | 2²¹ bytes (2 MB; 65,536 entries × 32 bytes) |

---

## Long File Name Implementation (VFAT)

The `DIR_Name` field only allows 11-character (8.3 format) names. VFAT (Virtual FAT) extends the directory structure to support long file names up to 255 Unicode characters while maintaining backward compatibility.

**Support**: Optional but strongly recommended. Works on FAT12, FAT16, and FAT32.

### Long Name Directory Entry Structure

Long names are stored in one or more additional 32-byte directory entries immediately preceding the associated short name entry. These are stored in **reverse order** (last entry first).

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | `LDIR_Ord` | Sequence number (1-based). Bit 6 (0x40) set on last entry. |
| 0x01 | 10 | `LDIR_Name1` | Characters 1–5 (Unicode, 2 bytes each) |
| 0x0B | 1 | `LDIR_Attr` | **Must be 0x0F** (`ATTR_LONG_NAME`) |
| 0x0C | 1 | `LDIR_Type` | **Must be 0** |
| 0x0D | 1 | `LDIR_Chksum` | Checksum of short name |
| 0x0E | 12 | `LDIR_Name2` | Characters 6–11 (Unicode) |
| 0x1A | 2 | `LDIR_FstClusLO` | **Must be 0** |
| 0x1C | 4 | `LDIR_Name3` | Characters 12–13 (Unicode) |

**Attribute Mask for Detection**:
```c
#define ATTR_LONG_NAME_MASK (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | \
                              ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE)

// Entry is LFN if: (DIR_Attr & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME
```

### Storage Order

Long name entries are stored in reverse order:

| Entry | LDIR_Ord Value | Characters Stored |
|-------|----------------|-------------------|
| Nth (last) long entry | `0x40 | N` | Last characters |
| ... | ... | ... |
| 2nd long entry | 0x02 | Characters 14–26 |
| 1st long entry | 0x01 | Characters 1–13 |
| Short name entry | N/A | 8.3 name |

### Ordinal Number Generation

1. First member has `LDIR_Ord` = **1**
2. Subsequent entries have monotonically increasing values
3. Last (Nth) member has value `N | 0x40` (`LAST_LONG_ENTRY`)

If any member violates these rules, the set is considered corrupt.

### Checksum Generation

An 8-bit checksum is computed from the 11-byte short name and placed in `LDIR_Chksum` of every long name entry. If any checksum doesn't match, the long name entries are considered corrupt.

```c
//--------------------------------------------------------------------------
// ChkSum()
// Returns an unsigned byte checksum computed on an unsigned byte array.
// The array must be 11 bytes long and contain a name in MS-DOS format.
//
// Passed:  pFcbName - Pointer to 11-byte array
// Returns: 8-bit unsigned checksum
//--------------------------------------------------------------------------
unsigned char ChkSum(unsigned char *pFcbName)
{
    short FcbNameLen;
    unsigned char Sum;
    
    Sum = 0;
    for (FcbNameLen = 11; FcbNameLen != 0; FcbNameLen--) {
        // NOTE: The operation is an unsigned char rotate right
        Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }
    return Sum;
}
```

### Storage Example

**Name**: `"The quick brown.fox"`

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 2nd long entry (last): LDIR_Ord = 0x42                                      │
│   LDIR_Name1: "w" "n" "." "f" "o"                                           │
│   LDIR_Name2: "x" 0x0000 0xFFFF 0xFFFF 0xFFFF 0xFFFF 0xFFFF                 │
│   LDIR_Name3: 0xFFFF 0xFFFF                                                 │
│   LDIR_Attr = 0x0F, LDIR_Chksum = [checksum]                               │
├─────────────────────────────────────────────────────────────────────────────┤
│ 1st long entry: LDIR_Ord = 0x01                                             │
│   LDIR_Name1: "T" "h" "e" " " "q"                                           │
│   LDIR_Name2: "u" "i" "c" "k" " " "b"                                       │
│   LDIR_Name3: "r" "o"                                                       │
│   LDIR_Attr = 0x0F, LDIR_Chksum = [checksum]                               │
├─────────────────────────────────────────────────────────────────────────────┤
│ Short name entry: DIR_Name = "THEQUI~1FOX"                                  │
│   DIR_Attr = 0x20 (ATTR_ARCHIVE)                                           │
│   [other fields as normal]                                                  │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Notes**:
- Names are NULL-terminated (0x0000) and padded with 0xFFFF
- A name that exactly fits (multiple of 13 characters) is not NULL-terminated or padded

### Name Rules and Character Sets

**Long File Name Limits**:
- Maximum: 255 characters (not including trailing NULL)
- May contain all short name characters plus: `. + , ; = [ ]`
- Embedded spaces allowed
- Leading and embedded periods allowed; trailing periods ignored
- Leading and trailing spaces ignored

**Character Encoding**:
- Long names stored in **Unicode** (16-bit characters)
- Short names stored in **OEM code page** (8-bit or DBCS)
- Long names preserve original case
- Unicode provides unambiguous case mapping

**Namespace Rules**:
- Any name (short or long) can occur only once per directory (case-insensitive)
- Untranslatable characters are replaced with `_` (underscore) when converting between character sets

---

## Appendix A: FAT16 BPB Example

Example BPB for a 512-byte sector, 128 MB hard disk formatted FAT16:

| Byte Position | Length | Description | Value |
|---------------|--------|-------------|-------|
| 0 | 3 | Jump instruction | `0xEB 0x3C 0x90` |
| 3 | 8 | OEM Name | `"MSDOS5.0"` |
| 11 | 2 | Bytes per sector | `0x0200` (512) |
| 13 | 1 | Sectors per cluster | `0x08` (8) |
| 14 | 2 | Reserved sectors | `0x0004` (4) |
| 16 | 1 | Number of FATs | `0x02` (2) |
| 17 | 2 | Root entry count | `0x0200` (512) |
| 19 | 2 | 16-bit sector count | `0x0000` (use 32-bit) |
| 21 | 1 | Media type | `0xF8` (fixed disk) |
| 22 | 2 | Sectors per FAT | `0x0086` (134) |
| 24 | 2 | Sectors per track | `0x003F` (63) |
| 26 | 2 | Number of heads | `0x00FF` (255) |
| 28 | 4 | Hidden sectors | `0x0000003F` (63) |
| 32 | 4 | 32-bit sector count | `0x00042A92` (273,042) |
| 36 | 1 | Drive number | `0x80` |
| 37 | 1 | Reserved | `0x00` |
| 38 | 1 | Extended boot signature | `0x29` |
| 39 | 4 | Volume serial number | `0xA0309F1E` |
| 43 | 11 | Volume label | `"NO NAME    "` |
| 54 | 8 | FS type (informational) | `"FAT16   "` |
| 62 | 448 | Reserved | `0x00` |
| 510 | 2 | Signature | `0x55AA` |

---

## Appendix B: FAT32 BPB Example

Example BPB for a 512-byte sector, 128 MB hard disk formatted FAT32:

| Byte Position | Length | Description | Value |
|---------------|--------|-------------|-------|
| 0 | 3 | Jump instruction | `0xEB 0x58 0x90` |
| 3 | 8 | OEM Name | `"MSDOS5.0"` |
| 11 | 2 | Bytes per sector | `0x0200` (512) |
| 13 | 1 | Sectors per cluster | `0x04` (4) |
| 14 | 2 | Reserved sectors | `0x0020` (32) |
| 16 | 1 | Number of FATs | `0x02` (2) |
| 17 | 2 | Root entry count | `0x0000` (0 for FAT32) |
| 19 | 2 | 16-bit sector count | `0x0000` |
| 21 | 1 | Media type | `0xF8` |
| 22 | 2 | Sectors per FAT (16-bit) | `0x0000` (0 for FAT32) |
| 24 | 2 | Sectors per track | `0x003F` (63) |
| 26 | 2 | Number of heads | `0x00FF` (255) |
| 28 | 4 | Hidden sectors | `0x0000003F` (63) |
| 32 | 4 | 32-bit sector count | `0x00042A92` (273,042) |
| 36 | 4 | 32-bit sectors per FAT | `0x00000214` (532) |
| 40 | 2 | Extended flags | `0x0000` |
| 42 | 2 | FS version | `0x0000` |
| 44 | 4 | Root directory cluster | `0x00000002` (2) |
| 48 | 2 | FSInfo sector | `0x0001` (1) |
| 50 | 2 | Backup boot sector | `0x0006` (6) |
| 52 | 12 | Reserved | `0x00` |
| 64 | 1 | Drive number | `0x80` |
| 65 | 1 | Reserved | `0x00` |
| 66 | 1 | Extended boot signature | `0x29` |
| 67 | 4 | Volume serial number | `0x20BCCD50` |
| 71 | 11 | Volume label | `"NO NAME    "` |
| 82 | 8 | FS type (informational) | `"FAT32   "` |
| 90 | 420 | Reserved | `0x00` |
| 510 | 2 | Signature | `0x55AA` |

---

## Document Information

**Source**: Microsoft FAT Specification, August 30, 2005  
**Original Distribution**: Contributed to SD Card Association under SDA IPR Policy  
**Copyright**: © 2004 Microsoft Corporation. All rights reserved.

---

*This markdown document was generated from the original Microsoft FAT Specification PDF.*
