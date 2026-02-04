# FAT16 Forensic Analysis

Empirical analysis of macOS FAT16 formatting behavior, conducted to
determine the correct parameters for Nintendo DS flashcart compatibility.

## Background

The NdsSDFormat library currently produces FAT32 filesystems with 32KB
clusters and 4MB partition alignment. To support smaller SD cards
(≤2GB), FAT16 formatting may be required. Rather than relying solely
on the Microsoft FAT specification, we reverse-engineered macOS's
`newfs_msdos` output to discover what parameters actually work.

## Test Methodology

1. Created disk images of various sizes (64MB, 512MB, 1GB, 2GB)
2. Partitioned with `diskutil partitionDisk` using MBR + FAT16
3. Extracted raw sectors via `dd` and parsed MBR/VBR/FAT structures
4. Validated 1GB configuration by formatting a physical SD card and
   booting on a Nintendo DS flashcart

## Findings by Volume Size

| Size | Cluster Size | Clusters | PE_lbaStart | Notes |
|------|--------------|----------|-------------|-------|
| 64MB | 2KB (4 sec) | 32,695 | 1 | — |
| 512MB | 8KB (16 sec) | 65,501 | 1 | — |
| 1GB | 16KB (32 sec) | 65,518 | 1 | **Tested working** |
| 2GB | 32KB (64 sec) | 65,524 | 2048 | Near FAT16 limit |

macOS varies cluster size to keep the cluster count under 65,525 (the
FAT16 maximum). Partition alignment is minimal (1 sector) except at
2GB where it uses 1MB (2048 sectors).

## Known Working Configuration (1GB)

This configuration was validated on a physical 1GB SD card with an
R4i flashcart. The card booted successfully, though the flashcart
software displayed a warning that the cluster size was not 32KB and
games may load more slowly.

### MBR (Sector 0)

```text
Offset  Field           Value       Notes
------  --------------  ----------  ---------------------------
0x1BE   PE_status       0x00        Not bootable
0x1BF   PE_chsStart     FE FF FF    LBA mode marker
0x1C2   PE_type         0x06        FAT16 (CHS addressing flag)
0x1C3   PE_chsEnd       FE FF FF    LBA mode marker
0x1C6   PE_lbaStart     1           Partition at sector 1
0x1CA   PE_sectorCount  2,097,151   ~1GB
0x1FE   MBR_signature   0xAA55
```

### VBR (Sector 1)

```text
Offset  Field              Value         Notes
------  -----------------  ------------  ---------------------------
0x000   VBR_jmpBoot        EB 3C 90      Jump to offset 0x3E
0x003   VBR_oemName        "BSD  4.4"    macOS identifier
0x00B   BPB_BytsPerSec     512
0x00D   BPB_SecPerClus     32            16KB clusters
0x00E   BPB_RsvdSecCnt     1             Single reserved sector
0x010   BPB_NumFATs        2
0x011   BPB_RootEntCnt     512           16KB root directory
0x013   BPB_TotSec16       0             Use TotSec32
0x015   BPB_Media          0xF8          Fixed disk
0x016   BPB_FATSz16        256           128KB per FAT
0x018   BPB_SecPerTrk      32
0x01A   BPB_NumHeads       128
0x01C   BPB_HiddSec        1             Matches PE_lbaStart
0x020   BPB_TotSec32       2,097,151
0x024   BS_DrvNum          0x80          Hard disk
0x025   BS_Reserved1       0x00
0x026   BS_BootSig         0x29          Extended fields present
0x027   BS_VolID           (timestamp)
0x02B   BS_VolLab          "TESTLABEL  "
0x036   BS_FilSysType      "FAT16   "
0x1FE   VBR_signature      0xAA55
```

### FAT Region (Sectors 2–513)

```text
Entry   Value    Meaning
------  -------  ---------------------------
FAT[0]  0xFFF8   Media descriptor (0xF8) + high bits
FAT[1]  0xFFFF   EOC marker / clean shutdown flags
```

FAT 1 spans sectors 2–257 (256 sectors).
FAT 2 spans sectors 258–513 (256 sectors).

### Root Directory (Sectors 514–545)

Fixed 32-sector region (16,384 bytes) holding up to 512 directory
entries. Unlike FAT32, this is not a cluster chain—it exists between
the FAT region and the data region.

First entry contains the volume label with `DIR_Attr = 0x08`
(`ATTR_VOLUME_ID`).

### Data Region (Sector 546+)

Cluster 2 begins at sector 546. Total cluster count: 65,518.

## Comparison with FAT32

| Aspect | FAT32 (NdsSDFormat) | FAT16 (macOS 1GB) |
|--------|---------------------|-------------------|
| Partition alignment | 4MB (8192 sectors) | 512B (1 sector) |
| Reserved sectors | 32 | 1 |
| Cluster size | 32KB (fixed) | 16KB (varies by size) |
| Root directory | Cluster chain | Fixed 32-sector region |
| FSInfo sector | Yes (sectors 1, 7) | None |
| Backup VBR | Yes (sector 6) | None |
| FAT entry size | 32-bit | 16-bit |
| VBR_jmpBoot | EB 58 90 | EB 3C 90 |

## Cluster Size Warning

The flashcart software warned that the 16KB cluster size was
non-optimal:

> Cluster size is not 32KB. Games may load more slowly.

This suggests the ARM9 bootloader performs optimally with 32KB
clusters, likely due to alignment with internal read buffers or
NAND erase block sizes.

## TODO

- [ ] Format a 1GB SD card with forced 32KB clusters and test on
      the flashcart. macOS `fsck_msdos` rejects such a filesystem
      as poorly formatted (cluster count too low for the FAT size),
      but the DS bootloader may accept it regardless. If it works,
      we should use 32KB clusters unconditionally for DS compatibility.

## Implementation Notes

If implementing FAT16 support in NdsSDFormat:

1. **VBR structure differs significantly.** FAT16 BPB ends at offset
   0x024 with extended fields at 0x024–0x03D. FAT32 BPB ends at 0x040
   with extended fields at 0x040–0x059. The `VBR_jmpBoot` offset
   changes accordingly (0x3C vs 0x58).

2. **No FSInfo sector.** FAT16 has no equivalent to FAT32's FSInfo
   structure. The free cluster count must be computed by scanning
   the FAT.

3. **No backup boot sector.** FAT16 does not reserve sector 6 for
   a backup VBR.

4. **Root directory is a fixed region.** Calculate its size as
   `(BPB_RootEntCnt * 32) / BPB_BytsPerSec` sectors. It lives
   between the FAT copies and the data region, not in a cluster.

5. **FAT entries are 16-bit.** Reserved entries:
   - FAT[0] = 0xFF00 | BPB_Media (typically 0xFFF8)
   - FAT[1] = 0xFFFF (EOC with clean flags)

## References

- `docs/microsoft_fat_specification.md` — Field definitions
- `docs/canonical_file_system.md` — Naming conventions
- `tools/fat16_forensics.sh` — Forensic extraction script

## Document History

- 2026-02-03: Initial forensic analysis (Claude + Herb)
