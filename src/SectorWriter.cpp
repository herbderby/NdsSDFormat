#include "SectorWriter.h"

#include <errno.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <ctime>
#include <print>
#include <span>

namespace sdFormat {

// -----------------------------------------------------------------------------
// Constants & Structs
// -----------------------------------------------------------------------------

static constexpr uint16_t kMbrSignature = 0xAA55;
static constexpr uint8_t kPartitionTypeFat32Lba = 0x0C;
static constexpr uint32_t kMbrBootstrapSize = 446;
static constexpr uint16_t kVbrSignature = 0xAA55;
static constexpr uint8_t kAttrVolumeId = 0x08;

struct PartitionEntry {
  uint8_t status;        // 0x80 = Active (bootable)
  uint8_t chsStart[3];   // CHS of first sector (0xFF for LBA)
  uint8_t type;          // Partition type (0x0C = FAT32 LBA)
  uint8_t chsEnd[3];     // CHS of last sector (0xFF for LBA)
  uint32_t lbaStart;     // LBA of first sector in partition
  uint32_t sectorCount;  // Number of sectors in partition
} __attribute__((packed));

struct MasterBootRecord {
  std::byte bootstrap[kMbrBootstrapSize];  // Boot code (unused/zeroed)
  PartitionEntry partitions[4];            // Partition table
  uint16_t signature;                      // 0xAA55
} __attribute__((packed));

static constexpr uint32_t kRootCluster = 2;
static constexpr uint8_t kMediaDescriptor = 0xF8;
static constexpr uint32_t kFsInfoSector = 1;
static constexpr uint32_t kBackupBootSector = 6;

struct BiosParameterBlock {
  // Common BPB (0x00B–0x023)
  const uint16_t bytesPerSector{SectorWriter::kSectorSize};
  const uint8_t sectorsPerCluster{SectorWriter::kSectorsPerCluster};
  const uint16_t reservedSectorCount{SectorWriter::kReservedSectors};
  const uint8_t fatCount{SectorWriter::kFatCount};
  const uint16_t rootEntryCount{0};
  const uint16_t totalSectors16{0};
  const uint8_t mediaDescriptor{kMediaDescriptor};
  const uint16_t fatSize16{0};
  const uint16_t sectorsPerTrack{63};
  const uint16_t headCount{255};
  const uint32_t hiddenSectors{SectorWriter::kPartitionAlignmentSectors};
  const uint32_t totalSectors32;

  // FAT32 Extended BPB (0x024–0x03F)
  const uint32_t fatSize32;
  const uint16_t extFlags{0};
  const uint16_t fsVersion{0};
  const uint32_t rootCluster{kRootCluster};
  const uint16_t fsInfoSector{kFsInfoSector};
  const uint16_t backupBootSector{kBackupBootSector};
  const std::array<std::byte, 12> reserved{};
} __attribute__((packed));
static_assert(sizeof(BiosParameterBlock) == 53,
              "BiosParameterBlock must be 53 bytes");

struct VolumeBootRecord {
  // VBR header (0x000–0x00A)
  const std::array<uint8_t, 3> jmpBoot{0xEB, 0x58, 0x90};
  const std::array<char, 8> oemName{'M', 'S', 'W', 'I', 'N', '4', '.', '1'};

  // BPB (0x00B–0x03F)
  const BiosParameterBlock bpb;

  // VBR fields outside BPB (0x040–0x059)
  const uint8_t driveNumber{0x80};
  const uint8_t reserved1{0};
  const uint8_t bootSignature{0x29};
  const uint32_t volumeId;
  const std::array<char, 11> volumeLabel;
  const std::array<char, 8> fsType{'F', 'A', 'T', '3', '2', ' ', ' ', ' '};

  // VBR tail (0x05A–0x1FF)
  const std::array<std::byte, 420> bootCode{};
  const uint16_t signature{kVbrSignature};
} __attribute__((packed));
static_assert(sizeof(VolumeBootRecord) == 512,
              "VolumeBootRecord must be 512 bytes");

struct FSInfo {
  const uint32_t leadSignature{0x41615252};
  const std::array<std::byte, 480> reserved1{};
  const uint32_t structSignature{0x61417272};
  const uint32_t freeCount;
  const uint32_t nextFree{3};
  const std::array<std::byte, 12> reserved2{};
  const uint32_t trailSignature{0xAA550000};
} __attribute__((packed));
static_assert(sizeof(FSInfo) == 512, "FSInfo must be 512 bytes");

struct DirectoryEntry {
  const std::array<char, 11> name;
  const uint8_t attributes{kAttrVolumeId};
  const uint8_t ntReserved{0};
  const uint8_t creationTimeTenths{0};
  const uint16_t creationTime{0};
  const uint16_t creationDate{0};
  const uint16_t lastAccessDate{0};
  const uint16_t firstClusterHigh{0};
  const uint16_t writeTime{0};
  const uint16_t writeDate{0};
  const uint16_t firstClusterLow{0};
  const uint32_t fileSize{0};
} __attribute__((packed));
static_assert(sizeof(DirectoryEntry) == 32, "DirectoryEntry must be 32 bytes");

struct RootDirSector {
  const DirectoryEntry volumeLabel;
  const std::array<std::byte, 480> padding{};
} __attribute__((packed));
static_assert(sizeof(RootDirSector) == 512, "RootDirSector must be 512 bytes");

// -----------------------------------------------------------------------------
// Volume Label Preparation
// -----------------------------------------------------------------------------

static std::array<char, 11> prepareVolumeLabel(std::string_view label) {
  std::array<char, 11> result;
  result.fill(' ');
  size_t len = std::min(label.length(), size_t{11});
  for (size_t i = 0; i < len; i++) {
    result[i] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(label[i])));
  }
  return result;
}

