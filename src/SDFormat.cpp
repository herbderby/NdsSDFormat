// =============================================================================
// SDFormat.cpp
// =============================================================================
//
// Implementation of deterministic FAT32 formatting for Nintendo DS flashcarts.
//
// This file creates all on-disk structures needed for a bootable FAT32
// filesystem. The structures follow the Microsoft FAT Specification (August
// 2005) with specific parameters chosen for DS flashcart compatibility.
//
// On-Disk Layout Overview
// -----------------------
// The formatted device has four contiguous regions:
//
//   ┌───────────────────────────────────────────────────────────────────────┐
//   │ Sector 0              Master Boot Record (MBR)                        │
//   │                       Contains partition table with one FAT32 entry   │
//   ├───────────────────────────────────────────────────────────────────────┤
//   │ Sectors 1–8191        Alignment Gap (zeroed)                          │
//   │                       4 MB alignment for NAND flash optimization      │
//   ├───────────────────────────────────────────────────────────────────────┤
//   │ Sector 8192           Partition Start (Reserved Region)               │
//   │  ├─ Sector 0          Volume Boot Record (VBR) with BPB               │
//   │  ├─ Sector 1          FSInfo structure                                │
//   │  ├─ Sectors 2–5       (unused, zeroed)                                │
//   │  ├─ Sector 6          Backup VBR                                      │
//   │  ├─ Sector 7          Backup FSInfo                                   │
//   │  └─ Sectors 8–31      (unused, zeroed)                                │
//   ├───────────────────────────────────────────────────────────────────────┤
//   │ Sector 8224           FAT Region                                      │
//   │  ├─ Primary FAT       fatSizeSectors sectors                          │
//   │  └─ Backup FAT        fatSizeSectors sectors (identical copy)         │
//   ├───────────────────────────────────────────────────────────────────────┤
//   │ dataStartSector       Data Region                                     │
//   │  ├─ Cluster 2         Root directory (first 32 KB)                    │
//   │  └─ Clusters 3–N      Available for file data                         │
//   └───────────────────────────────────────────────────────────────────────┘
//
// Naming Conventions
// ------------------
// This implementation uses canonical names from docs/canonical_file_system.md:
//   - VBR_ prefix: Volume Boot Record fields (replaces MS spec's BS_)
//   - BPB_ prefix: BIOS Parameter Block fields within the VBR
//   - FSI_ prefix: FSInfo sector fields
//   - DIR_ prefix: Directory entry fields
//   - MBR_ prefix: Master Boot Record fields
//   - PE_ prefix:  Partition table entry fields
//   - k prefix:    Compile-time constants (e.g., kSectorSize, kFatCount)
//
// Reference Documentation
// -----------------------
//   - docs/canonical_file_system.md — Primary reference for field names
//   - docs/microsoft_fat_specification.md — Microsoft FAT spec (August 2005)
//   - docs/fat_file_system_design.md — FAT architecture overview
//   - docs/mbr_x86_design.md — MBR structure and bootstrap
//
// =============================================================================

#include "SDFormat.h"

#include <errno.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <ctime>
#include <span>
#include <string_view>

// =============================================================================
// Constants
// =============================================================================
//
// These constants define the fixed parameters of the filesystem layout.
// The values are chosen specifically for Nintendo DS flashcart compatibility.

// -----------------------------------------------------------------------------
// Sector and Cluster Geometry
// -----------------------------------------------------------------------------

// kSectorSize: The fundamental unit of disk I/O.
// All FAT filesystems use 512-byte sectors (the original IBM PC sector size).
// Every structure offset and size in this implementation is a multiple of 512.
static constexpr uint32_t kSectorSize = 512;

// kSectorsPerCluster: The allocation unit size, expressed in sectors.
// A cluster is the minimum allocation unit for file data. Larger clusters
// reduce FAT table size but waste space for small files.
//
// 64 sectors × 512 bytes = 32,768 bytes (32 KB) per cluster.
//
// This specific value is CRITICAL for DS flashcart compatibility. The ARM9
// bootloader in most flashcarts expects 32 KB clusters. Using a different
// cluster size will cause the bootloader to fail to locate files.
static constexpr uint32_t kSectorsPerCluster = 64;

// kPartitionAlignmentSectors: Where the partition begins (in sectors).
// This value determines the gap between the MBR (sector 0) and the partition
// start. The alignment serves two purposes:
//
//   1. NAND Flash Optimization: Flash memory is organized into erase blocks,
//      typically 128 KB or larger. Aligning the partition to a 4 MB boundary
//      ensures filesystem structures don't straddle erase block boundaries,
//      reducing write amplification and improving performance.
//
//   2. Modern Standard: The 1 MB (2048 sector) or 4 MB (8192 sector) alignment
//      has become standard practice for SSDs and flash media.
//
// 8192 sectors × 512 bytes = 4,194,304 bytes (4 MB).
static constexpr uint32_t kPartitionAlignmentSectors = 8192;

// kReservedSectors: Sectors at the start of the partition before the FAT.
// The reserved region contains the VBR, FSInfo, and their backups.
// The Microsoft spec recommends 32 reserved sectors for FAT32 volumes.
//
// Reserved region layout (partition-relative sectors):
//   Sector 0:   Primary VBR (Volume Boot Record)
//   Sector 1:   Primary FSInfo
//   Sectors 2-5: Unused (zeroed)
//   Sector 6:   Backup VBR
//   Sector 7:   Backup FSInfo
//   Sectors 8-31: Unused (zeroed)
static constexpr uint32_t kReservedSectors = 32;

// kFatCount: Number of File Allocation Table copies.
// FAT32 traditionally maintains two identical FAT copies for redundancy.
// If the primary FAT becomes corrupted, filesystem repair tools can restore
// it from the backup. The BPB_extFlags field can disable mirroring (using
// only one active FAT), but we use the default mirrored configuration.
static constexpr uint32_t kFatCount = 2;

// kFatStartSector: Absolute LBA where the FAT region begins.
// This is computed as: partition start + reserved sectors.
// From this point, the FAT occupies (kFatCount × fatSizeSectors) sectors.
static constexpr uint32_t kFatStartSector =
    kPartitionAlignmentSectors + kReservedSectors;

