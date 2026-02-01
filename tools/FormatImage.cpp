/// @file FormatImage.cpp
/// @brief Minimal C++ CLI for formatting a file image as FAT32.
///
/// Usage: format_image <path> <label> <sector-count>
///
/// Opens the file at @p path, constructs a SectorWriter with the given
/// sector count, and writes all five filesystem structures (MBR, boot
/// sector, FSInfo, FAT tables, root directory).  Exits 0 on success,
/// 1 on any failure.
///
/// This tool is intentionally minimal: no simulation, no device support,
/// no confirmation prompt.  It exists to test the C++ library in
/// isolation, without the Swift CLI.

#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <print>
#include <string>
#include <string_view>

#include "SDFormatResult.h"
#include "SectorWriter.h"

int main(int argc, char* argv[]) {
  if (argc != 4) {
    std::println(stderr, "Usage: format_image <path> <label> <sector-count>");
    return 1;
  }

  const std::string path = argv[1];
  const std::string_view label = argv[2];
  const uint64_t sectorCount = std::stoull(argv[3]);

  int fd = open(path.c_str(), O_RDWR);
  if (fd < 0) {
    std::println(stderr, "Error: Failed to open '{}'", path);
    return 1;
  }

  auto writer = sdFormat::SectorWriter::make(fd, sectorCount, label);

  struct Step {
    const char* name;
    SDFormatResult (sdFormat::SectorWriter::*method)();
  };

  const Step steps[] = {
      {"MBR", &sdFormat::SectorWriter::writeMBR},
      {"VBR", &sdFormat::SectorWriter::writeVolumeBootRecord},
      {"FSInfo", &sdFormat::SectorWriter::writeFSInfo},
      {"FAT Tables", &sdFormat::SectorWriter::writeFat32Tables},
      {"Root Directory", &sdFormat::SectorWriter::writeRootDirectory},
  };

  for (const auto& step : steps) {
    std::println("[FormatImage] Writing {}...", step.name);
    SDFormatResult result = (writer.*(step.method))();
    if (result != SDFormatResult::Success) {
      std::println(stderr, "Error: {} failed (code {})", step.name,
                   static_cast<int>(result));
      close(fd);
      return 1;
    }
  }

  close(fd);
  std::println("[FormatImage] Done.");
  return 0;
}
