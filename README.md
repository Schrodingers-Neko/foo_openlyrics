# foo_openlyrics

![build-and-test workflow](https://github.com/jacquesh/foo_openlyrics/actions/workflows/run_tests.yml/badge.svg)

An open-source lyrics plugin for [foobar2000](https://www.foobar2000.org/) that includes its own UI panel for displaying and sources for downloading lyrics that are not available locally. It is intended to be a replacement for LyricShowPanel3 so it is fully-featured and supports lyric searching, saving and editing directly from within foobar2000.

## Features
* Buttery-smooth lyric scrolling
* Supports retrieving lyrics from local files, ID3 tags or the internet
* Customise the font & colours to perfectly suite your layout & theme
* Supports using album art or any other image as the panel background, with optional transparency and blur
* Easily edit lyrics directly inside foobar2000 with built-in support for timestamps
* Check the saved lyrics of any track in your library (regardless of whether it is currently playing)
* Apply common edits (such as removing blank lines) in just 2 clicks
* ...and more!

## Embedded furigana

Japanese readings supplied in count-based `[kana:...]` lyric metadata appear above the text in both the panel and external window. Disable **Show furigana** in the display preferences to hide them. Readings use half the lyric font size and follow the current line's highlight color.

Kana metadata may occur before or after the lyric body. Single-character and grouped readings are supported, including empty placeholders. Unsupported or misaligned metadata is hidden from the display rather than attached to the wrong text. Existing readings are preserved through supported edits and local saves; editing text with ambiguous associations clears the affected readings. Timing-bearing kana entries are not supported.

## Optional generated furigana

For Japanese lyrics without kana metadata, open **Preferences → OpenLyrics → Furigana** and choose **Download dictionary and enable**. Generation is disabled by default; the component download contains no dictionary. The optional reading dictionary is a 12.8 MiB download and uses 50.5 MiB in the foobar2000 profile. After setup, generation works offline, including in portable installations.

Generation adds dictionary readings to a temporary display copy. It does not change lyric files, tags, editor text, or uploads. Automatic detection skips Han-only lines; use **Generate furigana for these lyrics** in the lyric menu to treat the current lyrics as Japanese. Existing kana metadata takes precedence, including intentionally empty entries. Dictionary estimates may differ from the singing, especially names and poetic readings.

Turning generation off clears its memory cache and releases the analyzer. The downloaded dictionary remains for re-enabling; **Remove dictionary** frees its disk space. Download/repair is always an explicit action. Generated annotations use a bounded in-memory cache and are regenerated after restart.

## Screenshots
Fonts & colours are fully configurable
![](.github/readme/lyrics_vertical_scroll.gif)

The editor window
![](.github/readme/editor.jpg)

## How to install foo_openlyrics
1. Find the latest [release on Github](https://github.com/jacquesh/foo_openlyrics/releases).
2. Download the `fb2k-component` file attached to the release (don't worry about the `debug_symbols` zip file).
3. Double-click on the file you just downloaded. Assuming foobar2000 is installed, it should open up with the installation dialog. Restart foobar2000 when asked.
4. Add the "OpenLyrics Panel" to your layout.

## Why another lyrics plugin?
At the time that I started this, the most widely-used lyrics plugin was [foo_uie_lyrics3](https://www.foobar2000.org/components/view/foo_uie_lyrics3) which had several built-in sources but those had largely stopped working due to the relevant websites going down or otherwise becoming generally unavailable. The original developer seemed to be nowhere in sight though and the source for the plugin did not appear to be available anywhere online. There is an SDK for building one's own sources for foo_uie_lyrics3 but building plugins for plugins didn't really take my fancy. Other (more up-to-date) plugins did exist but were mostly distributed by people posting binaries for you to download from their Dropbox on Reddit. Running binaries published via Dropbox by random people on Reddit did not seem like the most amazing idea.

## Contributing
Please do log an issue or send a pull request if you have found a bug, would like a feature added. If you'd like to support the project you can also make a small donation using any of these links:

[![](.github/readme/bmc-button.png)](https://www.buymeacoffee.com/jacquesheunis)
[![Donate](https://liberapay.com/assets/widgets/donate.svg)](https://liberapay.com/jacquesheunis/donate)
