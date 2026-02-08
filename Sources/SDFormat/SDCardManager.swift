import DiskArbitration
import Foundation
import NDSSDFormat

/// Manages SD card device operations: capacity detection and unmounting.
///
/// Capacity detection uses the macOS Disk Arbitration framework for
/// block devices and `stat` for regular files. Unmounting delegates
/// to `diskutil unmountDisk`.
enum SDCardManager {

  /// Expected sector size for SD cards (512 bytes).
  private static let expectedBlockSize: UInt64 = 512

  /// Returns the byte count of the block device at `path`.
  ///
  /// Queries `kDADiskDescriptionMediaBlockSizeKey` and
  /// `kDADiskDescriptionMediaSizeKey` from the Disk Arbitration
  /// framework. Validates that the block size is exactly 512 bytes.
  ///
  /// - Parameter path: A device node (e.g., `/dev/rdisk4`).
  /// - Returns: The total device size in bytes.
  /// - Throws: ``FormatterError/invalidDevice`` if the device cannot
  ///   be queried or has an unexpected block size.
  static func deviceByteCount(of path: String) throws(FormatterError) -> UInt64 {
    guard let session = DASessionCreate(kCFAllocatorDefault) else {
      throw .invalidDevice
    }

    // Strip /dev/ prefix and raw device "r" prefix (e.g.,
    // "/dev/rdisk4" -> "disk4") since Disk Arbitration expects
    // block device names.
    var bsdName = String(path.dropFirst("/dev/".count))
    if bsdName.hasPrefix("r") {
      bsdName = String(bsdName.dropFirst())
    }

    guard
      let disk = DADiskCreateFromBSDName(
        kCFAllocatorDefault, session, bsdName)
    else {
      throw .invalidDevice
    }

    guard let cfDescription = DADiskCopyDescription(disk) else {
      throw .invalidDevice
    }

    let description = cfDescription as NSDictionary

    guard
      let blockSize = description[
        kDADiskDescriptionMediaBlockSizeKey] as? UInt64,
      let mediaSize = description[
        kDADiskDescriptionMediaSizeKey] as? UInt64
    else {
      throw .invalidDevice
    }

    guard blockSize == expectedBlockSize else {
      throw .invalidDevice
    }

    return mediaSize
  }

  /// Returns the byte count of the regular file at `path`.
  ///
  /// Uses `stat` to read the file size. Suitable for disk image
  /// files used during testing.
  ///
  /// - Parameter path: A path to a regular file.
  /// - Returns: The file size in bytes.
  /// - Throws: ``FormatterError/accessDenied`` if `stat` fails, or
  ///   ``FormatterError/invalidDevice`` if the file size is zero
  ///   or negative.
  static func fileByteCount(of path: String) throws(FormatterError) -> UInt64 {
    var statBuffer = stat()
    guard stat(path, &statBuffer) == 0 else {
      throw .accessDenied
    }
    guard statBuffer.st_size > 0 else {
      throw .invalidDevice
    }
    return UInt64(statBuffer.st_size)
  }

  /// Unmounts the disk at `path` using `diskutil unmountDisk`.
  ///
  /// - Parameter path: The device path to unmount (e.g., `/dev/disk4`).
  /// - Throws: ``FormatterError/deviceBusy`` if `diskutil` returns a
  ///   non-zero exit status, or ``FormatterError/ioError(_:)`` if the
  ///   process cannot be launched.
  static func unmount(at path: String) async throws(FormatterError) {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: "/usr/sbin/diskutil")
    process.arguments = ["unmountDisk", path]

    // Discard stdout and stderr.
    process.standardOutput = Pipe()
    process.standardError = Pipe()

    do {
      try await withCheckedThrowingContinuation { continuation in
        process.terminationHandler = { proc in
          if proc.terminationStatus == 0 {
            continuation.resume()
          } else {
            continuation.resume(throwing: FormatterError.deviceBusy)
          }
        }

        do {
          try process.run()
        } catch {
          continuation.resume(throwing: FormatterError.ioError(errno))
        }
      }
    } catch let error as FormatterError {
      throw error
    } catch {
      throw FormatterError.ioError(errno)
    }
  }
}
