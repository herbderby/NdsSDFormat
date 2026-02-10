# Next Up

## Completed This Session

- Fixed `minimumByteCount` from 500 MB to `(1 << 31) + (1 << 23)`
  (2 GiB + 8 MB = 2,155,872,256 bytes) — commit 2cc1a49
- Empirically tested 2 GB SD card:
  - macOS Disk Utility formats it as FAT16 with 32KB clusters
    (61,399 clusters, below FAT32's 65,525 threshold)
  - 16KB clusters produce valid FAT32 (122,563 clusters) and
    DS flashcart boots, but warns about slow game loading
  - Confirmed: 32KB clusters on 2 GB = FAT16, not viable
- Formatted 4 GB card (~3.64 GB actual) successfully with the
  new minimum — works on DS without warnings

## In Progress

- Nothing partially done

## Next Steps

1. Tag **3.1.0** (CLI executable + minimumByteCount fix)
2. Push to GitHub
3. Decide fate of SD_Card_Formatter repo:
   - Option A: Archive/deprecate (SDFormat CLI replaces it)
   - Option B: Commit the 3.0.0 dependency update, then archive
4. Update ProjectButler registry if repos are archived

## Key Files

- `Sources/NDSSDFormat/SectorWriter.swift:46` — minimumByteCount
- `Sources/SDFormat/SDFormat.swift` — CLI entry point
- `Sources/SDFormat/SDCardManager.swift` — device ops

## Context

- Marketed "4 GB" cards can report as low as ~3.6 GB. The
  first attempt at 4,000,000,000 rejected a real 4 GB card.
- The minimum formula: 2^31 = data region (2^16 clusters ×
  2^15 bytes/cluster), 2^23 = overhead (4 MB alignment +
  reserved sectors + two FAT copies).
- Never `sudo swift run` — it poisons `.build` with root-owned
  files. Use `swift build` then `sudo .build/debug/SDFormat`.
