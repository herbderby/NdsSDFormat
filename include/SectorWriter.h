#ifndef SECTOR_WRITER_H
#define SECTOR_WRITER_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "SDFormatResult.h"

namespace sdFormat {

class SectorWriter {
public:
  // Constants
  static constexpr uint32_t kSectorSize = 512;
  static constexpr uint32_t kSectorsPerCluster = 64;
  static constexpr uint32_t kClusterSize = kSectorsPerCluster * kSectorSize;
  static constexpr uint32_t kPartitionAlignmentSectors = 8192;
  static constexpr uint32_t kReservedSectors = 32;
  static constexpr uint32_t kFatCount = 2;

  // Factory
  static SectorWriter make(int fd, size_t sectorCount, std::string_view label);

  // Atomic Write Operations
  SDFormatResult writeMBR();
  SDFormatResult writeVolumeBootRecord();
  SDFormatResult writeFSInfo();
  SDFormatResult writeFat32Tables();
  SDFormatResult writeRootDirectory();

  // Accessors
  size_t sectorCount() const { return sectorCount_; }
  size_t partitionSectorCount() const { return partitionSectorCount_; }

private:
  // Private Constructor (called by make)
  SectorWriter(int fd, size_t sectorCount, std::array<char, 11> volumeLabel,
               size_t partitionSectorCount, uint32_t fatSizeSectors,
               uint32_t fatStartSector, uint32_t dataStartSector,
               uint32_t freeClusterCount);

  // Low-level I/O (static, only need fd)
  static SDFormatResult writeBytes(int fd, off_t offset,
                                   std::span<const std::byte> data);
  static SDFormatResult writeSectors(int fd, off_t sectorLba,
                                     std::span<const std::byte> data);
  static SDFormatResult zeroSectors(int fd, off_t startSector, uint32_t count);

  // Instance Members
  int fd_;
  size_t sectorCount_;
  std::array<char, 11> volumeLabel_;
  size_t partitionSectorCount_;
  uint32_t fatSizeSectors_;
  uint32_t fatStartSector_;
  uint32_t dataStartSector_;
  uint32_t freeClusterCount_;
};

}  // namespace sdFormat

#endif  // SECTOR_WRITER_H