// -----------------------------------------------------------------------------
// Signature and Type Constants
// -----------------------------------------------------------------------------

// kMbrSignature: The "magic number" at the end of a valid MBR.
// Located at bytes 510-511 (offsets 0x1FE-0x1FF) of sector 0.
// The bytes are 0x55 at offset 510 and 0xAA at offset 511, which reads
// as 0xAA55 when interpreted as a little-endian 16-bit word.
// The BIOS checks this signature before attempting to boot from a disk.
static constexpr uint16_t kMbrSignature = 0xAA55;

// kPartitionTypeFat32Lba: MBR partition type code for FAT32 with LBA.
// Type 0x0C indicates FAT32 using Logical Block Addressing (as opposed to
// the obsolete Cylinder-Head-Sector addressing). This is the standard
// partition type for FAT32 volumes larger than 8 GB.
// See: docs/mbr_x86_design.md "Partition Type"
static constexpr uint8_t kPartitionTypeFat32Lba = 0x0C;

// kMbrBootstrapSize: Size of the bootstrap code area in the MBR.
// The first 446 bytes of the MBR can contain executable code that the BIOS
// loads and executes during boot. Since we're formatting data cards (not
// bootable system disks), we zero this area.
static constexpr uint32_t kMbrBootstrapSize = 446;

// kVbrSignature: Boot signature at the end of the Volume Boot Record.
// Same value as kMbrSignature, but located at the end of the VBR (the first
// sector of the partition). This signature validates the boot sector.
static constexpr uint16_t kVbrSignature = 0xAA55;

// kAttrVolumeId: Directory entry attribute for volume label entries.
// A directory entry with this attribute (0x08) contains the volume's name
// rather than a file or subdirectory. Only the root directory should contain
// a volume label entry.
// See: docs/canonical_file_system.md §File Attributes
static constexpr uint8_t kAttrVolumeId = 0x08;

// -----------------------------------------------------------------------------
// FAT32-Specific Constants
// -----------------------------------------------------------------------------

// kRootCluster: The cluster number where the root directory begins.
// In FAT32, the root directory is stored in the data region like any other
// directory (unlike FAT12/FAT16 where it had a fixed location between the
// FAT and data regions). Cluster numbering starts at 2 because clusters 0
// and 1 are reserved for FAT metadata.
static constexpr uint32_t kRootCluster = 2;

// kMediaDescriptor: Media type byte stored in BPB_mediaDescriptor and FAT[0].
// 0xF8 indicates a "fixed" (non-removable) disk, which is the standard value
// for hard disks and SD cards. 0xF0 would indicate removable media like
// floppy disks. This byte occupies the low 8 bits of FAT[0].
static constexpr uint8_t kMediaDescriptor = 0xF8;

// kFsInfoSector: Partition-relative sector number of the FSInfo structure.
// The FSInfo sector immediately follows the VBR (which is sector 0 of the
// partition). This value is stored in BPB_fsInfoSector.
static constexpr uint32_t kFsInfoSector = 1;

// kBackupBootSector: Partition-relative sector number of the backup VBR.
// FAT32 requires a backup copy of the boot sector for disaster recovery.
// Sector 6 is the Microsoft-recommended location. This value is stored
// in BPB_backupBootSector.
static constexpr uint32_t kBackupBootSector = 6;

// =============================================================================
// On-Disk Structures
// =============================================================================
//
// These packed structures map directly to the binary layout on disk.
// All structures use __attribute__((packed)) to ensure no padding is inserted
// between fields. All multi-byte values are stored little-endian.
//
// The structures are declared `const` where possible to enable
// aggregate initialization with designated initializers (C++20).

// -----------------------------------------------------------------------------
// PartitionEntry — 16-byte partition table entry within the MBR
// -----------------------------------------------------------------------------
//
// Each entry describes one partition on the disk. The MBR contains space for
// four entries, though we only use the first one.
//
// See: docs/mbr_x86_design.md §Partition Table Entry Format
// See: docs/canonical_file_system.md §Master Boot Record (MBR)

struct PartitionEntry {
  // PE_status: Boot indicator flag.
  // 0x80 = Active/bootable partition (the BIOS will attempt to boot from it)
  // 0x00 = Inactive partition
  // Only one partition should be marked active.
  uint8_t status;

  // PE_chsStart: Cylinder-Head-Sector address of the partition's first sector.
  // CHS addressing is obsolete (limited to ~8 GB), so we set all bytes to 0xFF
  // to indicate "use LBA instead." macOS specifically requires this value for
  // partitions using LBA addressing.
  //
  // CHS encoding (when used): byte[0] = head, byte[1] = sector | (cyl_hi << 6),
  // byte[2] = cyl_lo. But we just use {0xFF, 0xFF, 0xFF}.
  uint8_t chsStart[3];

  // PE_type: Partition type code identifying the filesystem.
  // 0x0C = FAT32 with LBA addressing
  // 0x0B = FAT32 with CHS addressing (not used)
  // See docs/mbr_x86_design.md for the full list of partition type codes.
  uint8_t type;

  // PE_chsEnd: CHS address of the partition's last sector.
  // Same encoding and rationale as chsStart — set to 0xFF for LBA mode.
  uint8_t chsEnd[3];

  // PE_lbaStart: Logical Block Address of the partition's first sector.
  // This is the sector number (counting from 0 at the start of the disk)
  // where the partition begins. For us, this equals kPartitionAlignmentSectors.
  uint32_t lbaStart;

  // PE_sectorCount: Total number of sectors in the partition.
  // The partition spans from lbaStart to (lbaStart + sectorCount - 1).
  uint32_t sectorCount;
} __attribute__((packed));

// -----------------------------------------------------------------------------
// MasterBootRecord — 512-byte structure at absolute sector 0 (LBA 0)
// -----------------------------------------------------------------------------
//
// The MBR is the very first sector on the disk. It contains:
//   1. Bootstrap code (446 bytes) — executable code for BIOS boot (unused here)
//   2. Partition table (4 × 16 bytes) — describes up to 4 primary partitions
//   3. Boot signature (2 bytes) — 0xAA55 validates the sector
//
// Note: The optional "Unique Disk ID" (4 bytes at offset 0x1B8) and reserved
// field (2 bytes at offset 0x1BC) are implicitly zero within our bootstrap
// area, as we don't use them.
//
// See: docs/mbr_x86_design.md §MBR Structure

