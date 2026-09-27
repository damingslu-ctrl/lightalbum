# -*- coding: utf-8 -*-
"""光影集 · 本地客户端 原生能力层

三个模块各自独立、可单独测试，不依赖 Qt：
    exif.py    —— 从 JPEG/HEIC/TIFF 读真实 EXIF（机身/镜头/焦距/光圈/快门/ISO/时间/GPS）
    scanner.py —— 递归扫描目录，按路径推导地区/省/城市/系列/日期
    thumbs.py  —— 缩略图生成与磁盘缓存
"""
