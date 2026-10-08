# Furigana generation from unannotated Japanese lyrics

Drafted October 8, 2026 against the embedded-furigana implementation on `feat-furigana-support`. The optional implementation is now present on that branch; measured results and verification limitations are recorded in [furigana-generation-verification.md](furigana-generation-verification.md).

Revised to make generation an optional feature with a separately downloaded dictionary. The base component contains the native analyzer and setup UI, but no dictionary data. Existing embedded readings continue to work without enabling generation or downloading anything.

## Recommended first release

Generate readings offline for lyrics with no `[kana:...]` metadata, using a native Japanese tokenizer and reading dictionary. Reuse `FuriganaSpan`, the shared ruby layout, and both existing rendering adapters. Display ordinary lyrics immediately, then add readings when analysis completes.

Generated readings are temporary display annotations. Keep the authoritative lyrics, local files, editor contents, and upload text unchanged. Do not set `has_kana_metadata` to make generated readings look like provider metadata.

Recommended controls:

- Rename **Show embedded furigana** to **Show furigana**, retaining its existing preference GUID and enabled default. It controls both embedded and generated display.
- Add **Enable generated furigana** as a separate, initially disabled preference. When enabled and a compatible dictionary is installed, automatically generate for eligible lyrics without kana metadata. This does not fill empty positions in an annotated file.
- When the dictionary is absent, offer **Download dictionary and enable** with the measured download/installed sizes, a short offline-use explanation, and dictionary notices. This explicit action starts setup; merely installing the component or opening preferences does not download anything. An existing compatible installation can be enabled without downloading it again.
- Add **Generate furigana for these lyrics** to the lyric context menu when generation is enabled and ready. This explicit action treats the current lyrics as Japanese, including Han-only lines, for the current track session. Reapply that choice after lyric edits within the session; clear it on track change. When setup is required, offer **Set up generated furigana...** to open preferences instead of silently downloading assets.
- Use hiragana readings and the existing half-size font, wrapping, colors, fades, and alignment. The master display setting still controls visibility.
- Explain once in preferences that generated readings are dictionary estimates and may differ from the singing.

Any existing kana tag, including an invalid or unsupported one, excludes the lyrics from this first generation path. This keeps provider readings and intentionally empty entries authoritative. Filling gaps, manual corrections, generated export, and persistent caches belong to later work.

## Engine and dictionary decision

