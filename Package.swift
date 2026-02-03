// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "NdsSDFormat",
    platforms: [.macOS(.v26)],
    products: [
        .library(name: "NDSSDFormatCore",
                 targets: ["NDSSDFormatCore"])
    ],
    targets: [
        .target(
            name: "NDSSDFormatCore",
            path: ".",
            sources: ["src/SDFormat.cpp"],
            publicHeadersPath: "include",
            cxxSettings: [
                .unsafeFlags(["-std=c++23"])
            ]
        )
    ]
)
