import NDSSDFormatCore

/// Groups the parameters for a formatting operation and provides
/// throwing wrappers around the C formatting functions.
///
/// Each method writes one logical component of the FAT32 filesystem.
/// The caller manages the file descriptor lifecycle and should call
/// methods in order: MBR, VBR, FSInfo, FAT tables, root directory.
///
/// ```swift
/// let label = try VolumeLabel("NDS")
/// let writer = try SectorWriter(
///   fd: handle.fileDescriptor,
///   sectorCount: sectorCount,
///   volumeLabel: label)
/// try writer.writeMasterBootRecord()
/// try writer.writeVolumeBootRecord()
/// try writer.writeFSInfo()
/// try writer.writeFat32Tables()
/// try writer.writeRootDirectory()
/// ```
public struct SectorWriter: Sendable {
  /// An open file descriptor with write permissions to the target device.
  ///
  /// The caller owns this descriptor and is responsible for closing it
  /// after all write methods have returned.
  private let fd: Int32

  /// Total number of 512-byte sectors on the target device.
  ///
  /// Every C formatting function receives this value so it can derive
  /// partition geometry (FAT size, data region start, free clusters).
  private let sectorCount: UInt64

  /// The validated volume label written into the VBR and root directory.
  private let label: VolumeLabel

  /// Minimum device size: 18 432 sectors (~9 MB).
  ///
  /// Below this threshold the partition is too small to hold even the
  /// reserved sectors, two FAT copies, and a single data cluster.
  private static let minimumSectorCount: UInt64 = 18_432

  /// Creates a sector writer for the given device.
  ///
  /// Validates all parameters before any I/O occurs:
  /// - The file descriptor must be positive.
  /// - The device must have at least 18 432 sectors (~9 MB).
  ///
  /// - Parameters:
  ///   - fd: An open file descriptor with write permissions.
  ///   - sectorCount: Total 512-byte sectors on the device.
  ///   - volumeLabel: A validated ``VolumeLabel``.
  /// - Throws: ``FormatterError/invalidFileDescriptor`` if `fd` is
  ///   not positive, or ``FormatterError/tooSmall`` if `sectorCount`
  ///   is below the minimum.
  public init(fd: Int32, sectorCount: UInt64, volumeLabel: VolumeLabel) throws(FormatterError) {
    guard fd > 0 else {
      throw .invalidFileDescriptor
    }
    guard sectorCount >= Self.minimumSectorCount else {
      throw .tooSmall
    }
    self.fd = fd
    self.sectorCount = sectorCount
    self.label = volumeLabel
  }

  /// Writes the Master Boot Record to absolute sector 0.
  ///
  /// - Throws: ``FormatterError`` if the write fails.
  public func writeMasterBootRecord() throws(FormatterError) {
    try check(sdFormatWriteMBR(fd, sectorCount))
  }

  /// Writes the Volume Boot Record and its backup at sector 6.
  ///
  /// - Throws: ``FormatterError`` if the write fails.
  public func writeVolumeBootRecord() throws(FormatterError) {
    try check(sdFormatWriteVolumeBootRecord(fd, sectorCount, label.cChars))
  }

  /// Writes the FSInfo sector and its backup at sector 7.
  ///
  /// - Throws: ``FormatterError`` if the write fails.
  public func writeFSInfo() throws(FormatterError) {
    try check(sdFormatWriteFSInfo(fd, sectorCount))
  }

  /// Writes and zeroes both FAT copies.
  ///
  /// - Throws: ``FormatterError`` if the write fails.
  public func writeFat32Tables() throws(FormatterError) {
    try check(sdFormatWriteFat32Tables(fd, sectorCount))
  }

  /// Writes and zeroes the root directory cluster with a volume label entry.
  ///
  /// - Throws: ``FormatterError`` if the write fails.
  public func writeRootDirectory() throws(FormatterError) {
    try check(sdFormatWriteRootDirectory(fd, sectorCount, label.cChars))
  }

  // MARK: - Private

  /// Translates a C result code into a Swift typed throw.
  ///
  /// If `result` is `SDFormatSuccess` this method returns normally.
  /// Any other value is converted to a ``FormatterError`` via
  /// ``FormatterError/init(result:)`` and thrown.
  ///
  /// - Parameter result: The result code returned by a C formatting function.
  /// - Throws: ``FormatterError`` mapped from the C result code.
  private func check(_ result: SDFormatResult) throws(FormatterError) {
    guard result == SDFormatSuccess else {
      throw FormatterError(result: result)
    }
  }
}
