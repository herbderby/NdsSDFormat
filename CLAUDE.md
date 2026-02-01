# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when
working with code in this repository.

## Project Overview

Pure C++23 library for deterministic FAT32 formatting targeting
Nintendo DS flashcarts (R4i, Acekard). Constructs MBR + FAT32
filesystem structures with 32KB clusters and 4MB alignment,
ensuring bit-perfect compatibility with ARM9 bootloaders.

This is **Repo 1** of a 3-repo dependency chain:

```text
herbderby/NDSFlashcartFormatter (App)
  depends on -> herbderby/SwiftNdsSdFormat (Swift bridge)
    depends on -> herbderby/NdsSDFormat (this repo, C++ library)
```

SPM product: `NDSSDFormatCore`

## Build Commands

```bash
# Full build (library + FormatImage CLI + test runner)
make all

# Run integration tests (4GB, 8GB, 16GB, 32GB, 64GB)
./build/test_runner

# Format a single image for manual inspection
./build/format_image test.img TESTLABEL 7813568

# Clean
make clean
```

## Directory Structure

```text
NdsSDFormat/
├── include/           # Public headers (SPM publicHeadersPath)
│   ├── SDFormatResult.h    # Result/error codes
│   └── SectorWriter.h      # Main API
├── src/
│   └── SectorWriter.cpp    # Implementation
├── tools/
│   └── FormatImage.cpp     # Minimal C++ CLI for testing
├── tests/
│   └── integration_runner.cpp  # hdiutil/fsck/mount tests
├── Makefile           # C++ build (library + tools + tests)
└── Package.swift      # SPM consumable C++ library
```

## Architecture

Single class design: `sdFormat::SectorWriter`

- **Factory**: `SectorWriter::make(fd, sectorCount, label)` —
  pre-calculates all derived layout values (FAT size, data start
  sector, free cluster count) from fundamentals.
- **Write methods**: `writeMBR()`, `writeFat32BootSector()`,
  `writeFSInfo()`, `writeFat32Tables()`, `writeRootDirectory()` —
  consume pre-calculated members, return `SDFormatResult`.
- **Static I/O helpers**: `writeBytes()`, `writeSectors()`,
  `zeroSectors()` — low-level I/O, only need fd.
- **Non-owning**: Caller manages FD lifecycle.

## Engineering Standards

### C++

- C++23 with `-Wall -Wextra -Wpedantic -Werror` (zero warnings)
- **No exceptions** — code bridges to Swift via C. Use result codes.
- File names: PascalCase matching primary class name
- Naming: `PascalCase` types, `camelCase` variables, `camelCase_`
  members (trailing underscore)
- Include guards: `#ifndef FILENAME_H` (`#pragma once` forbidden)
- Include order: related header, C system, C++ stdlib, project headers
- Pointer style: `Type* variable` (asterisk with type)
- Use `std::print`/`std::println` (not `printf`/`cout`)
- Use `std::byte` for raw binary buffers
- Packed structs must have `static_assert` on size; never reorder
  members (they map to binary disk formats)
- Zero-init with aggregate initialization (`= {}`), not `memset`
- `static constexpr` over `#define` macros
- `static_cast<>()` never C-style casts
- Calculated `constexpr` values over hardcoded magic numbers
- Always use braces for control flow bodies
- Class layout: Types -> Constants -> Factory -> Lifecycle ->
  Public -> Accessors -> Private Members

### General

- Dead code must be deleted immediately, not commented out
- Cleanup/formatting changes must be separate atomic commits

## Git Workflow

- Never use `git add .` — add files individually by path
- Write commit message to a `commit_message` file for user review
- Commit with `git commit -F commit_message && rm commit_message`
- Subject line: max 50 chars, imperative mood, type prefix
  (`feat:`, `fix:`, `refactor:`, `test:`, `perf:`, `chore:`,
  `docs:`, `style:`)
- Body: wrap at 72 chars, explain *why* not *what*

## Markdown

- Lines wrapped at 80 characters (except code blocks, URLs, tables)
- 2-space indentation for nested list items
- Blank lines before and after headers and lists

## Key Technical Details

- Cluster size is always 32KB (critical for DS flashcart compatibility)
- 4MB (8192 sectors) alignment for NAND flash optimization
- FAT[0] and FAT[1] must have high bits set (clean shutdown + no
  error flags)
- MBR CHS values must be `0xFF, 0xFF, 0xFF` for LBA partitions
  (macOS requirement)
- Root directory needs explicit `ATTR_VOLUME_ID` entry (BPB label
  alone is insufficient)

## SPM Notes

- `Package.swift` **must** declare `platforms: [.macOS(.v26)]`.
  Without this, SPM defaults to a deployment target too low for
  C++23 `<print>` (which uses `std::to_chars` internally, unavailable
  before macOS 13.3).
- The library is consumed by `SwiftNdsSdFormat` via
  `.package(url: "https://github.com/herbderby/NdsSDFormat.git",
  from: "1.0.1")`.
- Never re-tag a released version. SPM caches commit hashes per tag
  and will refuse to resolve if the hash changes. Always bump the
  version number.

## Test Status

Integration tests (`tests/integration_runner.cpp`) verify formatting
across multiple SD card sizes:

1. Create disk image with random data pollution
2. Format via `./build/format_image`
3. Attach with `hdiutil`
4. Verify filesystem integrity with `fsck_msdos`
5. Verify mountability with `diskutil mount`

All 5 sizes (4GB, 8GB, 16GB, 32GB, 64GB) pass as of 2026-01-31.
Tests do not require `sudo`.
