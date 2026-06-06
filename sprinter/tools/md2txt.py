#!/usr/bin/env python3
"""
md2txt.py — minimal Markdown -> plain text for the floppy docs.

Not a full Markdown engine: just strips the formatting that appears in our
README/HOWTO so the result reads cleanly in a DOS/Sprinter text viewer.
  * headings  -> UPPERCASE line + underline of '='/'-'
  * **bold** / *italic* / `code`  -> plain text
  * [text](url)  -> text (url)
  * bullet "- " / "* "  -> "  - "
  * fenced ``` code blocks -> kept verbatim (fence lines dropped)
Output uses CRLF line endings and ASCII only (smart punctuation folded).
"""
import sys
import re

SMART = {
    '—': '-', '–': '-', '‘': "'", '’': "'",
    '“': '"', '”': '"', '…': '...', ' ': ' ',
    '→': '->', '←': '<-', '×': 'x', '•': '-',
}


def fold(s):
    for k, v in SMART.items():
        s = s.replace(k, v)
    return ''.join(c if ord(c) < 128 else '?' for c in s)


def inline(s):
    s = re.sub(r'!\[[^\]]*\]\([^)]*\)', '', s)          # images
    s = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'\1 (\2)', s)  # links
    s = re.sub(r'`([^`]*)`', r'\1', s)                   # inline code
    s = re.sub(r'\*\*([^*]+)\*\*', r'\1', s)             # bold
    s = re.sub(r'(?<!\*)\*([^*]+)\*(?!\*)', r'\1', s)    # italic
    s = re.sub(r'__([^_]+)__', r'\1', s)
    return s


def convert(md):
    out, in_code = [], False
    for line in md.splitlines():
        if line.strip().startswith('```'):
            in_code = not in_code
            continue
        if in_code:
            out.append(fold(line))
            continue
        m = re.match(r'^(#{1,6})\s+(.*)$', line)
        if m:
            title = fold(inline(m.group(2))).rstrip()
            level = len(m.group(1))
            if out and out[-1] != '':
                out.append('')
            out.append(title.upper() if level <= 2 else title)
            out.append(('=' if level <= 2 else '-') * max(1, len(title)))
            continue
        m = re.match(r'^(\s*)[-*+]\s+(.*)$', line)
        if m:
            out.append('  ' + fold(inline(m.group(2))).rstrip())
            continue
        out.append(fold(inline(line)).rstrip())
    # collapse 3+ blank lines to 1
    txt, blanks = [], 0
    for l in out:
        if l == '':
            blanks += 1
            if blanks > 1:
                continue
        else:
            blanks = 0
        txt.append(l)
    return '\r\n'.join(txt).rstrip() + '\r\n'


def main():
    if len(sys.argv) != 3:
        print('usage: md2txt.py <in.md> <out.txt>', file=sys.stderr)
        sys.exit(2)
    with open(sys.argv[1], encoding='utf-8') as f:
        md = f.read()
    with open(sys.argv[2], 'w', newline='') as f:
        f.write(convert(md))


if __name__ == '__main__':
    main()
