// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "NdsSDFormat",
    products: [
        .library(name: "NDSSDFormatCore",
                 targets: ["NDSSDFormatCore"])
    ],
    targets: [
        .target(
            name: "NDSSDFormatCore",
            path: ".",
            sources: ["src/SectorWriter.cpp"],
            publicHeadersPath: "include",
            cxxSettings: [
                .unsafeFlags(["-std=c++23"])
            ]
        )
    ]
)
