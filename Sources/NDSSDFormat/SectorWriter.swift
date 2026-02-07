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
///   byteCount: deviceSize,
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
  /// Derived from the byte count provided at initialization. Every C
  /// formatting function receives this value so it can derive partition
  /// geometry (FAT size, data region start, free clusters).
  private let sectorCount: UInt64

  /// The validated volume label written into the VBR and root directory.
  private let label: VolumeLabel

  /// Minimum device size: 512 MB (decimal).
  ///
  /// SD cards use decimal sizing (1 MB = 1,000,000 bytes), so a
  /// marketed "512 MB" card is 512,000,000 bytes. Devices smaller
  /// than this are almost certainly not valid targets for formatting.
  public static let minimumByteCount: UInt64 = 512_000_000

  /// Creates a sector writer for the given device.
  ///
  /// Validates all parameters before any I/O occurs:
  /// - The file descriptor must be positive.
  /// - The device must be at least 512 MB.
  ///
  /// - Parameters:
  ///   - fd: An open file descriptor with write permissions.
  ///   - byteCount: Total size of the device in bytes.
  ///   - volumeLabel: A validated ``VolumeLabel``.
  /// - Throws: ``FormatterError/invalidFileDescriptor`` if `fd` is
  ///   not positive, or ``FormatterError/tooSmall(actual:minimum:)``
  ///   if `byteCount` is below the minimum.
  public init(fd: Int32, byteCount: UInt64, volumeLabel: VolumeLabel) throws(FormatterError) {
    guard fd > 0 else {
      throw .invalidFileDescriptor
    }
    guard byteCount >= Self.minimumByteCount else {
      throw .tooSmall(actual: byteCount, minimum: Self.minimumByteCount)
    }
    self.fd = fd
    self.sectorCount = byteCount / 512
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

  /// Translates a C errno return into a Swift typed throw.
  ///
  /// If `errno` is 0 this method returns normally. Any non-zero value
  /// is wrapped in ``FormatterError/ioError(_:)`` and thrown.
  ///
  /// - Parameter errno: The value returned by a C formatting function
  ///   (0 on success, or an `errno` code on failure).
  /// - Throws: ``FormatterError/ioError(_:)`` when `errno` is non-zero.
  private func check(_ errno: Int32) throws(FormatterError) {
    guard errno == 0 else {
      throw FormatterError(errno: errno)
    }
  }
}
