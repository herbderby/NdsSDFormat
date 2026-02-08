import ArgumentParser
import Foundation
import Logging
import NDSSDFormat

@main
struct SDFormat: AsyncParsableCommand {
  static let configuration = CommandConfiguration(
    abstract: "Format an SD card for Nintendo DS flashcarts (FAT32/MBR).",
    discussion: """
      Writes a deterministic FAT32 filesystem with 32KB clusters, MBR
      partition scheme, and 4MB alignment for R4i/Acekard flashcart
      compatibility.

      By default, treats the target as a block device: unmounts,
      detects capacity via Disk Arbitration, and prompts for
      confirmation before writing. Use --file for disk image files.
      """)

  @Argument(help: "Path to device node (e.g., /dev/rdisk4) or file.")
  var targetPath: String

  @Argument(help: "Volume label (1–11 ASCII characters).")
  var volumeLabel: String

  @Flag(name: .long, help: "Treat target as a regular file (skip unmount and confirmation).")
  var file: Bool = false

  mutating func run() async throws {
    var logger = Logger(label: "SDFormat")
    logger.logLevel = .info

    // Validate volume label before any I/O.
    let label: VolumeLabel
    do {
      label = try VolumeLabel(volumeLabel)
    } catch {
      logger.error("Invalid volume label: \(error)")
      throw ExitCode.failure
    }

    let byteCount: UInt64

    if file {
      // File mode: get size from stat, no unmount, no confirmation.
      logger.info("File mode: \(targetPath)")
      do {
        byteCount = try SDCardManager.fileByteCount(of: targetPath)
      } catch {
        logger.error("\(error.localizedDescription)")
        throw ExitCode.failure
      }
    } else {
      // Device mode: unmount, detect capacity, confirm with user.
      logger.info("Device mode: \(targetPath)")

      logger.info("Unmounting \(targetPath)...")
      do {
        try await SDCardManager.unmount(at: targetPath)
      } catch {
        logger.error("\(error.localizedDescription)")
        throw ExitCode.failure
      }

      do {
        byteCount = try SDCardManager.deviceByteCount(of: targetPath)
      } catch {
        logger.error("\(error.localizedDescription)")
        throw ExitCode.failure
      }

      let gigabytes = Double(byteCount) / 1_000_000_000.0
      print(
        """
        WARNING: You are about to ERASE \(targetPath) \
        (\(String(format: "%.1f", gigabytes)) GB) and format it as FAT32.
        All data will be lost.
        """)
      print("Type 'yes' to continue: ", terminator: "")

      guard let input = readLine(), input == "yes" else {
        print("Operation cancelled.")
        throw ExitCode.failure
      }
    }

    logger.info(
      "Capacity: \(byteCount) bytes (\(byteCount / 512) sectors)")

    // Open the target.
    let openFlags: Int32 = file ? O_RDWR : (O_RDWR | O_EXLOCK)
    let fd = open(targetPath, openFlags)
    guard fd >= 0 else {
      logger.error(
        "Failed to open \(targetPath): \(String(cString: strerror(errno)))")
      throw ExitCode.failure
    }
    defer { close(fd) }

    // Create the sector writer.
    let writer: SectorWriter
    do {
      writer = try SectorWriter(
        fd: fd, byteCount: byteCount, volumeLabel: label)
    } catch {
      logger.error("\(error.localizedDescription)")
      throw ExitCode.failure
    }

    // Write all five filesystem structures.
    do {
      logger.info("Writing MBR...")
      try writer.writeMasterBootRecord()

      logger.info("Writing VBR...")
      try writer.writeVolumeBootRecord()

      logger.info("Writing FSInfo...")
      try writer.writeFSInfo()

      logger.info("Writing FAT tables...")
      try writer.writeFat32Tables()

      logger.info("Writing root directory...")
      try writer.writeRootDirectory()
    } catch {
      logger.error("\(error.localizedDescription)")
      throw ExitCode.failure
    }

    logger.info("Formatting complete.")
  }
}
