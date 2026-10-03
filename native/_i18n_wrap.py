#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
i18n wrap pass for LightAlbum native.

Turns user-visible CJK string literals that sit in QString contexts
(QStringLiteral("...") / QString::fromUtf8("...")) into
QCoreApplication::translate("LightAlbum", "...") so lupdate can extract
them and the runtime translator can localize them.

Lines whose (file, line) is in SKIP stay raw on purpose: they are
comparison / lookup keys (category, nav, .value(), .contains()) whose
other operand is NOT translated, so translating only one side would
break filtering / picker logic.

Raw `const char*` label arrays (kHomeNav / kModeNames / kNav /
GalleryPage category list) are NOT touched because they are not wrapped
with QStringLiteral in the source; leaving them Chinese is a known,
documented gap for a follow-up pass.
"""
import os
import re

SRC = r"D:\Test\workbuddy\2026-09-18-09-00-13\native\src"

# (relative path under src, set of 1-based line numbers to LEAVE RAW)
TARGETS = {
    r"ui\LogPage.cpp": {519},
    r"core\CullEngine.cpp": set(),
    r"ui\CompareWidget.cpp": set(),
    r"ui\CullPage.cpp": {845, 846},
    r"ui\RetouchPage.cpp": set(),
    r"ui\MapPage.cpp": set(),
    r"ui\HomePage.cpp": set(),
    r"ui\GalleryPage.cpp": {19, 20, 21, 285},
    r"ui\MainWindow.cpp": {66, 67, 68, 1065},
    r"ui\MapView.cpp": {927, 1169},
}

LIT = re.compile(r'QStringLiteral\(\s*"((?:[^"\\]|\\.)*)"\s*\)')
UTF8 = re.compile(r'QString::fromUtf8\(\s*"((?:[^"\\]|\\.)*)"\s*\)')

CJK = re.compile(r"[一-鿿]")


def has_cjk(s: str) -> bool:
    return bool(CJK.search(s))


def split_code_comment(line: str):
    """Return (code, comment) splitting at // outside string literals."""
    in_str = False
    esc = False
    i = 0
    while i < len(line):
        c = line[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c == "/" and i + 1 < len(line) and line[i + 1] == "/":
                return line[:i], line[i:]
        i += 1
    return line, ""


def repl(m):
    inner = m.group(1)
    return 'QCoreApplication::translate("LightAlbum", "%s")' % inner


def transform_line(code: str):
    changed = False

    def sub_lit(mm):
        nonlocal changed
        if has_cjk(mm.group(1)):
            changed = True
            return repl(mm)
        return mm.group(0)

    def sub_utf8(mm):
        nonlocal changed
        if has_cjk(mm.group(1)):
            changed = True
            return repl(mm)
        return mm.group(0)

    new = LIT.sub(sub_lit, code)
    new = UTF8.sub(sub_utf8, new)
    return new, changed


def ensure_include(src: str) -> str:
    if "QCoreApplication::translate" not in src:
        return src
    if "#include <QCoreApplication>" in src or "#include <QApplication>" in src:
        return src
    lines = src.split("\n")
    for idx, ln in enumerate(lines):
        if ln.lstrip().startswith("#include"):
            lines.insert(idx + 1, "#include <QCoreApplication>")
            return "\n".join(lines)
    # fallback: prepend
    return "#include <QCoreApplication>\n" + src


def main():
    grand = 0
    for rel, skip in TARGETS.items():
        path = os.path.join(SRC, rel)
        with open(path, "r", encoding="utf-8") as f:
            lines = f.read().split("\n")
        new_lines = []
        file_count = 0
        for i, line in enumerate(lines, start=1):
            if i in skip:
                new_lines.append(line)
                continue
            code, comment = split_code_comment(line)
            new_code, changed = transform_line(code)
            if changed:
                file_count += 1
            new_lines.append(new_code + comment)
        if file_count:
            out = ensure_include("\n".join(new_lines))
            with open(path, "w", encoding="utf-8") as f:
                f.write(out)
            grand += file_count
            print("WRAPPED %-28s %d sites" % (rel, file_count))
    print("TOTAL sites wrapped: %d" % grand)


if __name__ == "__main__":
    main()
