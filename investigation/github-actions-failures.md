# GitHub Actions investigation

Investigated October 9, 2026 for the Schrodingers-Neko fork.

## Confirmed failure

The latest failing component run, [37809023506](https://github.com/Schrodingers-Neko/foo_openlyrics/actions/runs/37809023506), built main at `3a3d95401beda70c02fe541ddcfe093d2a707f48`. Both x86 and x64 failed before tests:

1. The version-header script used `git describe` without a fallback, while this fork had no component version tags. Git returned `fatal: No names found, cannot describe anything.`
2. The script printed a failure and used `return 1`; that returns output from a PowerShell script instead of failing the process. It did not create the generated header.
3. Compilation continued and reported C1083: missing `openlyrics_version_generated.h`.

The old March run's logs have expired (GitHub HTTP 410), so its precise cause cannot be independently established from logs now.

## Fix and validation

Commit `ac9e052` on the feature branch already corrects version generation: match only component tags (`v[0-9]*`), use `--always` for a commit fallback, and throw if Git fails. Tagless forks build as `0.0.0-g<commit>`, and the dictionary data tag cannot become a component version. Tagged component builds retain their release version.

The test workflow previously ran only for main pushes, pull requests, or reusable workflow calls. Feature-branch pushes therefore did not test the fix. The workflow now also runs on `feat-furigana-support` pushes, covering both native architectures, dictionary-backed regression tests, and Release builds.

Main and its historical red runs retain the old behavior until the feature branch's version-header fix is integrated into main. No component release tags are copied or created to mask this problem.

The optional dictionary's independent [build and publication run](https://github.com/Schrodingers-Neko/foo_openlyrics/actions/runs/37860754248) succeeded.

Fresh component [run 37887185046](https://github.com/Schrodingers-Neko/foo_openlyrics/actions/runs/37887185046), building feature commit `a7709d4e15e64c684df9aa333f4fdb83a42f9cae`, succeeded on both x86 and x64. Each job compiled the optional dictionary, passed all 121 native tests, and built Release binaries. Both Debug and Release logs show the tagless version fallback using `a7709d4`. The documentation-only result commit skips duplicate CI; the tested source and workflow are unchanged.
