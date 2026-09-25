"""Word-wrap and ASCII sanitization shared by every input format, so the
device (with its plain 6x8 ASCII font) never has to deal with characters it
can't render.

Hard-wrapped source lines are joined back into paragraphs first, then each
paragraph is word-wrapped to the device width. Long words are hard-split,
and blank source lines are preserved as empty output lines (paragraph
spacing)."""

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


def _paragraphs(text: str) -> list[str]:
    """Joins hard-wrapped source lines back into paragraphs. Blank lines
    separate paragraphs; single newlines inside one are just the source's
    own line wrapping (plain-text essays and PDF extractions wrap at ~70
    columns), and wrapping those lines individually at a narrower width
    leaves a ragged one-word stub at the end of almost every source line."""
    paras, current = [], []
    for raw_line in text.split("\n"):
        words = raw_line.split()
        if words:
            current.extend(words)
        else:
            if current:
                paras.append(" ".join(current))
                current = []
            paras.append("")
    if current:
        paras.append(" ".join(current))
    return paras


def wrap_text(text: str, width: int) -> list[str]:
    text = to_ascii(text).replace("\r\n", "\n").replace("\r", "\n")
    out: list[str] = []
    for para in _paragraphs(text):
        if not para:
            out.append("")
            continue
        wrapped = textwrap.wrap(para, width=width, break_long_words=True, break_on_hyphens=False)
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