struct MasterBootRecord {
  // MBR_bootstrap: Bootstrap code area.
  // On a bootable disk, this contains executable code that the BIOS loads
  // to address 0x7C00 and executes. Since we're formatting data cards, we
  // leave this area zeroed.
  std::byte bootstrap[kMbrBootstrapSize];

  // MBR_partitions: Partition table with four 16-byte entries.
  // We use only partitions[0]; the rest remain zeroed (empty entries).
  PartitionEntry partitions[4];

  // MBR_signature: Boot signature validating this sector as an MBR.
  // Must be 0xAA55 (bytes 0x55, 0xAA at offsets 510, 511).
  uint16_t signature;
} __attribute__((packed));

// -----------------------------------------------------------------------------
// BiosParameterBlock — 53-byte structure embedded within the VBR
// -----------------------------------------------------------------------------
//
// The BPB describes the volume's geometry and FAT parameters. It begins at
// byte offset 0x00B of the VBR and consists of two parts:
//   1. Common BPB (offsets 0x00B–0x023, 25 bytes) — shared by FAT12/16/32
//   2. FAT32 Extended BPB (offsets 0x024–0x03F, 28 bytes) — FAT32-specific
//
// The BPB is the most critical metadata structure. Corruption here makes
// the volume unmountable, which is why FAT32 requires a backup copy.
//
// See: docs/canonical_file_system.md §Volume Boot Record (VBR)
// See: docs/microsoft_fat_specification.md §Boot Sector and BPB

struct BiosParameterBlock {
  // =========================================================================
  // Common BPB Fields (offsets 0x00B–0x023, shared by FAT12/FAT16/FAT32)
  // =========================================================================

  // BPB_bytesPerSector: Bytes per logical sector.
  // Valid values: 512, 1024, 2048, 4096. We always use 512.
  const uint16_t bytesPerSector{kSectorSize};

  // BPB_sectorsPerCluster: Allocation unit size in sectors.
  // Must be a power of 2: 1, 2, 4, 8, 16, 32, 64, or 128.
  // We use 64 (= 32 KB clusters) for DS compatibility.
  const uint8_t sectorsPerCluster{kSectorsPerCluster};

  // BPB_reservedSectorCount: Sectors before the FAT region.
  // Includes the boot sector itself. For FAT32, the Microsoft spec
  // recommends 32 reserved sectors.
  const uint16_t reservedSectorCount{kReservedSectors};

  // BPB_fatCount: Number of FAT copies.
  // The spec recommends 2 for redundancy. Some implementations use 1.
  const uint8_t fatCount{kFatCount};

  // BPB_rootEntryCount: Maximum root directory entries (FAT12/FAT16 only).
  // MUST be 0 for FAT32, since FAT32 stores the root directory in the
  // data region as a regular cluster chain.
  const uint16_t rootEntryCount{0};

  // BPB_totalSectors16: 16-bit total sector count.
  // Used only if the volume has fewer than 65536 sectors.
  // MUST be 0 for FAT32 (use totalSectors32 instead).
  const uint16_t totalSectors16{0};

  // BPB_mediaDescriptor: Media type byte.
  // 0xF8 = fixed (non-removable) disk, 0xF0 = removable media.
  // This value is also stored in the low byte of FAT[0].
  const uint8_t mediaDescriptor{kMediaDescriptor};

  // BPB_fatSize16: 16-bit sectors per FAT (FAT12/FAT16 only).
  // MUST be 0 for FAT32 (use fatSize32 instead).
  const uint16_t fatSize16{0};

  // BPB_sectorsPerTrack: Sectors per track for INT 13h BIOS calls.
  // Relevant only for CHS geometry on old systems. Standard value: 63.
  const uint16_t sectorsPerTrack{63};

  // BPB_headCount: Number of heads for INT 13h geometry.
  // Standard value for large disks: 255.
  const uint16_t headCount{255};

  // BPB_hiddenSectors: Sectors preceding this partition on the disk.
  // Equals PE_lbaStart from the partition table entry.
  // Used by the boot code to locate the partition.
  const uint32_t hiddenSectors{kPartitionAlignmentSectors};

  // BPB_totalSectors32: 32-bit total sector count of the partition.
  // This is the partition size, not the entire disk size.
  // Computed as: diskSectorCount - kPartitionAlignmentSectors.
  const uint32_t totalSectors32;

  // =========================================================================
  // FAT32 Extended BPB Fields (offsets 0x024–0x03F)
  // =========================================================================

  // BPB_fatSize32: 32-bit sectors per FAT.
  // Computed by fatSizeSectors() using the Microsoft spec formula.
  const uint32_t fatSize32;

  // BPB_extFlags: FAT mirroring and active FAT flags.
  // Bits 0-3: Zero-based number of the active FAT (only if bit 7 is set)
  // Bits 4-6: Reserved
  // Bit 7: 0 = FAT is mirrored to all copies; 1 = only one FAT is active
  // We use 0 (all FATs mirrored).
  const uint16_t extFlags{0};

  // BPB_fsVersion: FAT32 filesystem version.
  // High byte = major version, low byte = minor version.
  // MUST be 0x0000 per the Microsoft spec.
  const uint16_t fsVersion{0};

  // BPB_rootCluster: First cluster of the root directory.
  // In FAT32, the root directory is a regular cluster chain starting here.
  // Typically 2 (the first usable data cluster).
  const uint32_t rootCluster{kRootCluster};

  // BPB_fsInfoSector: Sector number of the FSInfo structure.
  // This is a partition-relative sector number. Typically 1.
  const uint16_t fsInfoSector{kFsInfoSector};

  // BPB_backupBootSector: Sector number of the backup boot sector.
  // Also partition-relative. Typically 6 per Microsoft recommendation.
  // 0 means no backup exists, but FAT32 should always have a backup.
  const uint16_t backupBootSector{kBackupBootSector};

  // BPB_reserved: Reserved space within the extended BPB.
  // Must be zero. Occupies 12 bytes at offsets 0x034–0x03F.
  const std::array<std::byte, 12> reserved{};
} __attribute__((packed));

