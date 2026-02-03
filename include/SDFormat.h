#ifndef SD_FORMAT_H
#define SD_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SDFormatSuccess = 0,
  SDFormatAccessDenied,
  SDFormatDeviceBusy,
  SDFormatInvalidDevice,
  SDFormatIOError,
  SDFormatTooSmall,
  SDFormatUnknownError
} SDFormatResult;

SDFormatResult sdFormatWriteMBR(int fd, uint64_t sectorCount);
SDFormatResult sdFormatWriteVolumeBootRecord(int fd, uint64_t sectorCount,
                                             const char* label);
SDFormatResult sdFormatWriteFSInfo(int fd, uint64_t sectorCount);
SDFormatResult sdFormatWriteFat32Tables(int fd, uint64_t sectorCount);
SDFormatResult sdFormatWriteRootDirectory(int fd, uint64_t sectorCount,
                                          const char* label);

#ifdef __cplusplus
}
#endif

#endif  // SD_FORMAT_H
