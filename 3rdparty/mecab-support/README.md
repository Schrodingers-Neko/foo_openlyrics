# Static MeCab adapter

Source: `taku910/mecab` revision `61b90ba6e669dc2d7d533d4a80d206f3b31d52b1` (MeCab 0.996 and IPADIC 2.7.0-20070801).
The verified source archive is fetched by `fetch-3rdparty-libs.ps1`. Redistribution of the engine uses its BSD option; see LICENSE.

CMake copies the source into its intermediate directory and applies two narrow Windows compatibility changes: suppress DLL imports/exports for static linkage and define the upstream Unicode filename helper for MSVC as well as MinGW. It also supplies the modern standard-library include and unsigned 64-bit formatter configuration needed by this revision. Upstream source files are not edited in place. Configuration and dictionary paths are passed explicitly; the component never depends on a system MeCab installation.

`build/build_furigana_dictionary.ps1` compiles the dictionary to UTF-8 with the same pinned native compiler and packages a deterministic ZIP with the original dictionary notices and a file-digest manifest. The separately downloadable asset is data only. Changes to its payload require a new asset version and updated trusted hashes in `src/furigana_dictionary.h`; never overwrite an existing version's release asset.
