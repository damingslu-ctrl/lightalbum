#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CRLF-safe: in shootPages, wait longer on the 图库 page so async thumbnails load+repaint."""
import io

SRC = r"D:\Test\workbuddy\2026-09-18-09-00-13\native\src\ui\MainWindow.cpp"

with io.open(SRC, "rb") as f:
    raw = f.read()
text = raw.replace(b"\r\n", b"\n").decode("utf-8")

old = (
    '    for (int i = 0; i < nav_->count(); ++i) {\n'
    '        nav_->setCurrentRow(i);\n'
    '        QCoreApplication::processEvents(QEventLoop::AllEvents, 120);\n'
    '        const QPixmap pm = grab();\n'
)
new = (
    '    for (int i = 0; i < nav_->count(); ++i) {\n'
    '        nav_->setCurrentRow(i);\n'
    '        QCoreApplication::processEvents(QEventLoop::AllEvents, 120);\n'
    '        // P27：图库页缩略图为后台线程池异步加载（ThumbCache::LoadTask），\n'
    '        // 120ms 不足以让可见缩略图落盘并重绘；专用长等待让其到位后再 grab\n'
    '        //（否则 offscreen 直接抓到 96.7% 暗底，误判为黑边问题）。\n'
    '        if (QString::fromUtf8(kNav[i].label) == QStringLiteral("\u56fe\u5e93")) {\n'
    '            for (int w = 0; w < 9; ++w)\n'
    '                QCoreApplication::processEvents(QEventLoop::AllEvents, 400);\n'
    '        }\n'
    '        const QPixmap pm = grab();\n'
)
assert text.count(old) == 1, "shootPages anchor count=%d" % text.count(old)
text = text.replace(old, new)

with io.open(SRC, "wb") as f:
    f.write(text.replace("\n", "\r\n").encode("utf-8"))
print("OK shootPages patched (gallery long-wait)")