static_assert(sizeof(BiosParameterBlock) == 53,
              "BiosParameterBlock must be 53 bytes");

// -----------------------------------------------------------------------------
// VolumeBootRecord — 512-byte structure at partition sector 0
// -----------------------------------------------------------------------------
//
// The VBR (also called "boot sector") is the first sector of the partition.
// It contains the BPB and additional boot-related fields. A backup copy
// resides at sector 6 (BPB_backupBootSector).
//
// Note: The Microsoft spec uses "BS_" prefix for VBR fields outside the BPB.
// We use "VBR_" prefix for clarity (see docs/canonical_file_system.md).
//
// Layout:
//   0x000–0x002: VBR_jmpBoot (3 bytes)
//   0x003–0x00A: VBR_oemName (8 bytes)
//   0x00B–0x03F: BiosParameterBlock (53 bytes)
//   0x040–0x059: VBR fields outside BPB (26 bytes)
//   0x05A–0x1FD: VBR_bootCode (420 bytes)
//   0x1FE–0x1FF: VBR_signature (2 bytes)
//
// See: docs/canonical_file_system.md §VBR Structure Overview

struct VolumeBootRecord {
  // =========================================================================
  // VBR Header (offsets 0x000–0x00A)
  // =========================================================================

  // VBR_jmpBoot: Jump instruction to skip over the BPB to boot code.
  // Two valid forms exist:
  //   0xEB xx 0x90: Short jump (EB) + 1-byte offset + NOP (90)
  //   0xE9 xx xx:   Near jump (E9) + 2-byte offset
  //
  // For FAT32, the standard is 0xEB 0x58 0x90, which jumps to offset 0x5A
  // (the start of the boot code area). The 0x58 is the signed displacement
  // from the instruction following the jump.
  const std::array<uint8_t, 3> jmpBoot{0xEB, 0x58, 0x90};

  // VBR_oemName: OEM name/identifier string (8 characters).
  // This is informational only — it doesn't affect filesystem operation.
  // "MSWIN4.1" is the recommended value for maximum compatibility, as
  // some older systems check this string.
  const std::array<char, 8> oemName{'M', 'S', 'W', 'I', 'N', '4', '.', '1'};

  // =========================================================================
  // BIOS Parameter Block (offsets 0x00B–0x03F)
  // =========================================================================

  // Embedded BPB structure containing all volume geometry parameters.
  const BiosParameterBlock bpb;

  // =========================================================================
  // VBR Fields Outside BPB (offsets 0x040–0x059)
  // =========================================================================
  //
  // These fields are part of the boot sector but NOT part of the BPB proper.
  // The Microsoft spec prefixes them with "BS_"; we use "VBR_" for clarity.

  // VBR_driveNumber: INT 13h drive number for BIOS disk access.
  // 0x80 = first hard disk, 0x00 = floppy drive A:.
  // The boot code uses this to identify which drive to read from.
  const uint8_t driveNumber{0x80};

  // VBR_reserved1: Reserved byte.
  // Originally used by Windows NT for dirty volume flags.
  // Set to 0x00.
  const uint8_t reserved1{0};

  // VBR_bootSignature: Extended boot signature.
  // 0x29 indicates that the following three fields (volumeId, volumeLabel,
  // fsType) are present and valid. 0x28 means only volumeId is valid.
  const uint8_t bootSignature{0x29};

  // VBR_volumeId: Volume serial number.
  // A unique identifier for the volume, typically generated from the
  // date and time of formatting. Used by operating systems to detect
  // when removable media has been changed.
  const uint32_t volumeId;

  // VBR_volumeLabel: Volume label (11 characters, space-padded, uppercase).
  // Should match the volume label in the root directory's ATTR_VOLUME_ID
  // entry. Some systems display this label, others display the directory
  // entry's label — write both to ensure compatibility.
  const std::array<char, 11> volumeLabel;

  // VBR_fsType: Filesystem type string (8 characters).
  // "FAT32   " for FAT32 volumes. This is INFORMATIONAL ONLY — the
  // Microsoft spec explicitly states: "Do NOT use this field to determine
  // FAT type." The FAT type must be determined by counting clusters.
  const std::array<char, 8> fsType{'F', 'A', 'T', '3', '2', ' ', ' ', ' '};

  // =========================================================================
  // VBR Tail (offsets 0x05A–0x1FF)
  // =========================================================================

  // VBR_bootCode: Bootstrap code area.
  // On a bootable volume, this contains executable code that loads the
  // operating system. Since we're formatting data cards, this is zeroed.
  const std::array<std::byte, 420> bootCode{};

  // VBR_signature: Boot sector signature.
  // Must be 0xAA55 (byte 0x55 at offset 510, byte 0xAA at offset 511).
  // Validates this sector as a legitimate boot sector.
  const uint16_t signature{kVbrSignature};
} __attribute__((packed));

static_assert(sizeof(VolumeBootRecord) == 512,
              "VolumeBootRecord must be 512 bytes");

// -----------------------------------------------------------------------------
// FSInfo — 512-byte structure at partition sector 1 (and backup at sector 7)
// -----------------------------------------------------------------------------
//
// The FSInfo (File System Information) sector caches information about free
// space to accelerate cluster allocation. Without FSInfo, the filesystem
// driver would need to scan the entire FAT to find free clusters.
//
// IMPORTANT: FSInfo values are advisory hints only. Per the Microsoft spec,
// drivers must validate these values against the actual FAT on mount, as
// they may be stale if the volume was not cleanly unmounted.
//
// See: docs/canonical_file_system.md §FS Information Sector (FSInfo)
// See: docs/microsoft_fat_specification.md §FSInfo Structure (FAT32)

struct FSInfo {
  // FSI_leadSignature: Lead signature for structure validation.
  // Value: 0x41615252, which is ASCII "RRaA" (little-endian).
  // Provides a quick sanity check that this sector contains FSInfo data.
  const uint32_t leadSignature{0x41615252};

  // FSI_reserved1: Reserved area (480 bytes).
  // Must be zero. This large reserved block exists for future expansion.
  const std::array<std::byte, 480> reserved1{};

