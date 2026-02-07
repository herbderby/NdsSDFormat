/// A validated FAT32 volume label, ready to pass to C formatting functions.
///
/// FAT32 volume labels must be 1--11 printable ASCII characters,
/// excluding characters forbidden in 8.3 short names. The initializer
/// validates these constraints and stores the result as a null-terminated
/// `[CChar]` array for zero-cost bridging to `const char*`.
///
/// ```swift
/// let label = try VolumeLabel("NDS")
/// ```
public struct VolumeLabel: Sendable, Equatable {
  /// The null-terminated C string representation of the label.
  ///
  /// Stored as `[CChar]` rather than `String` so Swift can bridge it
  /// to `const char*` automatically at C call sites, avoiding the
  /// typed-throws erasure that `String.withCString` causes.
  internal let cChars: [CChar]

  /// The maximum number of characters in a FAT32 volume label.
  ///
  /// FAT32 directory entries reserve exactly 11 bytes for the volume
  /// label field (`DIR_Name`), with no null terminator on disk.
  public static let maxLength = 11

  /// Characters forbidden in FAT32 volume labels.
  ///
  /// These are the same characters forbidden in 8.3 short filenames,
  /// as specified in the Microsoft FAT specification (section 6.1).
  private static let forbiddenCharacters: Set<Character> = [
    "\"", "*", "+", ",", ".", "/", ":", ";", "<", "=", ">", "?",
    "[", "\\", "]", "|",
  ]

  /// Creates a validated volume label from a string.
  ///
  /// - Parameter string: The label text (1--11 printable ASCII characters).
  /// - Throws: ``ValidationError/empty`` if `string` is empty,
  ///   ``ValidationError/tooLong`` if it exceeds 11 characters, or
  ///   ``ValidationError/invalidCharacter(_:)`` if it contains
  ///   non-ASCII, non-printable, or FAT32-forbidden characters.
  public init(_ string: String) throws(ValidationError) {
    guard !string.isEmpty else {
      throw .empty
    }
    guard string.count <= Self.maxLength else {
      throw .tooLong
    }
    for ch in string {
      guard ch.asciiValue != nil, ch >= " ", ch <= "~" else {
        throw .invalidCharacter(ch)
      }
      guard !Self.forbiddenCharacters.contains(ch) else {
        throw .invalidCharacter(ch)
      }
    }
    self.cChars = Array(string.utf8CString)
  }
}

extension VolumeLabel {
  /// Errors thrown when a volume label string fails validation.
  public enum ValidationError: Error, Equatable, Sendable {
    /// The label string is empty.
    case empty
    /// The label string exceeds 11 characters.
    case tooLong
    /// The label contains a non-printable-ASCII or FAT32-forbidden character.
    case invalidCharacter(Character)
  }
}
