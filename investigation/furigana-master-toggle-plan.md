# Master furigana toggle and hidden kana metadata

Drafted and implemented October 9, 2026 for `feat-furigana-support`. The implementation also fixes the setup progress bar remaining visible after completion/removal. Native validation covers retained generation choices, master-off setup/request gating, stale completions, offline re-enabling, metadata variants, and dialog controls/progress states. Live interaction checks remain part of the acceptance list below.

## Current behavior

Before this change, the Display page contained **Show furigana**, backed by preference GUID `8dcae30b-51c2-45e8-a911-396124624ec7`. Both renderers used it to hide embedded and generated readings, but it did not stop the generator. **Enable generated furigana** on the Furigana page was a separate preference.

The LRC parser already separated recognized `[kana:...]` records from lyric lines before decoding their readings, without consulting either preference. This change extends recognition to surrounding whitespace, BOMs, case variants, and timestamp prefixes. Timestamp-prefixed forms remain unsupported for readings and are retained as hidden raw metadata.

## Controls and defaults

- Put **Enable furigana** at the top of the Furigana preferences page. It is the master switch for embedded display, generated display, and background generation.
- Reuse the existing Show furigana preference GUID and its enabled default. A previously unchecked Show furigana setting becomes master-off; the stored generation choice is retained.
- Move the existing checkbox out of Display, avoiding two independently saved controls for the same setting. Update the explanatory text and README to point to the Furigana page.
- Keep **Generate readings when kana metadata is absent** as a subordinate, initially disabled choice. Preserve its existing GUID and saved value. Grey this control while the master is off without overwriting its value.
- Add a checked **Enable furigana** item to the lyric context menu for quick access. It changes the same persisted master setting and updates every panel and external window immediately.
- The master remains usable without a dictionary. **Download dictionary and enable** remains explicit, with the existing size/progress/cancel UI. This action requires the master to be on; clicking it must commit the relevant pending preferences before starting setup. **Remove dictionary** and **Dictionary notices...** remain available while the master is off.

| Master | Generation choice | Visible readings | Background analysis |
| --- | --- | --- | --- |
| Off | Either | None | None |
| On | Off | Embedded metadata only | None |
| On | On, dictionary ready | Embedded readings, or generated readings for eligible unannotated lyrics | Allowed |
| On | On, dictionary unavailable | Embedded readings only | None; show setup/repair status |

Any kana metadata continues to exclude the whole file from generation, including malformed/unsupported metadata and intentionally empty reading entries.

## Metadata is always metadata

The master changes annotation behavior, never whether a kana record becomes a visible lyric. Keep metadata classification independent of both the master and generation settings.

1. Recognize standalone kana records at the beginning or end of the lyric body, including case variants, BOMs, surrounding whitespace, and records preceded only by LRC timestamps. Classify the entire record before adding timestamped or untimed lyric lines.
2. Preserve the original metadata in the existing metadata fields. Use a trimmed working view for recognition while retaining the raw record; canonicalize only through the existing serialization policy. Timestamp-bearing forms may remain unsupported for decoding, but must not become visible lyrics or enable generated fallback.
3. Hide malformed/unsupported reserved kana records too. Ordinary lyric text containing an inline literal `[kana:...]` after other lyric text stays ordinary text; do not run a broad substring-removal filter over the lyrics.
4. Preserve the existing editor/local-save/upload behavior. Turning the master off must not rewrite a file, delete provider metadata, or cause an edit/save/upload event. Existing protection against stale metadata after text edits still applies.
5. Keep embedded parsing available while the master is off so enabling it can immediately display the current source without another search or download. Ruby drawing/layout work remains gated by the master.

## Runtime and lifecycle changes

Use one central persisted master accessor and one change handler. Retain `preferences::display::show_furigana()` as a compatibility wrapper if useful during the change. The generation preference expresses the user's choice; the service's runtime enablement is computed from **master AND generation choice**, with dictionary readiness required for actual requests.

Refactor the current host service callback that copies runtime `status.enabled` into the saved generation preference. An effective disable caused by the master must not overwrite the saved generation choice. Separate runtime status notifications from explicit preference changes and successful user-requested setup; otherwise turning the master off would silently lose the user's generation setting.

Applying master-off or using the context toggle must:

- Invalidate panel tickets and service request identities, reject late callbacks, remove generated display copies, and clear manual Japanese overrides.
- Cancel active dictionary setup, clear queued analysis and the shared annotation cache, and release the analyzer on its worker. Keep the installed dictionary and the persisted generation choice.
- Clear both renderers' ruby caches and pending reading-band reservation, then repaint ordinary lyrics. Preserve source text, lyric timing, playback position, and manual-scroll state.

Master-on restores embedded display immediately. If generation is selected and the installed dictionary is ready, request analysis for current eligible lyrics. It must not initiate a download. Setup completion must respect the latest master/request identity and never turn the master back on after a disable action.

Apply the gate consistently at startup, request submission, panel completion, manual generation, generation preference changes, download completion, and dictionary removal/repair. Preference Apply/reset semantics and asynchronous status refresh must preserve unsaved checkbox edits and the distinction between the saved generation choice and effective runtime state.

## Implementation and verification

1. Add the master configuration/change API; relocate the checkbox and add the shared context-menu action. Update the Furigana dialog resource and bounds/state tests.
2. Connect the master gate to the service and panel lifecycle, including setup cancellation and re-enabling an installed dictionary offline.
3. Extend parser classification for the identified metadata variants, retaining original records and source/serialization protections.
4. Add meaningful regression coverage for every row in the table: both rendering backends, master-off before startup, disabling during setup/analysis, stale completion, re-enabling, simultaneous windows, and stored-choice preservation.
5. Test kana headers/footers in timestamped and untimed lyrics, malformed records, whitespace/BOM/case/timestamp-prefix variants, and inline literal text. Assert no visible kana record, no footer-induced extra scroll height, no new generation eligibility, and preserved local-save/editor/upload semantics with the master off.
6. Build and test x86/x64 Debug and Release. Verify in the live player that a metadata-bearing file ends at its final lyric in both toggle states, all readings disappear when off, and current embedded/generated readings return correctly when on.

Acceptance: one discoverable master switch disables annotations and background generation; recognized kana metadata never trails the visible lyrics; disabling retains the dictionary and the user's generation choice without altering lyric files.