  // FSI_structSignature: Structure signature for additional validation.
  // Value: 0x61417272, which is ASCII "rrAa" (little-endian).
  // Located just before the actual data fields.
  const uint32_t structSignature{0x61417272};

  // FSI_freeCount: Last known count of free clusters on the volume.
  // 0xFFFFFFFF indicates the count is unknown and must be computed by
  // scanning the FAT. We set this to the actual computed free count
  // during formatting.
  const uint32_t freeCount;

  // FSI_nextFree: Hint for the next free cluster to allocate.
  // The driver can start searching for free clusters from this point.
  // 0xFFFFFFFF indicates no hint (start from cluster 2).
  // We set this to 3 (the cluster after the root directory).
  const uint32_t nextFree{3};

  // FSI_reserved2: Second reserved area (12 bytes).
  // Must be zero.
  const std::array<std::byte, 12> reserved2{};

  // FSI_trailSignature: Trail signature for structure validation.
  // Value: 0xAA550000 (note: NOT the same as the boot signature 0xAA55).
  // Validates the end of the FSInfo structure.
  const uint32_t trailSignature{0xAA550000};
} __attribute__((packed));

static_assert(sizeof(FSInfo) == 512, "FSInfo must be 512 bytes");

// -----------------------------------------------------------------------------
// DirectoryEntry — 32-byte structure for files, directories, and volume labels
// -----------------------------------------------------------------------------
//
// A directory is a file whose data consists of a sequence of 32-byte entries.
// Each entry describes a file, subdirectory, or (in the root directory) the
// volume label.
//
// For formatting, we only create one entry: the volume label in the root
// directory. This entry has ATTR_VOLUME_ID set and contains the volume name
// in the DIR_name field.
//
// See: docs/canonical_file_system.md §Directory Entry
// See: docs/microsoft_fat_specification.md §Directory Entry Format

struct DirectoryEntry {
  // DIR_name: Short filename (8.3 format, 11 characters total).
  // For files: 8-character base name + 3-character extension, space-padded.
  // For volume labels: 11-character label, space-padded, uppercase.
  // The dot between base and extension is NOT stored.
  //
  // Special values in DIR_name[0]:
  //   0x00: Entry is free and all following entries are also free
  //   0x05: First character is actually 0xE5 (Kanji compatibility)
  //   0x2E: Dot entry ("." or "..")
  //   0xE5: Entry has been deleted
  const std::array<char, 11> name;

  // DIR_attributes: File attribute bitmask.
  // Bit 0 (0x01): ATTR_READ_ONLY
  // Bit 1 (0x02): ATTR_HIDDEN
  // Bit 2 (0x04): ATTR_SYSTEM
  // Bit 3 (0x08): ATTR_VOLUME_ID — this entry is the volume label
  // Bit 4 (0x10): ATTR_DIRECTORY — this entry is a subdirectory
  // Bit 5 (0x20): ATTR_ARCHIVE — file has been modified since last backup
  //
  // The combination 0x0F (ATTR_LONG_NAME) indicates a VFAT long filename entry.
  // For a volume label entry, we set only ATTR_VOLUME_ID (0x08).
  const uint8_t attributes{kAttrVolumeId};

  // DIR_ntReserved: Reserved for Windows NT lowercase flags.
  // Must be 0 for volume label entries.
  const uint8_t ntReserved{0};

  // DIR_creationTimeTenths: Creation time, sub-second component.
  // Range 0–199, representing 0–1.99 seconds in 10ms increments.
  // Optional; we set to 0.
  const uint8_t creationTimeTenths{0};

  // DIR_creationTime: Creation time (2-second granularity).
  // Bits 0-4: Seconds/2 (0-29), Bits 5-10: Minutes (0-59), Bits 11-15: Hours.
  // Optional for volume labels; we set to 0.
  const uint16_t creationTime{0};

  // DIR_creationDate: Creation date.
  // Bits 0-4: Day (1-31), Bits 5-8: Month (1-12), Bits 9-15: Year from 1980.
  // Optional for volume labels; we set to 0.
  const uint16_t creationDate{0};

  // DIR_lastAccessDate: Last access date (same format as creationDate).
  // Optional for volume labels; we set to 0.
  const uint16_t lastAccessDate{0};

  // DIR_firstClusterHigh: High 16 bits of the first cluster number.
  // Must be 0 for volume label entries (they have no associated data).
  const uint16_t firstClusterHigh{0};

  // DIR_writeTime: Last modification time (same format as creationTime).
  // Optional for volume labels; we set to 0.
  const uint16_t writeTime{0};

  // DIR_writeDate: Last modification date (same format as creationDate).
  // Optional for volume labels; we set to 0.
  const uint16_t writeDate{0};

  // DIR_firstClusterLow: Low 16 bits of the first cluster number.
  // Must be 0 for volume label entries.
  const uint16_t firstClusterLow{0};

  // DIR_fileSize: File size in bytes (32-bit).
  // Must be 0 for volume labels and directories.
  const uint32_t fileSize{0};
} __attribute__((packed));

static_assert(sizeof(DirectoryEntry) == 32, "DirectoryEntry must be 32 bytes");

// -----------------------------------------------------------------------------
// RootDirSector — Helper structure for writing the root directory
// -----------------------------------------------------------------------------
//
// The root directory begins at cluster 2 (BPB_rootCluster). We initialize
// only the first sector of the cluster, placing the volume label entry at
// the very beginning. The rest of the cluster is zeroed.

struct RootDirSector {
  // The volume label entry at the start of the root directory.
  const DirectoryEntry volumeLabel;

  // Padding to fill the 512-byte sector.
  // Remaining directory entries would follow here, but for a freshly
  // formatted volume, they're all zero (free entries).
  const std::array<std::byte, 480> padding{};
} __attribute__((packed));

static_assert(sizeof(RootDirSector) == 512, "RootDirSector must be 512 bytes");

// =============================================================================
// Volume Label Preparation
// =============================================================================

