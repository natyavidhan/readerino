"""Text extraction for the three supported input formats. Each extractor
returns (title, author, body_text) — plain text, no markup, not yet
wrapped or ASCII-sanitized (that happens in pack.py)."""

from pathlib import Path


def extract_txt(path: Path) -> tuple[str, str, str]:
    raw = path.read_text(encoding="utf-8", errors="replace")
    lines = raw.split("\n")
    title = None
    body = raw
    # our own essays (and many plain-text ebooks) put "Title\n\n" up front
    if len(lines) >= 2 and lines[0].strip() and not lines[1].strip() and len(lines[0]) <= 80:
        title = lines[0].strip()
        body = "\n".join(lines[2:])
    if not title:
        title = path.stem.replace("_", " ").replace("-", " ")
    return title, "", body


def extract_pdf(path: Path) -> tuple[str, str, str]:
    import pymupdf as fitz

    doc = fitz.open(path)
    try:
        meta_title = (doc.metadata.get("title") or "").strip()
        meta_author = (doc.metadata.get("author") or "").strip()
        body = "\n".join(page.get_text("text") for page in doc)
    finally:
        doc.close()
    title = meta_title or path.stem.replace("_", " ").replace("-", " ")
    return title, meta_author, body


def extract_epub(path: Path) -> tuple[str, str, str]:
    import ebooklib
    from ebooklib import epub
    from bs4 import BeautifulSoup

    book = epub.read_epub(str(path))

    title = ""
    titles = book.get_metadata("DC", "title")
    if titles:
        title = titles[0][0]
    if not title:
        title = path.stem.replace("_", " ").replace("-", " ")

    author = ""
    creators = book.get_metadata("DC", "creator")
    if creators:
        author = creators[0][0]

    parts = []
    for item in book.get_items():
        if item.get_type() == ebooklib.ITEM_DOCUMENT:
            soup = BeautifulSoup(item.get_content(), "html.parser")
            for tag in soup.find_all(["script", "style"]):
                tag.decompose()
            parts.append(soup.get_text("\n"))
    body = "\n\n".join(parts)
    return title, author, body


EXTRACTORS = {
    ".txt": extract_txt,
    ".pdf": extract_pdf,
    ".epub": extract_epub,
}
