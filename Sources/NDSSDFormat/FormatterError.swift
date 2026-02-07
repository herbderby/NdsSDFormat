import NDSSDFormatCore

/// Errors thrown by ``SectorWriter`` operations.
///
/// The ``invalidFileDescriptor`` and ``tooSmall`` cases are caught
/// during initialization. The remaining cases map to result codes
/// from the C formatting library.
public enum FormatterError: Error, Equatable, Sendable {
  /// The file descriptor is not positive.
  case invalidFileDescriptor
  /// The operating system denied write access to the device.
  case accessDenied
  /// The device is in use by another process.
  case deviceBusy
  /// The device path does not refer to a valid block device.
  case invalidDevice
  /// A read or write operation failed.
  case ioError
  /// The device has fewer sectors than the minimum required for FAT32.
  case tooSmall
  /// The C library returned an unrecognized result code.
  case unknown(UInt32)

  /// Creates a `FormatterError` from a C `SDFormatResult` code.
  ///
  /// - Parameter result: The result code returned by a C formatting function.
  public init(result: SDFormatResult) {
    switch result {
    case SDFormatAccessDenied: self = .accessDenied
    case SDFormatDeviceBusy: self = .deviceBusy
    case SDFormatInvalidDevice: self = .invalidDevice
    case SDFormatIOError: self = .ioError
    case SDFormatTooSmall: self = .tooSmall
    default: self = .unknown(result.rawValue)
    }
  }

  /// A human-readable description of the error suitable for logging.
  ///
  /// Each case returns a short, plain-English phrase describing the
  /// failure. For ``unknown(_:)`` the raw numeric code is included
  /// so it can be cross-referenced with `SDFormatResult` values.
  public var localizedDescription: String {
    switch self {
    case .invalidFileDescriptor: "Invalid file descriptor"
    case .accessDenied: "Access denied"
    case .deviceBusy: "Device busy"
    case .invalidDevice: "Invalid device"
    case .ioError: "I/O error"
    case .tooSmall: "Device too small"
    case .unknown(let code): "Unknown error (code: \(code))"
    }
  }
}