// prepareVolumeLabel
// ------------------
// Converts a user-supplied volume label string into the 11-character format
// required by DIR_name and VBR_volumeLabel fields.
//
// Transformation rules:
//   1. Characters are converted to uppercase (FAT uses uppercase names)
//   2. The label is truncated to 11 characters if longer
//   3. The label is padded with spaces (0x20) if shorter
//
// Example transformations:
//   "MyDisk"     → "MYDISK     "
//   "R4"         → "R4         "
//   "VeryLongName" → "VERYLONGNAM"
//
// Note: This function does not validate characters against the FAT short
// name character set. The caller should ensure the label contains only
// valid characters (A-Z, 0-9, and certain punctuation).

static std::array<char, 11> prepareVolumeLabel(const char* label) {
  std::array<char, 11> result;
  result.fill(' ');  // Initialize with spaces (FAT padding character)

  std::string_view sv{label};
  size_t len = std::min(sv.length(), size_t{11});

  for (size_t i = 0; i < len; i++) {
    // std::toupper requires unsigned char to avoid undefined behavior
    // with negative char values on platforms where char is signed.
    result[i] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(sv[i])));
  }

  return result;
}

// =============================================================================
// Derived Layout Values
// =============================================================================
//
// These functions compute partition geometry values from the total sector
// count. They implement the formulas from the Microsoft FAT specification,
// using descriptive variable names as documented in canonical_file_system.md.

// partitionSectorCount
// --------------------
// Computes the number of sectors in the FAT32 partition.
//
// The partition begins at kPartitionAlignmentSectors (4 MB into the disk)
// and extends to the end of the device. This value becomes:
//   - PE_sectorCount in the partition table entry
//   - BPB_totalSectors32 in the BIOS Parameter Block

static uint64_t partitionSectorCount(uint64_t sectorCount) {
  return sectorCount - kPartitionAlignmentSectors;
}

// fatSizeSectors
// --------------
// Computes the size of each FAT copy in sectors.
//
// This implements the Microsoft specification's FAT size formula, which
// determines how many sectors are needed to hold the File Allocation Table.
// Each cluster requires one FAT entry (4 bytes for FAT32), so the FAT size
// depends on the number of data clusters.
//
// Formula derivation (see canonical_file_system.md §Derived Layout Values):
//
//   sectorsToAllocate = partitionSectorCount - reservedSectors
//   sectorsPerFatEntry = (256 × sectorsPerCluster + fatCount) / 2
//   fatSizeSectors = ceil(sectorsToAllocate / sectorsPerFatEntry)
//
// Why 256, and why divide by 2?
// -----------------------------
// The formula originates from the Microsoft spec and is designed to work
// for both FAT16 and FAT32. The constant 256 is the number of FAT16 entries
// per 512-byte sector (512 bytes / 2 bytes per entry = 256). The "/ 2"
// converts this to FAT32 entry density (512 / 4 = 128 entries per sector).
//
// The "+ fatCount" term accounts for the fact that adding one FAT sector
// requires space in ALL FAT copies, slightly reducing available data space.
//
// The result may be up to 8 sectors larger than strictly necessary (a safe
// over-estimate), but will never be too small.

static uint32_t fatSizeSectors(uint64_t sectorCount) {
  // sectorsToAllocate: Total sectors available for FAT + data regions
  // (partition size minus the reserved region)
  uint64_t sectorsToAllocate =
      partitionSectorCount(sectorCount) - kReservedSectors;

  // sectorsPerFatEntry: How many data sectors each FAT sector can track.
  // For FAT32 with 64 sectors/cluster: (256 × 64 + 2) / 2 = 8193
  // This means each FAT sector (128 entries × 64 sectors/cluster = 8192
  // data sectors) plus a small correction for the FAT copy overhead.
  uint64_t sectorsPerFatEntry = (256 * kSectorsPerCluster + kFatCount) / 2;

  // Ceiling division: (a + b - 1) / b computes ceil(a / b) in integer math
  return static_cast<uint32_t>((sectorsToAllocate + (sectorsPerFatEntry - 1)) /
                               sectorsPerFatEntry);
}

// dataStartSector
// ---------------
// Computes the absolute LBA of the first data cluster (cluster 2).
//
// The data region immediately follows the FAT region:
//   dataStartSector = partitionStart + reservedSectors + (fatCount × fatSize)
//                   = kFatStartSector + (2 × fatSizeSectors)
//
// This is where the root directory (cluster 2) begins.

static uint32_t dataStartSector(uint64_t sectorCount) {
  return kFatStartSector + (kFatCount * fatSizeSectors(sectorCount));
}

// freeClusterCount
// ----------------
// Computes the number of free clusters after formatting.
//
// After formatting:
//   - Cluster 2 is allocated for the root directory
//   - All other clusters are free
//
// Total clusters = totalDataSectors / sectorsPerCluster
// Free clusters = totalClusters - 1 (minus the root directory cluster)
//
// This value is stored in FSI_freeCount.

static uint32_t freeClusterCount(uint64_t sectorCount) {
  // Total data sectors = partition size - reserved - FAT regions
  uint32_t totalDataSectors =
      static_cast<uint32_t>(partitionSectorCount(sectorCount)) -
      kReservedSectors - (kFatCount * fatSizeSectors(sectorCount));

  // Total clusters in the data region
  uint32_t totalClusters = totalDataSectors / kSectorsPerCluster;

  // Subtract 1 for the root directory cluster (cluster 2)
  return totalClusters - 1;
}

// =============================================================================
// I/O Helpers
// =============================================================================
//
// Low-level functions for writing data to the block device or image file.
// All public formatting functions use these helpers for actual I/O.

// writeBytes
// ----------
// Writes a span of bytes to a specific byte offset in the file.
//
// Handles partial writes by looping until all bytes are written or an
// error occurs. Also handles EINTR (interrupted system call) by retrying.
//
// Parameters:
//   fd:     File descriptor open for writing
//   offset: Byte offset from the start of the file
//   data:   Span of bytes to write
//
// Returns:
//   SDFormatSuccess on successful write (or if data is empty)
//   SDFormatInvalidDevice if fd is invalid
//   SDFormatIOError on seek or write failure

