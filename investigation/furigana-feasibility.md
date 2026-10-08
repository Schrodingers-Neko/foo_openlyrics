# Furigana feasibility for foo_openlyrics

Investigated October 8, 2026. Repository cloned into `F:\projects\personal\foo_openlyrics`, branch `main`, commit `3a3d95401beda70c02fe541ddcfe093d2a707f48` (September 20, 2026).

## Assessment

Both embedded furigana and automatic generation are feasible. The supplied file makes a useful first milestone: decode its QQ-style `[kana:]` data into character spans and render those spans above the lyrics. This requires no reading dictionary. Automatic generation can then fill gaps using the same annotation and layout model.

The substantial work is rendering, preserving annotations through editing/saving, and mapping positional provider data correctly. A font option or kana transliteration alone would not implement the requested behavior. Existing line timestamps and line highlighting can remain the timing model; furigana does not require word-by-word synchronization.

This is a source investigation plus an executable analysis of the supplied file. No plugin implementation, native build, playback test, or automatic-reading accuracy benchmark has been performed.

## Supplied file: embedded readings confirmed

Reference: `C:\Users\Justin\AppData\Roaming\foobar2000-v2\lyrics\HOYO-MiX - 不乱不破 (feat. Reol).lrc`.

The 2,499-byte file is valid UTF-8. It contains ordinary timed LRC lines, multiple timestamps on some lines, and one `[kana:...]` tag at the very end. There are no inline ruby tags in its lyric text.

| Measured property | Result |
| --- | ---: |
| Physical timed lines | 36 |
| Timed lines after expanding repeated timestamps | 49 |
| Han characters and `々` in physical timed lines | 147 |
| Han characters and `々` after timestamp expansion | 157 |
| Entries in the kana payload | 157 |
| Entries with a reading | 101 |
| Empty entries | 56 |
| Initial / final empty entries | 37 / 19 |

Every marker in this fixture is `1`. Treating each marker as one eligible Han/iteration-mark position, with following kana up to the next marker as the reading, gives an exact count match **after timestamp expansion and chronological sorting**. Empty entries consume positions without supplying readings. The initial and final empty entries correspond to title/credit text. Discarding credits before mapping, ignoring `々`, or mapping only physical LRC lines shifts the readings.

Representative associations recovered by the probe:

| Timestamp | Base | Supplied reading |
| --- | --- | --- |
| 00:21.72 | 覚 | かく |
| 00:21.72 | 醒 | せい |
| 00:30.48 | 覚 | さ |
| 01:07.39 | 々 | び |
| 01:25.41 | 翳 | かざ |
| 01:38.49 | 翳 | かざ |

This also demonstrates why readings must belong to individual occurrences rather than a global kanji-to-kana lookup. The observed alignment is strong evidence for this file's ordering, not a complete specification of all provider variants or a validation against the recording.