// -----------------------------------------------------------------------------
// Factory & Constructor
// -----------------------------------------------------------------------------

SectorWriter::SectorWriter(int fd, size_t sectorCount,
                           std::array<char, 11> volumeLabel,
                           size_t partitionSectorCount, uint32_t fatSizeSectors,
                           uint32_t fatStartSector, uint32_t dataStartSector,
                           uint32_t freeClusterCount)
    : fd_(fd),
      sectorCount_(sectorCount),
      volumeLabel_(volumeLabel),
      partitionSectorCount_(partitionSectorCount),
      fatSizeSectors_(fatSizeSectors),
      fatStartSector_(fatStartSector),
      dataStartSector_(dataStartSector),
      freeClusterCount_(freeClusterCount) {}

SectorWriter SectorWriter::make(int fd, size_t sectorCount,
                                std::string_view label) {
  static constexpr size_t kMinSectorsForMBR = 18432;  // ~9MB
  assert(sectorCount >= kMinSectorsForMBR);

  auto volumeLabel = prepareVolumeLabel(label);

  size_t partitionSectorCount = sectorCount - kPartitionAlignmentSectors;

  // FAT size calculation from the Microsoft FAT specification.
  // See canonical_file_system.md "Derived Layout Values" for derivation.
  uint64_t sectorsToAllocate = partitionSectorCount - kReservedSectors;
  uint64_t sectorsPerFatEntry = (256 * kSectorsPerCluster + kFatCount) / 2;
  uint32_t fatSizeSectors = static_cast<uint32_t>(
      (sectorsToAllocate + (sectorsPerFatEntry - 1)) / sectorsPerFatEntry);

  uint32_t fatStartSector = kPartitionAlignmentSectors + kReservedSectors;
  uint32_t dataStartSector = fatStartSector + (kFatCount * fatSizeSectors);

  uint32_t totalDataSectors = static_cast<uint32_t>(partitionSectorCount) -
                              kReservedSectors - (kFatCount * fatSizeSectors);
  uint32_t freeClusterCount =
      (totalDataSectors / kSectorsPerCluster) - 1;  // minus root dir cluster

  return SectorWriter(fd, sectorCount, volumeLabel, partitionSectorCount,
                      fatSizeSectors, fatStartSector, dataStartSector,
                      freeClusterCount);
}

// -----------------------------------------------------------------------------
// Static I/O Helpers
// -----------------------------------------------------------------------------

SDFormatResult SectorWriter::writeBytes(int fd, off_t offset,
                                        std::span<const std::byte> data) {
  if (fd < 0 || data.empty()) {
    return SDFormatResult::InvalidDevice;
  }
  if (lseek(fd, offset, SEEK_SET) == -1) {
    return SDFormatResult::IOError;
  }

  const std::byte* ptr = data.data();
  size_t remaining = data.size();
  while (remaining > 0) {
    ssize_t written = write(fd, ptr, remaining);
    if (written == -1) {
      if (errno == EINTR) {
        continue;
      }
      return SDFormatResult::IOError;
    }
    ptr += written;
    remaining -= static_cast<size_t>(written);
  }
  return SDFormatResult::Success;
}

SDFormatResult SectorWriter::writeSectors(int fd, off_t sectorLba,
                                          std::span<const std::byte> data) {
  off_t offset = sectorLba * kSectorSize;
  return writeBytes(fd, offset, data);
}

SDFormatResult SectorWriter::zeroSectors(int fd, off_t startSector,
                                         uint32_t count) {
  static constexpr uint32_t kClusterSectors = kSectorsPerCluster;
  static constexpr uint32_t kClusterBytes = kClusterSectors * kSectorSize;
  std::byte buffer[kClusterBytes] = {};

  uint32_t remaining = count;
  off_t current = startSector;

  std::println("[SDFormat] Zeroing {} sectors starting at LBA {}", count,
               static_cast<uint64_t>(startSector));

  while (remaining > 0) {
    uint32_t toWrite =
        (remaining > kClusterSectors) ? kClusterSectors : remaining;
    SDFormatResult res =
        writeSectors(fd, current, std::span{buffer, toWrite * kSectorSize});
    if (res != SDFormatResult::Success) {
      return res;
    }

    remaining -= toWrite;
    current += toWrite;
  }
  return SDFormatResult::Success;
}

