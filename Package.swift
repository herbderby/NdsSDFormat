// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "NdsSDFormat",
    platforms: [.macOS(.v26)],
    products: [
        .library(name: "NDSSDFormatCore",
                 targets: ["NDSSDFormatCore"]),
        .library(name: "NDSSDFormat",
                 targets: ["NDSSDFormat"]),
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
        .testTarget(
            name: "NDSSDFormatTests",
            dependencies: ["NDSSDFormat"],
            path: "tests/NDSSDFormatTests",
            swiftSettings: [
                .swiftLanguageMode(.v6),
            ]
        ),
    ]
)