static SDFormatResult writeBytes(int fd, off_t offset,
                                 std::span<const std::byte> data) {
  if (data.empty()) {
    return SDFormatSuccess;
  }

  if (fd < 0) {
    return SDFormatInvalidDevice;
  }

  // Seek to the target offset
  if (lseek(fd, offset, SEEK_SET) == -1) {
    return SDFormatIOError;
  }

  // Write data, handling partial writes and interrupts
  const std::byte* ptr = data.data();
  size_t remaining = data.size();

  while (remaining > 0) {
    ssize_t written = write(fd, ptr, remaining);

    if (written == -1) {
      if (errno == EINTR) {
        continue;  // Interrupted; retry
      }
      return SDFormatIOError;
    }

    ptr += written;
    remaining -= static_cast<size_t>(written);
  }

  return SDFormatSuccess;
}

// writeSector
// -----------
// Writes a single 512-byte structure to a specific LBA on the device.
//
// Accepts any type that is exactly kSectorSize bytes, enforced at compile
// time via static_assert. Converts the sector number to a byte offset and
// delegates to writeBytes.
//
// Parameters:
//   fd:        File descriptor open for writing
//   sectorLba: Logical Block Address (sector number, 0-based)
//   sector:    Reference to a 512-byte structure to write
//
// Returns:
//   SDFormatSuccess on successful write
//   SDFormatIOError on I/O failure

template <typename T>
static SDFormatResult writeSector(int fd, off_t sectorLba, const T& sector) {
  static_assert(sizeof(T) == kSectorSize);
  off_t offset = sectorLba * kSectorSize;
  return writeBytes(fd, offset, std::as_bytes(std::span{&sector, 1}));
}

// zeroSectors
// -----------
// Writes zeros to a contiguous range of sectors.
//
// Uses a cluster-sized buffer (32 KB) for efficiency, writing multiple
// sectors per system call when possible.
//
// Parameters:
//   fd:          File descriptor open for writing
//   startSector: First sector (LBA) to zero
//   sectorCount: Number of sectors to zero
//
// Returns:
//   SDFormatSuccess if all sectors zeroed successfully
//   SDFormatIOError on I/O failure

static SDFormatResult zeroSectors(int fd, off_t startSector,
                                  uint32_t sectorCount) {
  // Use a cluster-sized buffer for efficient bulk zeroing
  static constexpr uint32_t kClusterBytes = kSectorsPerCluster * kSectorSize;
  std::byte buffer[kClusterBytes] = {};  // Zero-initialized

  off_t offset = startSector * kSectorSize;
  uint32_t remaining = sectorCount;

  while (remaining > 0) {
    // Write up to one cluster at a time
    uint32_t toWrite = std::min(remaining, kSectorsPerCluster);
    uint32_t bytes = toWrite * kSectorSize;

    if (auto result = writeBytes(fd, offset, std::span{buffer, bytes});
        result != SDFormatSuccess) {
      return result;
    }

    remaining -= toWrite;
    offset += bytes;
  }

  return SDFormatSuccess;
}

// =============================================================================
// Public API Implementation
// =============================================================================

// sdFormatWriteMBR
// ----------------
// Writes the Master Boot Record to absolute sector 0.
//
// The MBR layout (512 bytes total):
//   Offset 0x000: 446 bytes of bootstrap code (zeroed — not a boot disk)
//   Offset 0x1BE: 16-byte partition entry 1 (FAT32 LBA partition)
//   Offset 0x1CE: 16-byte partition entry 2 (zeroed — unused)
//   Offset 0x1DE: 16-byte partition entry 3 (zeroed — unused)
//   Offset 0x1EE: 16-byte partition entry 4 (zeroed — unused)
//   Offset 0x1FE: 2-byte signature (0xAA55)
//
// The single partition entry specifies:
//   - Active/bootable status (0x80)
//   - FAT32 LBA type (0x0C)
//   - Starting at sector 8192 (4 MB alignment)
//   - Extending to the end of the device

SDFormatResult sdFormatWriteMBR(int fd, uint64_t sectorCount) {
  const MasterBootRecord mbr = {
      // bootstrap is implicitly zeroed (not a boot disk)
      .partitions =
          {
              PartitionEntry{
                  .status = 0x80,  // Active/bootable partition

                  // CHS values set to 0xFF for LBA mode (required by macOS)
                  .chsStart = {0xFF, 0xFF, 0xFF},

                  .type = kPartitionTypeFat32Lba,  // 0x0C = FAT32 with LBA

                  .chsEnd = {0xFF, 0xFF, 0xFF},

                  // Partition starts at 4 MB boundary (8192 sectors)
                  .lbaStart = kPartitionAlignmentSectors,

                  // Partition extends to the end of the device
                  .sectorCount =
                      static_cast<uint32_t>(partitionSectorCount(sectorCount)),
              },
              // partitions[1–3] are implicitly zeroed (unused)
          },
      .signature = kMbrSignature,  // 0xAA55
  };

  // Write to sector 0 (absolute LBA 0)
  return writeSector(fd, 0, mbr);
}

// sdFormatWriteVolumeBootRecord
// -----------------------------
// Writes both the primary VBR (sector 0 of partition) and backup (sector 6).
//
// The VBR contains critical filesystem metadata including:
//   - Jump instruction and OEM name
//   - Complete BIOS Parameter Block with FAT32 geometry
//   - Volume serial number and label
//   - Filesystem type string
//   - Boot sector signature
//
// Both the primary and backup copies are identical. The backup exists for
// disaster recovery — if sector 0 of the partition becomes unreadable,
// repair tools can restore the BPB from sector 6.

SDFormatResult sdFormatWriteVolumeBootRecord(int fd, uint64_t sectorCount,
                                             const char* label) {
  // Prepare the 11-character volume label (uppercase, space-padded)
  auto volumeLabel = prepareVolumeLabel(label);

  // Construct the VBR using designated initializers
  // Only the variable fields need explicit values; others use defaults
  const VolumeBootRecord vbr = {
      .bpb =
          {
              // Partition size in sectors
              .totalSectors32 =
                  static_cast<uint32_t>(partitionSectorCount(sectorCount)),

              // Computed FAT size in sectors
              .fatSize32 = fatSizeSectors(sectorCount),
          },

      // Volume serial number from current timestamp
      .volumeId = static_cast<uint32_t>(time(nullptr)),

      // Volume label (must match root directory entry)
      .volumeLabel = volumeLabel,
  };

  // Write primary VBR to partition sector 0
  // Absolute LBA = kPartitionAlignmentSectors (8192)
  if (auto result = writeSector(fd, kPartitionAlignmentSectors, vbr);
      result != SDFormatSuccess) {
    return result;
  }

  // Write backup VBR to partition sector 6
  // Absolute LBA = kPartitionAlignmentSectors + kBackupBootSector (8198)
  return writeSector(fd, kPartitionAlignmentSectors + kBackupBootSector, vbr);
}

