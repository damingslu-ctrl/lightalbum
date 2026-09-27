# -*- coding: utf-8 -*-
"""光影集 本地客户端 启动入口。

源码运行：  python run.py
打包运行：  PyInstaller 把本文件作为入口，打成单文件 exe。
"""
import os
import sys

# 保证包导入：源码态把本目录加入 sys.path（打包后 PyInstaller 已内置）
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from lightalbum.app import main  # noqa: E402

if __name__ == '__main__':
    sys.exit(main())
