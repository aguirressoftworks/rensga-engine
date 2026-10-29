"""Render the design doc Markdown to a styled standalone HTML page.

    python build_doc.py <output_dir>
"""
import os
import sys

import markdown

import build

EXTRA_CSS = """
.doc-body table{border-collapse:collapse;width:100%;font-size:13px;margin:12px 0 18px;display:block;overflow-x:auto}
.doc-body th,.doc-body td{border:.5px solid var(--border-strong);padding:6px 9px;text-align:left;vertical-align:top}
.doc-body th{background:var(--surface-1);font-weight:600}
.doc-body pre{background:var(--surface-1);border-radius:8px;padding:12px 14px;overflow-x:auto;font-size:12.5px;line-height:1.5}
.doc-body code{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:.92em}
.doc-body p code,.doc-body li code,.doc-body td code{background:var(--surface-1);padding:1px 4px;border-radius:4px}
.doc-body img{max-width:100%;height:auto;border:.5px solid var(--border);border-radius:12px;background:#fff;margin:8px 0 4px}
.doc-body h2{border-top:.5px solid var(--border);padding-top:20px}
.doc-body li{margin:3px 0}
"""


def main(out):
    src = os.path.join(out, "design-doc", "rensga-engine-design-doc.md")
    with open(src, encoding="utf-8") as f:
        text = f.read()
    body = markdown.markdown(text, extensions=["tables", "fenced_code", "sane_lists"])
    body = body.replace("<li>[ ] ", '<li><input type="checkbox" disabled> ')
    html_page = build.page("Design document", f'<div class="doc-body">{body}</div>', "../")
    html_page = html_page.replace("</style>", EXTRA_CSS + "</style>", 1)
    dst = os.path.join(out, "design-doc", "rensga-engine-design-doc.html")
    with open(dst, "w", encoding="utf-8") as f:
        f.write(html_page)
    print("wrote", dst)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "out")