// sdFormatWriteFSInfo
// -------------------
// Writes both the primary FSInfo (sector 1) and backup (sector 7).
//
// The FSInfo structure provides hints to accelerate cluster allocation:
//   - FSI_freeCount: Number of free clusters on the volume
//   - FSI_nextFree: Where to start searching for free space
//
// For a freshly formatted volume:
//   - freeCount = total clusters - 1 (minus the root directory cluster)
//   - nextFree = 3 (first cluster after root directory)

SDFormatResult sdFormatWriteFSInfo(int fd, uint64_t sectorCount) {
  // Construct FSInfo with computed free cluster count
  const FSInfo fsinfo = {
      .freeCount = freeClusterCount(sectorCount),
      // nextFree defaults to 3 (cluster after root directory)
  };

  // Write primary FSInfo to partition sector 1
  // Absolute LBA = kPartitionAlignmentSectors + kFsInfoSector (8193)
  if (auto result =
          writeSector(fd, kPartitionAlignmentSectors + kFsInfoSector, fsinfo);
      result != SDFormatSuccess) {
    return result;
  }

  // Write backup FSInfo to partition sector 7
  // Absolute LBA = kPartitionAlignmentSectors + kBackupBootSector + 1 (8199)
  return writeSector(fd, kPartitionAlignmentSectors + kBackupBootSector + 1,
                     fsinfo);
}

// sdFormatWriteFat32Tables
// ------------------------
// Initializes both FAT copies (primary and backup).
//
// FAT initialization involves:
//   1. Zeroing all FAT sectors (marks all clusters as free)
//   2. Writing the reserved entries FAT[0], FAT[1], and FAT[2]
//
// Reserved FAT entries:
//   FAT[0] (FAT_mediaEntry): 0x0FFFFFF8
//     - Low byte = media descriptor (0xF8 for fixed disk)
//     - Upper bytes = 0xFFFFFF (all 1s)
//
//   FAT[1] (FAT_eocEntry): 0xFFFFFFFF
//     - End-of-chain marker with dirty volume flags
//     - Bit 27 set = clean shutdown (volume properly unmounted)
//     - Bit 26 set = no I/O errors encountered
//     - All bits set indicates a clean, error-free volume
//
//   FAT[2]: 0x0FFFFFFF
//     - Marks the root directory cluster as allocated
//     - End-of-chain marker (root directory is one cluster)

SDFormatResult sdFormatWriteFat32Tables(int fd, uint64_t sectorCount) {
  // First sector of each FAT containing the three reserved entries.
  // The array is 128 uint32_t values (512 bytes = 1 sector).
  // Entries 0-2 are initialized; entries 3-127 are implicitly zero.
  const uint32_t fatSector[128] = {
      // FAT[0]: Media descriptor (0xF8) with upper bits set
      // Stored as 0xFFFFFF00 | 0xF8 = 0xFFFFFFF8 in the spec's notation,
      // but for FAT32 only 28 bits matter, so 0x0FFFFFF8 is equivalent.
      0xFFFFFF00 | kMediaDescriptor,

      // FAT[1]: Clean shutdown flags (all bits set = clean)
      0xFFFFFFFF,

      // FAT[2]: Root directory cluster (allocated, end-of-chain)
      0x0FFFFFFF,
  };

  uint32_t fatSize = fatSizeSectors(sectorCount);

  // ----- Primary FAT (FAT 1) -----
  // Location: kFatStartSector to kFatStartSector + fatSize - 1

  if (auto result = zeroSectors(fd, kFatStartSector, fatSize);
      result != SDFormatSuccess) {
    return result;
  }

  // Write reserved entries to first sector of FAT 1
  if (auto result = writeSector(fd, kFatStartSector, fatSector);
      result != SDFormatSuccess) {
    return result;
  }

  // ----- Backup FAT (FAT 2) -----
  // Location: kFatStartSector + fatSize to kFatStartSector + 2*fatSize - 1

  if (auto result = zeroSectors(fd, kFatStartSector + fatSize, fatSize);
      result != SDFormatSuccess) {
    return result;
  }

  // Write reserved entries to first sector of FAT 2
  return writeSector(fd, kFatStartSector + fatSize, fatSector);
}

// sdFormatWriteRootDirectory
// --------------------------
// Initializes the root directory cluster (cluster 2) with a volume label.
//
// The root directory in FAT32 is stored in the data region like any other
// directory. Its location is determined by BPB_rootCluster (always 2 here).
//
// Initialization steps:
//   1. Zero the entire cluster (32 KB = 64 sectors)
//   2. Create a volume label entry (DIR_attributes = ATTR_VOLUME_ID)
//
// The volume label entry appears at the very beginning of the root directory.
// A freshly formatted volume has only this one entry; all others are free
// (zeroed, with DIR_name[0] = 0x00 indicating the end of directory entries).

SDFormatResult sdFormatWriteRootDirectory(int fd, uint64_t sectorCount,
                                          const char* label) {
  // Prepare the volume label (11 characters, uppercase, space-padded)
  auto volumeLabel = prepareVolumeLabel(label);

  // Calculate the absolute LBA of cluster 2 (root directory)
  uint32_t dataStart = dataStartSector(sectorCount);

  // Zero the entire first cluster of the data region
  if (auto result = zeroSectors(fd, dataStart, kSectorsPerCluster);
      result != SDFormatSuccess) {
    return result;
  }

  // Create the root directory's first sector with the volume label entry
  const RootDirSector rootDirSector = {
      .volumeLabel =
          {
              .name = volumeLabel,
              // attributes defaults to kAttrVolumeId (0x08)
              // All other fields default to 0
          },
      // padding is implicitly zeroed
  };

  // Write the volume label entry to the first sector of the root directory
  return writeSector(fd, dataStart, rootDirSector);
}
