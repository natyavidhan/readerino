"""Word-wrap and ASCII sanitization shared by every input format, so the
device (with its plain 6x8 ASCII font) never has to deal with characters it
can't render.

The wrap behavior mirrors the firmware's original on-device word-wrapper
(Reader::buildIndex): each source line is wrapped independently, long words
are hard-split, and blank source lines are preserved as empty output lines
(paragraph spacing)."""

import textwrap
import unicodedata

_REPLACEMENTS = {
    "‘": "'", "’": "'",
    "“": '"', "”": '"',
    "–": "-", "—": "--",
    "…": "...",
}


def to_ascii(s: str) -> str:
    for k, v in _REPLACEMENTS.items():
        s = s.replace(k, v)
    s = unicodedata.normalize("NFKD", s)
    return s.encode("ascii", "ignore").decode("ascii")


def wrap_text(text: str, width: int) -> list[str]:
    text = to_ascii(text).replace("\r\n", "\n").replace("\r", "\n")
    out: list[str] = []
    for raw_line in text.split("\n"):
        raw_line = raw_line.strip()
        if not raw_line:
            out.append("")
            continue
        wrapped = textwrap.wrap(raw_line, width=width, break_long_words=True, break_on_hyphens=False)
        out.extend(wrapped if wrapped else [""])

    # collapse runs of 3+ blank lines down to 1 (extraction artifacts,
    # especially from PDFs, tend to leave long stretches of blank lines)
    collapsed: list[str] = []
    blank_run = 0
    for line in out:
        if line == "":
            blank_run += 1
            if blank_run <= 1:
                collapsed.append(line)
        else:
            blank_run = 0
            collapsed.append(line)

    # trim leading/trailing blank lines
    while collapsed and collapsed[0] == "":
        collapsed.pop(0)
    while collapsed and collapsed[-1] == "":
        collapsed.pop()

    return collapsed
