"""Package the native-compiled dictionary as a deterministic, independently downloadable asset.

Build first with mecab-dict-index -d <pinned-source>/mecab-ipadic -o <output> -f EUC-JP -t UTF-8.
No lyrics, executables, or user configuration are included in this archive.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
import zlib

REVISION = "61b90ba6e669dc2d7d533d4a80d206f3b31d52b1"
VERSION = "ipadic-utf8-20070801-v1"
FILES = ("char.bin", "matrix.bin", "sys.dic", "unk.dic", "dicrc", "mecabrc", "COPYING")


def main():
    if zlib.ZLIB_RUNTIME_VERSION != "1.3.1":
        raise RuntimeError("Use the pinned Python 3.13.7 / zlib 1.3.1 packaging runtime")
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    directory = args.directory.resolve()
    source = root / "3rdparty" / f"mecab-{REVISION}" / "mecab-ipadic"
    (directory / "mecabrc").write_bytes(b"")
    (directory / "dicrc").write_bytes(
        b"cost-factor = 800\nbos-feature = BOS/EOS,*,*,*,*,*,*,*,*\nconfig-charset = UTF-8\n")
    # The English notice ends in Latin-1 division symbols, not EUC-JP text.
    notice = (source / "COPYING").read_bytes().decode("latin-1")
    (directory / "COPYING").write_bytes(notice.replace("\r\n", "\n").encode("utf-8"))
    manifest = {"version": VERSION, "engine": "mecab-0.996", "source_revision": REVISION,
                "charset": "UTF-8", "dictionary_format": 102, "reading_field": 7, "files": []}
    for name in FILES:
        data = (directory / name).read_bytes()
        manifest["files"].append({"name": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    (directory / "manifest.json").write_bytes((json.dumps(manifest, indent=2) + "\n").encode("utf-8"))
    with zipfile.ZipFile(args.output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in (*FILES, "manifest.json"):
            info = zipfile.ZipInfo(name, (2007, 8, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 0
            info.external_attr = 0x20
            archive.writestr(info, (directory / name).read_bytes(), compresslevel=9)
    payload = args.output.read_bytes()
    print(json.dumps({"archive": str(args.output), "size": len(payload),
                      "installed_size": sum((directory / name).stat().st_size for name in (*FILES, "manifest.json")),
                      "sha256": hashlib.sha256(payload).hexdigest()}, indent=2))


if __name__ == "__main__":
    main()
