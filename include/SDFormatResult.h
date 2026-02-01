#ifndef SD_FORMAT_RESULT_H
#define SD_FORMAT_RESULT_H

/**
 * @brief Result codes for SD Card Formatter operations.
 */
enum class SDFormatResult {
  Success = 0,
  AccessDenied,
  DeviceBusy,
  InvalidDevice,
  IOError,
  TooSmall,
  UnknownError
};

#endif  // SD_FORMAT_RESULT_H