// -----------------------------------------------------------------------------
// Write Operations
// -----------------------------------------------------------------------------

SDFormatResult SectorWriter::writeMBR() {
  std::byte masterBootRecord[512] = {};
  MasterBootRecord* mbr = reinterpret_cast<MasterBootRecord*>(masterBootRecord);

  mbr->partitions[0] = PartitionEntry{
      .status = 0x80,
      .chsStart = {0xFF, 0xFF, 0xFF},
      .type = kPartitionTypeFat32Lba,
      .chsEnd = {0xFF, 0xFF, 0xFF},
      .lbaStart = kPartitionAlignmentSectors,
      .sectorCount = static_cast<uint32_t>(partitionSectorCount_),
  };

  mbr->signature = kMbrSignature;

  return writeSectors(fd_, 0, std::span{masterBootRecord});
}

SDFormatResult SectorWriter::writeVolumeBootRecord() {
  const VolumeBootRecord vbr = {
      .bpb =
          {
              .totalSectors32 = static_cast<uint32_t>(partitionSectorCount_),
              .fatSize32 = fatSizeSectors_,
          },
      .volumeId = static_cast<uint32_t>(time(nullptr)),
      .volumeLabel = volumeLabel_,
  };

  std::println("[SDFormat] Writing VBR...");
  if (auto result = writeSectors(fd_, kPartitionAlignmentSectors,
                                 std::as_bytes(std::span{&vbr, 1}));
      result != SDFormatResult::Success) {
    return result;
  }

  std::println("[SDFormat] Writing Backup VBR...");
  return writeSectors(fd_, kPartitionAlignmentSectors + kBackupBootSector,
                      std::as_bytes(std::span{&vbr, 1}));
}

SDFormatResult SectorWriter::writeFSInfo() {
  const FSInfo fsinfo = {
      .freeCount = freeClusterCount_,
  };

  std::println("[SDFormat] Writing FSInfo...");
  if (auto result =
          writeSectors(fd_, kPartitionAlignmentSectors + kFsInfoSector,
                       std::as_bytes(std::span{&fsinfo, 1}));
      result != SDFormatResult::Success) {
    return result;
  }

  std::println("[SDFormat] Writing Backup FSInfo...");
  return writeSectors(fd_, kPartitionAlignmentSectors + kBackupBootSector + 1,
                      std::as_bytes(std::span{&fsinfo, 1}));
}

SDFormatResult SectorWriter::writeFat32Tables() {
  const uint32_t fatSector[128] = {
      0xFFFFFF00 | kMediaDescriptor,
      0xFFFFFFFF,
      0x0FFFFFFF,
  };

  std::println("[SDFormat] Initializing FAT tables...");

  // Zero FAT 1 & Write Header
  std::println("[SDFormat]   Zeroing FAT 1...");
  if (zeroSectors(fd_, fatStartSector_, fatSizeSectors_) !=
      SDFormatResult::Success) {
    return SDFormatResult::IOError;
  }
  if (writeSectors(fd_, fatStartSector_, std::as_bytes(std::span{fatSector})) !=
      SDFormatResult::Success) {
    return SDFormatResult::IOError;
  }

  // Zero FAT 2 & Write Header
  std::println("[SDFormat]   Zeroing FAT 2...");
  if (zeroSectors(fd_, fatStartSector_ + fatSizeSectors_, fatSizeSectors_) !=
      SDFormatResult::Success) {
    return SDFormatResult::IOError;
  }
  if (writeSectors(fd_, fatStartSector_ + fatSizeSectors_,
                   std::as_bytes(std::span{fatSector})) !=
      SDFormatResult::Success) {
    return SDFormatResult::IOError;
  }

  return SDFormatResult::Success;
}

SDFormatResult SectorWriter::writeRootDirectory() {
  std::println("[SDFormat] Initializing Root Directory...");
  std::println("[SDFormat]   Zeroing Root Directory Cluster...");

  if (zeroSectors(fd_, dataStartSector_, kSectorsPerCluster) !=
      SDFormatResult::Success) {
    return SDFormatResult::IOError;
  }

  const RootDirSector rootDirSector = {
      .volumeLabel =
          {
              .name = volumeLabel_,
          },
  };

  if (writeSectors(fd_, dataStartSector_,
                   std::as_bytes(std::span{&rootDirSector, 1})) !=
      SDFormatResult::Success) {
    return SDFormatResult::IOError;
  }

  return SDFormatResult::Success;
}

}  // namespace sdFormat
