// swift-tools-version: 6.2
import PackageDescription

let package = Package(
  name: "NdsSDFormat",
  platforms: [.macOS(.v26)],
  products: [
    .library(
      name: "NDSSDFormatCore",
      targets: ["NDSSDFormatCore"]),
    .library(
      name: "NDSSDFormat",
      targets: ["NDSSDFormat"]),
    .executable(
      name: "SDFormat",
      targets: ["SDFormat"]),
  ],
  dependencies: [
    .package(
      url: "https://github.com/apple/swift-argument-parser",
      from: "1.3.0"),
    .package(
      url: "https://github.com/apple/swift-log.git",
      from: "1.6.1"),
  ],
  targets: [
    .target(
      name: "NDSSDFormatCore",
      path: ".",
      sources: ["src/SDFormat.cpp"],
      publicHeadersPath: "include",
      cxxSettings: [
        .unsafeFlags([
          "-std=c++23",
          "-Wall",
          "-Wextra",
          "-Wpedantic",
          "-Werror",
        ])
      ]
    ),
    .target(
      name: "NDSSDFormat",
      dependencies: ["NDSSDFormatCore"],
      path: "Sources/NDSSDFormat",
      swiftSettings: [
        .swiftLanguageMode(.v6),
        .enableExperimentalFeature("StrictConcurrency"),
      ]
    ),
    .executableTarget(
      name: "SDFormat",
      dependencies: [
        "NDSSDFormat",
        .product(name: "ArgumentParser", package: "swift-argument-parser"),
        .product(name: "Logging", package: "swift-log"),
      ],
      path: "Sources/SDFormat",
      swiftSettings: [
        .swiftLanguageMode(.v6),
        .enableExperimentalFeature("StrictConcurrency"),
      ]
    ),
    .testTarget(
      name: "NDSSDFormatTests",
      dependencies: ["NDSSDFormat"],
      path: "tests/NDSSDFormatTests",
      swiftSettings: [
        .swiftLanguageMode(.v6)
      ]
    ),
  ]
)