Use **MeCab plus a pinned UTF-8 IPADIC dictionary**, behind a small analyzer interface. MeCab documents native C/C++ token APIs, input-relative token offsets, dictionary charset inspection, worker-local analysis state, and Visual C++ linking. The native component integration has been built with VS2022 for x86 and x64; its runtime needs no Python, Java, or JavaScript installation. [MeCab C/C++ documentation](https://taku910.github.io/mecab/libmecab.html)

Use the dictionary's **reading** field rather than its pronunciation field. IPADIC's configuration uses feature field 7 for reading; keep that detail inside an adapter for the exact pinned dictionary schema. Do not assume the same columns apply to UniDic or other dictionaries. [IPADIC configuration](https://raw.githubusercontent.com/taku910/mecab/master/mecab-ipadic/dicrc), [MeCab format documentation](https://taku910.github.io/mecab/format.html)

The initial dependency milestone must:

1. Pin an engine source revision, dictionary source revision, build recipe, and SHA-256 hashes. Use the existing verified dependency-fetch pattern for builds. Generation is offline; dictionary setup downloads only in response to the user's explicit setup/update action.
2. Build the runtime for both x86 and x64 with VS2022, preferably statically linked. Avoid exporting the engine's API from the component.
3. Produce a UTF-8 dictionary reproducibly and validate its charset/schema before use. Test whether the same built dictionary artifact works with both runtimes rather than assuming architecture compatibility.
4. Verify non-ASCII, space-containing, and portable profile paths. Resolve the managed dictionary through `core_api::get_profile_path()`, not a hardcoded AppData directory, the component installation folder, an installed system MeCab, or its global configuration.
5. Measure installed/archive size, initialization time, memory use, warm analysis time, and a small Japanese lyric quality corpus. Record the test machine and cold/warm distinction.
6. Include the selected MeCab license and the dictionary's own notices. MeCab offers a BSD licensing option; dictionary redistribution has separate terms that must be checked for the pinned artifact. [MeCab COPYING](https://github.com/taku910/mecab/blob/master/mecab/COPYING), [IPADIC COPYING](https://github.com/taku910/mecab/blob/master/mecab-ipadic/COPYING)
7. Publish the compatible compiled dictionary, its version/schema manifest, and notices as a separate versioned data archive. Extend the release workflow to produce this optional asset; exclude dictionary data from `.fb2k-component`. Verify download, installation, and lookup for both architectures, normal and portable foobar2000. Measure the analyzer's contribution to the base component separately from the optional dictionary archive.

If IPADIC's measured quality is inadequate, compare a pinned UniDic variant through a separate schema adapter before integrating it. Sudachi Rust is another candidate, but would add a Rust/C ABI bridge and a separately versioned dictionary format. Its upstream documentation describes that dictionary coupling; it should be a measured alternative, not an automatic fallback dependency. [Sudachi Rust documentation](https://github.com/WorksApplications/sudachi.rs)

Proceed to the component integration after this milestone records acceptable build, packaging, resource-path, and quality results. Do not promise an artifact-size or accuracy figure before measuring it.

## Optional dictionary setup and lifecycle

Add a Furigana preferences section/page with **Enable generated furigana**, dictionary status/version/installed size, and the actions appropriate to that status. Show product-level states such as **Not installed**, **Downloading**, **Installing**, **Ready**, and **Needs repair**. Explain errors in this section rather than repeatedly interrupting playback.

Setup behavior:

1. On a fresh installation, generation is disabled and no dictionary files, analyzer state, or generation-related network activity are created. Embedded furigana remains available through the existing display preference.
2. **Download dictionary and enable** starts one background download with progress and cancellation. Show its actual release size before this action. Keep base lyrics visible throughout setup; neither download nor extraction blocks the UI thread.
3. Use a project-controlled HTTPS release asset. The component carries a pinned download specification containing the artifact version, compatible format/schema, URL, SHA-256, and size limits. Verify against that trusted specification, not a digest supplied only by the downloaded archive. The asset contains dictionary data and notices; never execute installer scripts or downloaded code.
4. Stage the archive and extracted files beneath the component's managed dictionary root. Enforce download/extracted-size limits and reject archive paths that escape staging, links/reparse points, or unexpected files. Validate the dictionary's required files, charset, schema, and engine compatibility before activation.
5. Store the completed installation persistently beneath `<foobar profile>/openlyrics/furigana/dictionaries/<version>/`, with an installation manifest. Resolve the profile using the SDK so portable foobar2000 keeps the data with its profile. Use atomic activation of a validated version, leaving a working old installation intact if replacement fails.
6. Enable generation only after successful initial setup, invalidate generation/layout state, and request annotations for matching current lyrics. Cancellation or failure of initial setup leaves generation disabled, cleans only managed staging files, and offers an explicit retry. A failed update preserves the prior working installation and enablement choice. If the user disables generation during setup/update, cancel it and prevent a late completion from enabling the feature again; successful replacement must also respect the latest enablement choice.
7. On subsequent starts, use the local installed dictionary without a download. If it is missing or incompatible, show **Needs repair** and base lyrics; do not automatically fetch replacements. Updates/repair require an explicit action. A newer component may indicate an available compatible dictionary update from its bundled specification without doing a background update check.
8. Disabling generation cancels automatic and manual requests, clears generated overlays/cache, and releases the analyzer on its worker. Retain installed dictionary files for quick re-enabling. **Remove dictionary** separately disables generation, releases open mappings, and removes only files owned by this feature; it frees disk space without touching lyric files or other profile data.

Feature enablement and installation are distinct: an installed dictionary can be retained while generation is disabled. The generation service requires both an enabled preference and a verified compatible installation. Closing a preferences page need not cancel a chosen download, but cancellation, feature disablement, removal, and application shutdown must safely terminate its work. Setup callbacks must use request identities so cancelled/replaced work cannot activate obsolete assets.

The base download remains free of the large dictionary payload, although the bundled native analyzer and setup UI add some binary size. A persistent dictionary does not imply persistent annotations: generated lyric results remain memory-only.

## Generation pipeline

Add `furigana_generator.h/.cpp` for the engine-independent annotation pipeline, a MeCab adapter, a generation service for scheduling/caching, and a dictionary manager for the explicit download/install/remove lifecycle.

For each physical lyric subline:

1. Analyze the entire subline for word context. Never analyze isolated kanji or concatenate across lyric line boundaries. Equal text in repeated chorus occurrences can reuse analysis.
2. Convert the original UTF-16 text to UTF-8 while recording a byte-boundary-to-UTF-16 map. Validate token ranges and verify that each token's surface exactly matches the original substring. Retain spaces, punctuation, spelling, and all original code units.
3. Accept known dictionary tokens with a nonempty, valid kana reading and Han content. Skip unknown tokens and absent readings. In the initial release, leave numeric expressions and non-Japanese tokens unannotated. Dictionary costs are not calibrated pronunciation confidence; do not invent confidence percentages.
4. Convert ordinary katakana readings to hiragana, preserving long-vowel marks and small kana. Matching may use a normalized working copy with an offset map; the displayed lyric surface must remain untouched.
5. Align existing kana inside a mixed token with its reading. Annotate Han runs only when the kana anchors have one valid alignment. For example, `食べる` with reading `たべる` should place `た` above `食`; `取り戻す` can yield `取=と`, `戻=もど` when the tokenization and alignment support it.
6. Keep compounds such as `大人=おとな` together. When internal kana alignment is ambiguous, use a validated whole-token reading rather than guessing individual kanji readings. Unknown or invalid readings remain absent.
7. Validate every final span: nonempty reading, in bounds, no overlap, no newline crossing, and Unicode cluster boundaries. Support supplementary Han and variation selectors in generation without changing the established QQ positional-count rules.

Automatic language eligibility should be conservative: require both Han and meaningful hiragana/katakana evidence in a subline. Skip pure Han lines in automatic mode; a Chinese credit or title should not receive a Japanese reading solely because its characters are in the dictionary. The explicit context action bypasses this language filter for the current lyric session. This deliberately leaves some Japanese Han-only lines for the explicit action in the first release.

## Data ownership and asynchronous integration

Keep `LyricPanel::m_lyrics` authoritative because context-menu edits, editor launch, saving, and uploads already use it. Add a separate generated result and a stable display copy/accessor. Drawing uses the display copy; all edit/save paths continue to use the authoritative lyrics.

A generated result contains the exact input bodies, per-line spans, engine/dictionary/alignment versions, request identity, and automatic/manual trigger. Preserve a mapping from each physical subline back to its parent lyric line and UTF-16 offset when rebuilding compound display lines. No renderer or serializer should infer generated provenance from kana metadata flags.

Request analysis after `io::process_available_lyric_update` returns the final lyrics, so it sees completed auto-edits. Publish base lyrics to panels first. Route all authoritative assignments, including context-menu edits and track resets, through one update helper to invalidate old generation consistently.

Use one bounded shared worker and lazy dictionary initialization after feature enablement and installation validation. Coalesce equivalent requests across the panel and external window, deduplicate repeated lines, and prioritize the current track. Keep tokenizer state worker-owned; never run analysis or dictionary loading in paint handlers. Requests while disabled or without a ready dictionary must not initialize the analyzer or initiate downloads.

Each request captures track/session identity, lyric revision, exact body fingerprint, generation-policy revision, and active dictionary version. Completion posts to the main thread and applies only to matching active panels. Track switches, source replacements, edits, preference changes, dictionary replacement/removal, panel destruction, and component shutdown must invalidate stale completions. Use lifetime-safe callbacks; workers must not capture raw panel pointers.

Apply results through a dedicated display-annotation completion path. Do not call `announce_lyric_update` or the normal edit/save/upload pipeline for generated results. A successful completion rebuilds the stable display copy, clears the relevant ruby layout caches, and repaints both backends. It must not reset playback time, lyric timing, or manual scroll state.

Cache bounded in-memory results by exact subline text plus engine, dictionary, and alignment versions. Exclude timestamps, font, width, DPI, and colors from analysis keys. Retiming can reuse readings; font/size changes rebuild only rendering layout. Verify the exact text on cache lookup, cap input and cache memory, and allow cancellation between sublines. Disabling generation removes all generated overlays and manual session requests; embedded annotations remain governed by the independent display preference.

Missing, incompatible, or corrupt dictionary assets leave ordinary lyrics visible and report one useful status/log message. Avoid repeating initialization errors on every paint or every timer tick.

## Resource budget and annotation storage

The initial annotation cache is **memory-only**, shared across windows, with least-recently-used eviction. Start with an 8 MiB accounted-data budget and an additional entry-count limit to bound container overhead; current display results and native renderer caches are separate allocations. Account for retained text, spans, and readings, not just cache entry counts. Cache entries disappear on application exit and are regenerated on the next run. No generated annotations are written into LRC files, tags, or a sidecar database in this release.

For a normal song with around 100 annotations, expect a few to tens of KiB for the annotation data and retained text, excluding native drawing layouts. This is a representation-size estimate, not measured total process memory. The dictionary/analyzer will be much larger: reserve a planning allowance of roughly 50–100 MiB for the dictionary mapping and analysis state, pending measurement of the pinned Windows build. Do not equate mapped address space, resident RAM, and private heap usage. MeCab's Windows source uses read-only file mapping for dictionary access. [MeCab mapping implementation](https://github.com/taku910/mecab/blob/master/mecab/src/mmap.h)

Target under 100 ms for complete warm analysis of an ordinary song on the test PC; this is an acceptance target, not a benchmark result. Measure initialization and first analysis separately because cold file access can dominate them. No analysis runs on playback ticks, and simultaneous windows share work. The optional dictionary consumes profile disk space only after the user downloads it; disabling generation retains that installation, while removal frees it. Measure the base component, optional archive, installed dictionary, and temporary installation-space requirement separately in milestone 1.

Do not open the dictionary while generation is disabled or unavailable. Keep one initialized analyzer while enabled generation is active; release it on the worker when generation is disabled or the dictionary is being replaced/removed. Disabling generation also clears the annotation cache. A future optional persistent cache can store derived annotations by exact lyric content and engine/dictionary/alignment versions if cold-start measurements justify it.

## Implementation sequence and acceptance

| Milestone | Deliverable and acceptance |
| --- | --- |
| 1. Native dependency validation | Reproducible x86/x64 tokenizer and dictionary builds, separate pinned dictionary release artifact and notices, measured component/archive/installed sizes and performance, initial quality corpus, profile-path checks. |
| 2. Pure generation | Engine adapter, offset conversion, kana conversion, token-to-ruby alignment, span validation, conservative language eligibility, deterministic tests. |
| 3. Optional setup | Preferences, explicit dictionary download with progress/cancel, validation/staging/activation, persistent profile storage, retry/repair/update/removal, disabled-by-default behavior. |
| 4. Scheduling and display | Shared worker/cache, source/display separation, stale-result protection, both rendering paths, toggle/context action, edit/track/dictionary lifecycle integration. |
| 5. Release verification | Base component excludes dictionary data, separate optional asset, x86/x64 Debug and Release checks, regression suite, offscreen visuals, and live foobar2000 playback/edit/setup smoke tests. |

Acceptance coverage must include:

- Synthetic Japanese cases with compounds, okurigana, context-dependent readings, repeated kana anchors, punctuation, Latin text, numbers, unknown names, decomposed kana, surrogate pairs, and variation selectors.
- Timestamped and untimed lyrics, repeated chorus lines, equal-time compound lines, retiming, edited line replacement, rapid track switching, simultaneous panel/external window, disabling/re-enabling generation, and shutdown during initialization.
- Automatic exclusion of Chinese-only, English-only, and kana-only lines; explicit generation for Japanese Han-only lines; exclusion of any file with kana metadata, including malformed metadata.
- Byte-for-byte or semantic preservation, as appropriate, of source lyrics and local-save/editor/upload output before and after generation. Successful completion must cause no save, upload, edit metric, or retrieval event.
- Generated grouped and mixed-token readings in both renderers at narrow widths, multiple fonts/DPI values, highlight/fade, clipping, and synced/unsynced scroll positions. When readings arrive, the active base lyric baseline should remain anchored.
- Missing/corrupt assets, non-ASCII resource paths, resource limits, bounded cache eviction, warm reuse, and dictionary-version invalidation.
- Fresh-install/default-off behavior: no dictionary download, file creation, or analyzer initialization; embedded readings work without setup. Re-enabling an installed dictionary works offline.
- Explicit setup progress/cancel/retry, unavailable network, interrupted downloads, hash mismatch, unsafe archive paths, missing files, incompatible schema, insufficient disk space, and non-writable profile directories. Failed replacement preserves a working installation.
- Disable/remove/update during active download or generation, stale completions, application shutdown, normal/portable profile paths, persistence across restart, and cleanup confined to feature-owned files. Removal releases mappings before deletion and never touches saved lyrics.

Use a read-only copy of the supplied HOYO-MiX file with its kana footer removed as one quality benchmark. Compare generated coverage and reading agreement with the original 101 embedded annotations where spans can be aligned. Separate mapping correctness from reading disagreement: song-specific readings are not necessarily recoverable from ordinary dictionary analysis. Do not include the full lyric file in the repository or treat its provider annotations as a universal dictionary oracle.

The release target is responsive, correctly positioned, useful dictionary furigana. Matching the performed pronunciation for unusual names, puns, poetic readings, or intentionally reassigned kanji requires later correction support.