QQ Music is a plausible origin. Tencent Music's own technical disclosure describes `kana:` data where a numeric marker gives the number of base characters, including `1` and grouped `2` examples, and allows the metadata before or after the lyric body. This supports identifying the encoding family, but the file itself does not prove which service downloaded it. See [Tencent Music's lyric annotation disclosure](https://patents.google.com/patent/CN108763521B/zh).

The independent, read-only probe is [probe_qq_kana.py](F:/projects/personal/foo_openlyrics/investigation/probe_qq_kana.py); its measured output is [example-analysis.json](F:/projects/personal/foo_openlyrics/investigation/example-analysis.json). Reproduce with:

```powershell
python investigation/probe_qq_kana.py 'C:\Users\Justin\AppData\Roaming\foobar2000-v2\lyrics\HOYO-MiX - 不乱不破 (feat. Reol).lrc'
```

It deliberately accepts only the single-character marker variant present in this sample. Its character offsets are Python Unicode codepoint indices, not production Windows UTF-16 indices. It rejects count mismatches and other markers; it is not a general QQ parser.

## Where the plugin needs changes

| Area | Current behavior and implication |
| --- | --- |
| [LyricDataLine](https://github.com/jacquesh/foo_openlyrics/blob/3a3d95401beda70c02fe541ddcfe093d2a707f48/src/lyric_data.h#L33) | Holds only text and a timestamp. Add annotations associated with ranges of base text. |
| [LRC parser](https://github.com/jacquesh/foo_openlyrics/blob/3a3d95401beda70c02fe541ddcfe093d2a707f48/src/parsers/lrc.cpp#L322) | Expands multiple timestamps, stable-sorts lines, then combines equal-time lines with newlines. Attach this fixture's readings after expansion/sorting and before combination, or preserve subline boundaries and equivalent ordering explicitly. |
| [Metadata classification](https://github.com/jacquesh/foo_openlyrics/blob/3a3d95401beda70c02fe541ddcfe093d2a707f48/src/parsers/lrc.cpp#L31) | `kana` is absent from the recognized tag names. Metadata is also only extracted while `!tag_section_passed` (line 375). A trailing kana tag therefore becomes an untimed lyric line. Adding `kana` to the whitelist alone would not fix this file. |
| [Embedded panel](https://github.com/jacquesh/foo_openlyrics/blob/3a3d95401beda70c02fe541ddcfe093d2a707f48/src/ui_lyrics_panel.cpp#L422) | GDI measurement and `TextOut`, one selected font, whitespace-based wrapping. Needs ruby measurement, positioning, clipping, and wrapping. Japanese text without spaces also needs useful break opportunities. |
| [External window](https://github.com/jacquesh/foo_openlyrics/blob/3a3d95401beda70c02fe541ddcfe093d2a707f48/src/ui_lyrics_externalwindow.cpp#L415) | DirectWrite text layout and Direct2D rendering. Height currently equals `lineCount * line_height`; ruby needs actual row heights and baselines. |
| [Save/expand](https://github.com/jacquesh/foo_openlyrics/blob/3a3d95401beda70c02fe541ddcfe093d2a707f48/src/parsers/lrc.cpp#L415) | Splits compound lines and optionally merges equal text into multi-timestamp lines. Positional kana data must still describe the resulting expanded order, and merge equality must include annotations. |
| [Editor](https://github.com/jacquesh/foo_openlyrics/blob/3a3d95401beda70c02fe541ddcfe093d2a707f48/src/ui_lyric_editor.cpp#L618) | Re-parses plain editor contents. A stale global kana payload can silently attach to different text after an edit. Preserve/rebuild annotation associations or invalidate affected readings explicitly. |
| [QQ source](https://github.com/jacquesh/foo_openlyrics/blob/3a3d95401beda70c02fe541ddcfe093d2a707f48/src/sources/qqmusic.cpp#L143) | Base64-decodes the response's `lyric` string into raw bytes. If that response contains a kana tag, the source already transports it. No provider change is needed for the supplied local file. Current API availability of embedded kana was not checked. |
| [Update and saving](https://github.com/jacquesh/foo_openlyrics/blob/3a3d95401beda70c02fe541ddcfe093d2a707f48/src/lyric_io.cpp#L703) | Applies edits and saves before distributing a lyric update. Generation should operate on the final text asynchronously, with an independent completion path that does not trigger another save. |

The HTML source helper also recursively concatenates text nodes without ruby-specific handling. An HTML provider containing `<ruby>` would need an importer that distinguishes base text from `<rt>` and ignores `<rp>` fallback punctuation; simply flattening the tree loses the association. This is separate from the supplied `[kana:]` format.

## Recommended internal model and flow

Add a list of annotations to each lyric line. Each annotation needs a UTF-16 start and length, reading text, and provenance such as embedded, manual, or generated. Preserve empty provider entries during decoding/serialization even if they produce no visible ruby. A provider's intentionally empty entry should not automatically be treated as a missing reading unless the user selects a fill-gaps policy.

For QQ input, extract supported `[kana:]` tags anywhere in the file before normal lyric parsing. Expand timestamps and stable-sort; decode the payload against that ordered text; attach spans; then combine concurrent lines, shifting spans by the left text length plus the inserted newline. Keep the raw payload and validity state for unsupported data. A supported metadata tag should never be displayed as lyric text.

For saving, serialize embedded/manual spans into a payload matching the output's expanded reading order. Preserve raw data only while it still refers to the same text/order. Text edits, line deletions, concurrent-line splits, and timestamp reordering require annotation-aware updates. Timestamp-only edits can move already attached spans with their lines before regenerating the positional payload. A text edit with ambiguous reassociation should invalidate the affected annotations rather than guess.

Generated readings should be derived display data by default. They should not replace the original lyric text or get saved/uploaded automatically. An explicit export or accept-readings operation can turn selected readings into persistent annotations. Existing LRCLIB uploads use the same expansion function as local saving, so provide an export policy that produces base lyrics without private provider metadata.

For manual corrections, a span editor or explicit inline notation is easier than editing a long positional stream. A later option is an Aozora-style explicit span such as `｜漢字《かんじ》`, converted into the same internal model. Avoid guessing from ordinary parentheses or `《...》` alone: the supplied title itself uses these brackets for a work name. See [Aozora's ruby annotation rules](https://www.aozora.gr.jp/annotation/etc.html). Full Aozora document support is unnecessary.

## Rendering approach

Use one shared semantic representation and consistent ruby layout rules, with adapters for the existing GDI and DirectWrite surfaces. Keeping both backends is a smaller initial scope than replacing all embedded-panel rendering. A future consolidation could reuse DirectWrite with GDI interoperability, but is not a prerequisite.

For a first layout, treat each annotated span as an indivisible unit. Measure base and reading separately, use the larger width, and center each within that width. Reserve a reading band above the base baseline, initially around half the base font size. Include that band in row height, scrolling distance, top alignment, clipping, and manual scroll limits. Keep an entire base/reading unit together across line breaks; define a graceful fallback for a unit wider than the panel. Long readings should not overlap neighboring annotations.

In DirectWrite, an `IDWriteInlineObject` is a possible representation for a base-plus-reading unit: it exposes size/baseline, overhang, and break conditions, with application-defined drawing. This is an implementation option, not an existing automatic ruby feature in the plugin. Use actual line metrics instead of counting rows times one font height. See Microsoft's [inline object interface](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nn-dwrite-idwriteinlineobject), [inline metrics](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ns-dwrite-dwrite_inline_object_metrics), and [line metrics](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/ns-dwrite-dwrite_line_metrics).

Cache annotation analysis and layout. The embedded panel has a 16 ms paint timer; neither dictionary analysis nor layout construction should run for every paint. Rebuild on lyric edits, width/font/DPI changes, or furigana setting changes. During playback, reuse geometry and update only position and highlight color. Both base and reading should follow the current line's highlight/fade.

## Automatic generation

Generation needs Japanese word segmentation and dictionary readings. A dictionary lookup per isolated kanji is insufficient, as this sample's different readings for `覚` show.

| Candidate | Fit and remaining investigation |
| --- | --- |
| MeCab with a pinned UTF-8 reading dictionary | Best initial native C++ integration candidate. Official documentation provides C/C++ APIs, input positions, dictionary features, reading output, and MSVC linking. Verify x86/x64 builds and the selected dictionary's reading fields, notices, size, and accuracy. Do not hard-code one dictionary's feature schema for all dictionaries. |
| Sudachi Rust | Provides readings and multiple tokenization modes. A Rust wrapper/FFI or helper adds build and packaging work to this C++ project. Pin engine and dictionary versions together; upstream currently cautions that 0.7 releases can change behavior even between patches. |
| External preprocessing helper | Useful for quickly comparing analyzers and generating annotated files. Requires a helper deployment and is a weaker final installation experience if it depends on a separately installed Python/Java runtime. |

Primary references: [MeCab API and Windows linking](https://taku910.github.io/mecab/libmecab.html), [MeCab reading output](https://taku910.github.io/mecab/format.html), [Sudachi Rust](https://github.com/WorksApplications/sudachi.rs), and [Sudachi dictionary notices](https://github.com/WorksApplications/SudachiDict/blob/develop/LEGAL).

Run analysis once per lyric revision on a worker. Display base lyrics immediately; apply completed readings only if the track and text revision still match. Cache by text plus engine/dictionary version. Keep analyzer objects worker-local or follow the chosen engine's concurrency contract. Convert UTF-8 tokenizer offsets to validated UTF-16 spans, retaining spaces and punctuation. Do not normalize or rewrite the lyric surface to match dictionary entries.

Convert ordinary katakana dictionary readings to hiragana when desired. For mixed words, align kana/okurigana to annotate the kanji portion; if alignment is ambiguous, retain a word-level reading instead of inventing character readings. Unknown words should remain unannotated. A conservative precedence is manual correction, then embedded reading, then generated fallback. Lyrics can deliberately use nonstandard readings, so automatic output needs correction support and should not promise the performed pronunciation. Do not infer Japanese solely from the presence of Han characters: this example also includes Chinese credits.

## Suggested milestones and verification

1. **Embedded QQ support:** metadata extraction at either end, this fixture's mapping, span model, ruby rendering in both surfaces, display toggle/size, and safe save/editor round trips. Cover grouped and timed provider variants only after obtaining fixtures; the sample validates marker `1` only.
2. **Automatic readings:** optional offline engine and dictionary, asynchronous generation/cache, gap policy, manual overrides, and optional persistence/export.
3. **Compatibility:** annotation-aware auto-edits, exports/uploads, additional provider variants, Unicode/font/DPI coverage, and more annotated songs.

A rough planning allowance is several engineer-weeks: about 2–3 for robust embedded support across both renderers and editing, 1–2 for generation, and 1–2 for packaging and compatibility checks. These are code-review estimates, not measured delivery times. A rendering-only demonstration would be substantially smaller.

Meaningful checks for implementation:

- Use the supplied file as a local fixture; verify all 157 positions, 101 nonempty readings, repeated timestamps, `々`, credits, and different readings of the same character. Add synthetic equivalents to repository tests without checking in the full song.
- Verify kana tags at the top and bottom, malformed/truncated payloads, unsupported markers, supplementary Han, and span boundaries. Count mismatches must fail safely rather than shift the remaining readings.
- Verify semantic parse/save/parse equivalence with timestamp merging on and off, equal-time compound lines, changed timestamps, lyric edits, and removed lines.
- Check Japanese text without spaces, long readings, tiny panels, font fallback, high DPI, alignment modes, clipping, manual scroll, and line highlighting in both windows.
- Verify that generation leaves persisted lyrics unchanged by default, respects explicit readings, skips ambiguous/unknown words, and discards stale worker results after a track/edit change.
- Build/test x86 and x64 using the existing MSVC/MVTF CI workflow. Compare a small lyric corpus against manually checked readings before choosing a dictionary.

Local toolchain inspection found VS 2022/MSVC and Windows SDK 10.0.19041.0. The CMake on PATH is 3.30.4; this repository requires at least 3.31.6 and fetches missing curl/nghttp2 source archives during the build. No toolchain installation or dependency download was performed for this investigation. Native compilation and runtime behavior remain unverified.
