/// @file FormatImage.cpp
/// @brief Minimal C++ CLI for formatting a file image as FAT32.
///
/// Usage: format_image <path> <label> <sector-count>
///
/// Opens the file at @p path and writes all five filesystem structures
/// (MBR, VBR, FSInfo, FAT tables, root directory).  Exits 0 on
/// success, 1 on any failure.
///
/// This tool is intentionally minimal: no simulation, no device support,
/// no confirmation prompt.  It exists to test the C++ library in
/// isolation, without the Swift CLI.

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <print>
#include <string>

#include "SDFormat.h"

int main(int argc, char* argv[]) {
  if (argc != 4) {
    std::println(stderr, "Usage: format_image <path> <label> <sector-count>");
    return 1;
  }

  const std::string path = argv[1];
  const char* label = argv[2];
  const uint64_t sectorCount = std::stoull(argv[3]);

  int fd = open(path.c_str(), O_RDWR);
  if (fd < 0) {
    std::println(stderr, "Error: Failed to open '{}': {}", path,
                 strerror(errno));
    return 1;
  }

  int err;

  std::println("[FormatImage] Writing MBR...");
  err = sdFormatWriteMBR(fd, sectorCount);
  if (err != 0) {
    std::println(stderr, "Error: MBR failed: {}", strerror(err));
    close(fd);
    return 1;
  }

  std::println("[FormatImage] Writing VBR...");
  err = sdFormatWriteVolumeBootRecord(fd, sectorCount, label);
  if (err != 0) {
    std::println(stderr, "Error: VBR failed: {}", strerror(err));
    close(fd);
    return 1;
  }

  std::println("[FormatImage] Writing FSInfo...");
  err = sdFormatWriteFSInfo(fd, sectorCount);
  if (err != 0) {
    std::println(stderr, "Error: FSInfo failed: {}", strerror(err));
    close(fd);
    return 1;
  }

  std::println("[FormatImage] Writing FAT Tables...");
  err = sdFormatWriteFat32Tables(fd, sectorCount);
  if (err != 0) {
    std::println(stderr, "Error: FAT Tables failed: {}", strerror(err));
    close(fd);
    return 1;
  }

  std::println("[FormatImage] Writing Root Directory...");
  err = sdFormatWriteRootDirectory(fd, sectorCount, label);
  if (err != 0) {
    std::println(stderr, "Error: Root Directory failed: {}", strerror(err));
    close(fd);
    return 1;
  }

  close(fd);
  std::println("[FormatImage] Done.");
  return 0;
}
