import Testing
@testable import NDSSDFormat

@Suite("VolumeLabel Validation")
struct VolumeLabelTests {
  @Test func acceptsShortLabel() throws {
    let label = try VolumeLabel("NDS")
    #expect(label.cChars == Array("NDS".utf8CString))
  }

  @Test func acceptsSingleCharacter() throws {
    let label = try VolumeLabel("A")
    #expect(label.cChars == Array("A".utf8CString))
  }

  @Test func acceptsMaxLengthLabel() throws {
    let label = try VolumeLabel("12345678901")
    #expect(label.cChars.count == 12) // 11 chars + null terminator
  }

  @Test func acceptsLabelWithSpaces() throws {
    _ = try VolumeLabel("MY CARD")
  }

  @Test func rejectsEmptyLabel() {
    #expect(throws: VolumeLabel.ValidationError.empty) {
      try VolumeLabel("")
    }
  }

  @Test func rejectsTooLongLabel() {
    #expect(throws: VolumeLabel.ValidationError.tooLong) {
      try VolumeLabel("123456789012")
    }
  }

  @Test(arguments: ["\"", "*", "+", ",", ".", "/", ":", ";",
                     "<", "=", ">", "?", "[", "\\", "]", "|"])
  func rejectsForbiddenCharacter(_ ch: String) {
    let input = "A\(ch)B"
    #expect(throws: VolumeLabel.ValidationError.invalidCharacter(Character(ch))) {
      try VolumeLabel(input)
    }
  }

  @Test func rejectsNonAscii() {
    #expect(throws: VolumeLabel.ValidationError.self) {
      try VolumeLabel("CAFÉ")
    }
  }

  @Test func rejectsControlCharacter() {
    #expect(throws: VolumeLabel.ValidationError.self) {
      try VolumeLabel("AB\u{01}C")
    }
  }

  @Test func equatableConformance() throws {
    let a = try VolumeLabel("NDS")
    let b = try VolumeLabel("NDS")
    let c = try VolumeLabel("OTHER")
    #expect(a == b)
    #expect(a != c)
  }
}
