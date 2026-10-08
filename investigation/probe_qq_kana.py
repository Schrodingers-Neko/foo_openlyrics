"""Read-only probe for the single-character [kana:] variant in the supplied LRC.

This is investigation tooling, not a production parser. It deliberately rejects
markers other than 1; grouped readings and timed kana need additional fixtures.
"""

import argparse
import json
import re
from decimal import Decimal
from pathlib import Path


TIMESTAMP = re.compile(r"\[(\d{2}):(\d{2}\.\d+)\]")
PREFIX = re.compile(r"^(?:\[\d{2}:\d{2}\.\d+\])+")
KANA_TAG = re.compile(r"^\[kana:(.*)\]$", re.IGNORECASE)


def sample_han(character):
    """BMP Han plus the iteration mark, sufficient for this specific fixture."""
    point = ord(character)
    return 0x3400 <= point <= 0x4DBF or 0x4E00 <= point <= 0x9FFF or point == 0x3005


def analyze(path, all_spans=False):
    text = path.read_text(encoding="utf-8-sig")
    payloads = [match.group(1) for line in text.splitlines()
                if (match := KANA_TAG.fullmatch(line))]
    if len(payloads) != 1:
        raise ValueError("This probe requires exactly one complete [kana:] tag")
    payload = payloads[0]
    tokens = list(re.finditer(r"1([^0-9]*)", payload))
    if not tokens or "".join(token.group(0) for token in tokens) != payload:
        raise ValueError("Unsupported kana encoding: this probe accepts only marker 1")

    physical = []
    expanded = []
    for line in text.splitlines():
        prefix = PREFIX.match(line)
        if prefix is None:
            continue
        body = line[prefix.end():]
        physical.append(body)
        for timestamp in TIMESTAMP.finditer(prefix.group(0)):
            seconds = float(int(timestamp.group(1)) * 60 + Decimal(timestamp.group(2)))
            expanded.append((seconds, body))
    expanded.sort(key=lambda line: line[0])  # Python sorting is stable.

    characters = [(seconds, offset, character)
                  for seconds, body in expanded
                  for offset, character in enumerate(body)
                  if sample_han(character)]
    if len(characters) != len(tokens):
        raise ValueError(f"Alignment mismatch: {len(characters)} Han/々 slots "
                         f"versus {len(tokens)} kana entries")
    spans = [{"time_seconds": seconds, "codepoint_offset": offset,
              "base": character, "reading": token.group(1)}
             for (seconds, offset, character), token in zip(characters, tokens)
             if token.group(1)]
    nonempty_indices = [i for i, token in enumerate(tokens) if token.group(1)]
    representative_times = (21.72, 30.48, 67.39, 85.41, 98.49)
    selected = spans if all_spans else [span for span in spans
                                       if span["time_seconds"] in representative_times]
    return {
        "encoding": "UTF-8 (strict read)",
        "physical_timed_lines": len(physical),
        "expanded_timed_lines": len(expanded),
        "physical_han_slots": sum(sample_han(ch) for body in physical for ch in body),
        "expanded_han_slots": len(characters),
        "kana_entries": len(tokens),
        "nonempty_readings": len(spans),
        "empty_entries": len(tokens) - len(spans),
        "leading_empty_entries": nonempty_indices[0] if nonempty_indices else len(tokens),
        "trailing_empty_entries": len(tokens) - nonempty_indices[-1] - 1 if nonempty_indices else 0,
        "count_alignment": "exact",
        "ordering_hypothesis": "expanded timestamps, then stable chronological order",
        "spans": selected,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    parser.add_argument("--all-spans", action="store_true")
    arguments = parser.parse_args()
    print(json.dumps(analyze(arguments.path, arguments.all_spans), ensure_ascii=True, indent=2))
