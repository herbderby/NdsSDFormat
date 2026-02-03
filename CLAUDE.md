# CLAUDE.md

This file provides guidance to Claude Code when working with code in
this repository.

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

# Format all C++ source files
clang-format -i include/*.h src/*.cpp tools/*.cpp tests/*.cpp

# Clean
make clean
```

## Directory Structure

```text
NdsSDFormat/
├── include/           # Public headers (SPM publicHeadersPath)
│   └── SDFormat.h         # C API (result codes + write functions)
├── src/
│   └── SDFormat.cpp       # Implementation
├── tools/
│   └── FormatImage.cpp    # Minimal C++ CLI for testing
├── tests/
│   └── integration_runner.cpp  # hdiutil/fsck/mount tests
├── .clang-format      # Shared clang-format config (from SD_Card_Formatter)
├── Makefile           # C++ build (library + tools + tests)
└── Package.swift      # SPM consumable C++ library
```

## Architecture

Free C functions with `extern "C"` linkage. Each function takes
only what it needs — no class, no state.

- **Write functions**: `sdFormatWriteMBR(fd, sectorCount)`,
  `sdFormatWriteVolumeBootRecord(fd, sectorCount, label)`,
  `sdFormatWriteFSInfo(fd, sectorCount)`,
  `sdFormatWriteFat32Tables(fd, sectorCount)`,
  `sdFormatWriteRootDirectory(fd, sectorCount, label)` —
  return `SDFormatResult`.
- **Derived layout**: `partitionSectorCount()`,
  `fatSizeSectors()`, `dataStartSector()`, `freeClusterCount()`
  — file-scoped `static` functions (implementation detail).
- **I/O helpers**: `writeBytes()`, `writeSectors()`,
  `zeroSectors()` — file-scoped `static` functions.
- **Non-owning**: Caller manages FD lifecycle.

## Engineering Standards

See `~/.claude/standards/` for shared conventions:

- `base.md` — Core principles (dead code deletion, atomic commits)
- `cpp.md` — C++23 standards (naming, style, packed structs)
- `git.md` — Commit workflow (no `git add .`, message file, prefixes)
- `markdown.md` — Formatting rules (80-char wrap, 2-space indent)

### Project-Specific: clang-format

Run `clang-format -i include/*.h src/*.cpp tools/*.cpp tests/*.cpp`
before committing C++ changes.

**Known limitation**: clang-format splits nested designated
initializer braces onto a new line (e.g., `.bpb =\n{` instead of
`.bpb = {`). No config workaround exists as of clang-format 21.
Accept the tool's output rather than fighting it with
`// clang-format off`.

## Documentation Standards

- Reference docs should build from fundamentals: define basic
  concepts (sector, LBA, CHS) before using them in structure
  definitions.
- Formulas must use descriptive variable names, never opaque
  temporaries (`tmpVal1`). If transcribing a spec formula,
  rename its variables and explain the original reasoning.
- Every on-disk structure written by the library must be
  documented in `canonical_file_system.md`, including backup
  copies (backup VBR, backup FSInfo, backup FAT).
- When a spec uses magic constants (e.g., `256`, `/ 2`),
  explain the derivation in terms of byte sizes and entry
  counts so the reader can verify the arithmetic.

## Canonical Naming

`docs/canonical_file_system.md` is the authoritative reference for
on-disk names. Canonical naming drives **all** identifiers in
`SDFormat.cpp` — struct names, function names, struct field
names, constant names, and local variable names in formulas
(e.g., `kFatCount` not `kNumberFats`,
`partitionSectorCount` not `mbrPartitionSectorCount`,
`sectorsToAllocate` not `tmpSize`).

Key terminology:

- **VBR** (Volume Boot Record) = the entire first sector of the
  partition (also called "boot sector" or "0th sector").
- **BPB** (BIOS Parameter Block) = the sub-structure *within* the
  VBR that describes volume geometry (offsets 0x00B–0x033).
- Use `VBR_` prefix for boot sector fields outside the BPB (the MS
  spec uses `BS_`, but `VBR_` is clearer and avoids the `BS_`/`BPB_`
  visual collision).
- Prefixes: `MBR_`, `PE_`, `VBR_`, `BPB_`, `FSI_`, `DIR_`.

Reference documents in `docs/`:

- `canonical_file_system.md` — harmonized field name mapping
  (single source of truth for all on-disk names)
- `mbr_x86_design.md` — MBR bootstrap and partition table
  reference (from OSDev Wiki / IBM PC/AT)
- `microsoft_fat_specification.md` — MS FAT spec (August 2005)
- `fat_file_system_design.md` — Wikipedia design reference

When adding or updating reference docs, always include a
"Derived from" section listing source material. Cross-reference
canonical names from `canonical_file_system.md` wherever
applicable so the docs stay connected.

## Key Technical Details

- Cluster size is always 32KB (critical for DS flashcart compatibility)
- 4MB (8192 sectors) alignment for NAND flash optimization
- FAT[0] and FAT[1] must have high bits set (clean shutdown + no
  error flags)
- MBR CHS values must be `0xFF, 0xFF, 0xFF` for LBA partitions
  (macOS requirement)
- Root directory needs explicit `ATTR_VOLUME_ID` entry (BPB label
  alone is insufficient)
- `sdFormatWriteFSInfo()` writes both primary (sector 1) and
  backup (sector 7) copies; `sdFormatWriteVolumeBootRecord()`
  writes both primary (sector 0) and backup (sector 6) copies
- The FAT size formula in `fatSizeSectors()` follows the MS spec
  derivation documented in `canonical_file_system.md` — use
  `sectorsToAllocate` and `sectorsPerFatEntry` as variable names,
  not opaque arithmetic

## SPM Notes

- `Package.swift` **must** declare `platforms: [.macOS(.v26)]`.
  Without this, SPM defaults to a deployment target too low for
  C++23 `<print>` (which uses `std::to_chars` internally, unavailable
  before macOS 13.3).
- The library is consumed by `SwiftNdsSdFormat` via
  `.package(url: "https://github.com/herbderby/NdsSDFormat.git",
  from: "2.0.0")`.
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

## Next Steps

(None currently documented)
