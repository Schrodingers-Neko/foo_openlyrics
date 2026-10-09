# Optional furigana implementation verification

October 8, 2026; `feat-furigana-support`. The accepted design is [furigana-generation-plan.md](furigana-generation-plan.md).

## Implemented behavior

- Embedded `[kana:...]` readings are independent of generated furigana and require no dictionary.
- Generation defaults to disabled. Installing the component and opening preferences create no dictionary files, initialize no analyzer, and start no download.
- **Download dictionary and enable** is the explicit setup action. Installation validates a pinned HTTPS archive and individual file hashes, stages beneath the managed profile root, checks native compatibility, and activates a verified version. Cancellation and failed replacements preserve an existing installation.
- The downloaded dictionary persists beneath `<foobar profile>/openlyrics/furigana/dictionaries/ipadic-utf8-20070801-v1/`. Disabling retains it; removing releases mappings and deletes only recognized feature-owned files. SDK profile resolution supports normal and portable installations.
- Annotations are memory-only display overlays. Source text, timestamps, metadata, lyric files, editor text, and uploads remain authoritative. Shared analysis uses an 8 MiB accounted LRU cache and one lazy worker. Ineligible lyrics do not open the dictionary.
- Automatic generation requires Han and kana in the same physical subline. A context action permits Japanese Han-only lines for the track session. Any existing kana metadata excludes the entire lyric file from generation.
- Both GDI panel and DirectWrite external-window paths draw generated spans. **Enable furigana** on the Furigana page and lyric context menu controls both visibility and background generation. Master-off retains the installed dictionary and saved generation choice. Kana metadata stays hidden in either state. Top alignment reserves a reading band while generation is pending; existing scroll and highlight state are retained.

## Pinned artifacts and build

MeCab 0.996 / IPADIC 2.7.0-20070801 source revision: `61b90ba6e669dc2d7d533d4a80d206f3b31d52b1`. Source archive SHA-256: `7ad44f987ae0b7fd345c72a4b67e14dbe0f4bd1669d247a8aaeb0f15218a3fd1`.

Dictionary build and packaging recipe: `build/build_furigana_dictionary.ps1`, VS2022, Python 3.13.7 / zlib 1.3.1. x86 Debug and x64 Release builds reproduced the same payload. Both native runtimes loaded the same compiled dictionary.

| Resource | Measured size |
| --- | ---: |
| Optional download ZIP | 13,399,728 bytes (12.8 MiB) |
| Installed dictionary, config, manifest, notices | 52,936,022 bytes (50.5 MiB) |
| x86 component DLL, initial generation preview Release | 2,590,208 bytes |
| x64 component DLL, initial generation preview Release | 3,004,416 bytes |

The base `.fb2k-component` contains the two DLLs and MeCab/miniz notices, with no dictionary payload. Initial installation needs approximately one installed dictionary's free disk space plus a 1 MiB allowance, in addition to any retained old dictionary. The bounded archive is temporarily held in memory during setup.

Optional asset: [versioned dictionary data release](https://github.com/Schrodingers-Neko/foo_openlyrics/releases/tag/furigana-dictionary-ipadic-20070801-v1). [GitHub's independent build](https://github.com/Schrodingers-Neko/foo_openlyrics/actions/runs/37860754248) reproduced the pinned archive and published it successfully. Component release tags and version-header generation exclude dictionary data tags; forks without component tags use a commit-based development version.

Archive SHA-256: `3df6f40dfb4d7ddb558299ec4f39dc79ca32cd5ace076eb7bf61fe86f3d49412`.

## Local automated verification

- x86 and x64 Debug regression suites each pass all 118 tests, with the optional native dictionary and read-only supplied lyric fixture enabled. Release component builds pass on both architectures. Both suites also passed with the explicitly requested real HTTPS download test enabled against the published release asset, including digest and archive validation.
- Generator coverage includes grouped compounds, okurigana, ambiguous kana anchors, invalid/unknown readings, numerical/Latin exclusions, Unicode offsets, surrogate pairs, variation selectors, metadata exclusion, repeated sublines, cache reuse and eviction, and eligibility before dictionary loading.
- Dictionary tests exercise initial installation, cancellation during extraction, cancelled replacement preserving the old installation, exact archive hash validation, installed-file corruption/repair, Unicode and space-containing profile paths, unexpected files protecting removal, and unsafe archive names/link attributes/size declarations.
- Background-service tests verify disabled/default-off behavior, equivalent-request coalescing, obsolete requests, cancellation/disablement, source preservation, and shutdown rejecting stale callbacks.
- Actual GDI and DirectWrite offscreen output covers embedded and generated readings, three horizontal alignments, narrow widths, fallback fonts, colors, 96/144 DPI, and clipping. Preferences resource tests check control bounds and render absent/ready/downloading/repair states. Local images are exported under `build/Debug/furigana-visuals/`.

On October 9, the master-toggle changes passed all 121 tests on x86 and x64 with the optional dictionary and read-only song fixture. Additional checks cover stored-choice preservation, master-off setup/request gating, stale completions, offline re-enabling, hidden whitespace/BOM/case/timestamp-prefixed metadata, and progress visibility/reset after completion or removal. The preferences resource is also rendered with the master disabled.

The initial preview was installed after backing up the original component and settings. The user confirmed embedded and generated readings in live playback with screenshots. Native checks do not establish live interaction behavior for the new master toggle; those checks remain below.

## Supplied-song benchmark

Test machine: AMD Ryzen 7 7800X3D, Windows 11 Pro 10.0.26200, approximately 63.1 GiB visible RAM; MSVC 14.44.35207. Timings below are Debug x64 with warm OS file caches, excluding installation and full on-disk hash verification. A cold-disk benchmark has not been performed.

The supplied HOYO-MiX LRC was read without changes. A parsed in-memory copy omitted its kana metadata for generation; the complete copyrighted lyric text is not checked into the repository.

- Original fixture: 49 timed occurrences, 157 QQ positions, 101 embedded readings.
- Generated output: 77 spans. Of 67 comparable span/readings, 60 agreed with the embedded readings. These counts describe this one song, not a general accuracy percentage.
- Complete warm generation: approximately 3 ms; cache reuse: approximately 0.35 ms.
- Accounted annotation cache for the song: 10,288 bytes, excluding current display copies and native renderer layouts.
- Observed working-set delta in one warm Debug run: approximately +2.9 MiB; private memory delta: approximately +0.35 MiB. This is not the full dictionary mapping's address space or a worst-case resident-memory bound. The dictionary data uses read-only native mappings and process memory changes with page access.

## Live release checks still to perform

Before treating the preview as a production release, use an isolated or explicitly approved foobar2000 profile to exercise:

1. Fresh/default-off embedded display, opening preferences without download, explicit setup/progress/cancel/retry, and offline restart/re-enabling.
2. Timed and untimed playback, editor/save/upload source preservation, rapid track/source changes, simultaneous panel/external window, display/generation toggles, and removal during active work.
3. Active-line positioning when annotations arrive, including a wrapping change during scrolling; normal and portable profile locations.
4. Unavailable network, profile permissions/free-space errors, interrupted process recovery, and close during setup.

Native desktop interaction is unavailable in this session. The user has authorized backing up and updating the installed preview; process/module checks verify which DLL is loaded, while interactive playback checks require the user's player UI.
