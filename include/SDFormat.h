// =============================================================================
// SDFormat.h
// =============================================================================
//
// Public C API for deterministic FAT32 SD card formatting.
//
// This library creates FAT32 filesystem structures optimized for Nintendo DS
// flashcarts (R4i, Acekard). The format is bit-perfect compatible with ARM9
// bootloaders that expect specific alignment and cluster sizes.
//
// Key characteristics of the generated filesystem:
//   - 32 KB clusters (64 sectors × 512 bytes)
//   - 4 MB partition alignment (8192 sectors) for NAND flash optimization
//   - Two mirrored FAT copies for data redundancy
//   - Proper dirty volume flags in FAT[1] indicating clean shutdown
//
// Architecture:
//   Free C functions with extern "C" linkage. Each function writes one
//   logical component of the filesystem. The caller manages the file
//   descriptor lifecycle and calls functions in any order (though the
//   typical sequence is MBR → VBR → FSInfo → FAT tables → root directory).
//
// Reference Documentation:
//   See docs/canonical_file_system.md for the authoritative field name mapping
//   and on-disk structure definitions. This header uses canonical naming
//   throughout (e.g., VBR_ prefix for Volume Boot Record fields, BPB_ for
//   BIOS Parameter Block fields).
//
// =============================================================================

#ifndef SD_FORMAT_H
#define SD_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Formatting Functions
// -----------------------------------------------------------------------------
//
// Each function writes a specific on-disk structure. All functions require:
//   - fd: An open, writable file descriptor to the block device or image file
//   - sectorCount: Total number of 512-byte sectors on the device
//
// The sectorCount parameter drives all layout calculations. For a device of
// N bytes, sectorCount = N / 512.
//
// Return value:
//   0 on success, or the errno value from the failed I/O operation.
//   The caller is responsible for validating fd and sectorCount before
//   calling these functions.

// sdFormatWriteMBR
// ----------------
// Writes the Master Boot Record to absolute sector 0 (LBA 0).
//
// The MBR contains:
//   - A zeroed bootstrap code area (446 bytes) — no executable code needed
//   - A single partition table entry describing the FAT32 partition
//   - The boot signature 0xAA55
//
// The partition entry uses:
//   - PE_type = 0x0C (FAT32 with LBA addressing)
//   - PE_lbaStart = 8192 (4 MB alignment for NAND flash)
//   - PE_chsStart/End = 0xFF 0xFF 0xFF (LBA mode indicator, required by macOS)
//
// See: docs/mbr_x86_design.md, docs/canonical_file_system.md §MBR
int sdFormatWriteMBR(int fd, uint64_t sectorCount);

// sdFormatWriteVolumeBootRecord
// -----------------------------
// Writes both the primary VBR (partition sector 0) and its backup (sector 6).
//
// The VBR is the first sector of the partition and contains:
//   - VBR_jmpBoot: Jump instruction (0xEB 0x58 0x90 for FAT32)
//   - VBR_oemName: OEM identifier ("MSWIN4.1")
//   - The BIOS Parameter Block (BPB) describing volume geometry
//   - VBR_volumeId: Volume serial number (generated from current timestamp)
//   - VBR_volumeLabel: 11-character volume label (uppercase, space-padded)
//   - VBR_signature: Boot sector signature 0xAA55
//
// The label parameter is converted to uppercase and padded with spaces to
// exactly 11 characters. Labels longer than 11 characters are truncated.
//
// FAT32 requires both a primary and backup copy of the boot sector. The
// BPB_backupBootSector field (set to 6) points to the backup location, and
// this function writes identical copies to both sectors.
//
// See: docs/canonical_file_system.md §VBR, docs/microsoft_fat_specification.md
int sdFormatWriteVolumeBootRecord(int fd, uint64_t sectorCount,
                                  const char* label);

// sdFormatWriteFSInfo
// -------------------
// Writes both the primary FSInfo sector (sector 1) and backup (sector 7).
//
// The FSInfo structure caches free cluster information to accelerate
// allocation. It contains:
//   - FSI_leadSignature: 0x41615252 ("RRaA")
//   - FSI_structSignature: 0x61417272 ("rrAa")
//   - FSI_freeCount: Number of free clusters (computed from volume size)
//   - FSI_nextFree: Hint for next free cluster (set to 3, after root dir)
//   - FSI_trailSignature: 0xAA550000
//
// These values are advisory only. Per the Microsoft specification, filesystem
// drivers should validate FSInfo contents against the actual FAT on mount.
//
// See: docs/canonical_file_system.md §FSInfo,
// docs/microsoft_fat_specification.md
int sdFormatWriteFSInfo(int fd, uint64_t sectorCount);

// sdFormatWriteFat32Tables
// ------------------------
// Writes both FAT copies (primary and backup) with proper initialization.
//
// Each FAT is zeroed, then the first three entries are initialized:
//   - FAT[0] (FAT_mediaEntry): 0x0FFFFFF8 — media descriptor with high bits set
//   - FAT[1] (FAT_eocEntry): 0xFFFFFFFF — end-of-chain with clean volume flags
//   - FAT[2]: 0x0FFFFFFF — marks root directory cluster as allocated (EOF)
//
// The high bits of FAT[1] serve as dirty volume flags:
//   - Bit 27 (0x08000000): Clean shutdown flag (1 = clean, 0 = dirty)
//   - Bit 26 (0x04000000): No I/O errors flag (1 = no errors, 0 = errors)
//
// Setting both flags to 1 (as in 0xFFFFFFFF) indicates the volume was
// properly unmounted with no disk errors.
//
// The FAT region begins immediately after the reserved sectors. With two
// FAT copies (the default), the layout is:
//   Primary FAT:   sectors [kFatStartSector .. kFatStartSector + fatSize - 1]
//   Backup FAT:    sectors [kFatStartSector + fatSize .. + 2*fatSize - 1]
//
// See: docs/canonical_file_system.md §FAT Region
int sdFormatWriteFat32Tables(int fd, uint64_t sectorCount);

// sdFormatWriteRootDirectory
// --------------------------
// Initializes the root directory cluster with a volume label entry.
//
// The root directory on FAT32 is stored in the data region like any other
// directory (unlike FAT12/FAT16 where it had a fixed location). Its first
// cluster is specified by BPB_rootCluster (always 2 in this implementation).
//
// This function:
//   1. Zeros the entire first cluster of the data region (32 KB)
//   2. Creates a directory entry with ATTR_VOLUME_ID (0x08) containing
//      the volume label
//
// The volume label directory entry is required in addition to VBR_volumeLabel.
// Some operating systems only display the root directory volume label, while
// others prefer VBR_volumeLabel. Writing both ensures maximum compatibility.
//
// See: docs/canonical_file_system.md §Directory Entry
int sdFormatWriteRootDirectory(int fd, uint64_t sectorCount, const char* label);

#ifdef __cplusplus
}
#endif

#endif  // SD_FORMAT_H
