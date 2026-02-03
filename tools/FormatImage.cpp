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

#include <cstdlib>
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
    std::println(stderr, "Error: Failed to open '{}'", path);
    return 1;
  }

  SDFormatResult r;

  std::println("[FormatImage] Writing MBR...");
  r = sdFormatWriteMBR(fd, sectorCount);
  if (r != SDFormatSuccess) {
    std::println(stderr, "Error: MBR failed (code {})", static_cast<int>(r));
    close(fd);
    return 1;
  }

  std::println("[FormatImage] Writing VBR...");
  r = sdFormatWriteVolumeBootRecord(fd, sectorCount, label);
  if (r != SDFormatSuccess) {
    std::println(stderr, "Error: VBR failed (code {})", static_cast<int>(r));
    close(fd);
    return 1;
  }

  std::println("[FormatImage] Writing FSInfo...");
  r = sdFormatWriteFSInfo(fd, sectorCount);
  if (r != SDFormatSuccess) {
    std::println(stderr, "Error: FSInfo failed (code {})", static_cast<int>(r));
    close(fd);
    return 1;
  }

  std::println("[FormatImage] Writing FAT Tables...");
  r = sdFormatWriteFat32Tables(fd, sectorCount);
  if (r != SDFormatSuccess) {
    std::println(stderr, "Error: FAT Tables failed (code {})",
                 static_cast<int>(r));
    close(fd);
    return 1;
  }

  std::println("[FormatImage] Writing Root Directory...");
  r = sdFormatWriteRootDirectory(fd, sectorCount, label);
  if (r != SDFormatSuccess) {
    std::println(stderr, "Error: Root Directory failed (code {})",
                 static_cast<int>(r));
    close(fd);
    return 1;
  }

  close(fd);
  std::println("[FormatImage] Done.");
  return 0;
}
