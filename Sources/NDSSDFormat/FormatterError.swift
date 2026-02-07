import Darwin

/// Errors thrown by ``SectorWriter`` operations.
///
/// The ``invalidFileDescriptor`` and ``tooSmall`` cases are caught
/// during initialization. The ``ioError(_:)`` case wraps the `errno`
/// value from a failed I/O system call in the C formatting library.
public enum FormatterError: Error, Equatable, Sendable {
  /// The file descriptor is not positive.
  case invalidFileDescriptor
  /// The device is smaller than the minimum required for formatting.
  ///
  /// Both values are in bytes.
  case tooSmall(actual: UInt64, minimum: UInt64)
  /// A system call failed during I/O. The associated value is `errno`.
  case ioError(Int32)

  /// Creates a `FormatterError` from an `errno` value returned by a
  /// C formatting function.
  ///
  /// - Parameter errno: The `errno` value from the failed I/O call.
  ///   Must be non-zero.
  public init(errno: Int32) {
    self = .ioError(errno)
  }

  /// A human-readable description of the error suitable for logging.
  ///
  /// For ``ioError(_:)`` the system's `strerror` message is included
  /// so the user sees a meaningful explanation (e.g. "Permission denied").
  public var localizedDescription: String {
    switch self {
    case .invalidFileDescriptor: "Invalid file descriptor"
    case .tooSmall(let actual, let minimum):
      "Device too small: \(actual) bytes, need at least \(minimum) bytes"
    case .ioError(let code):
      "I/O error: \(String(cString: strerror(code))) (errno: \(code))"
    }
  }
}
