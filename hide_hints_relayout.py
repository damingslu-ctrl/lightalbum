#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""1.7.8 — 隐藏所有交互说明并重新排版。
- 把「交互说明」面板（.map-hint，标题即"交互说明"）整体隐藏，但保留其中的
  「重新定位」按钮：移到地图右下角（与图例/返回键同一绝对定位层）。
- 隐藏灯箱控制提示 .lb-hint、地图悬停提示指令变体。
- 精简各 section 的 sec-sub（去掉"点击/按/翻页"等操作手册句，保留描述）。
- 删除/精简几处纯操作提示（编辑台提示、检视面板空态提示、胶片条空态、编辑器副标题）。
- 重新排版：移除 map-hint 后，region-list 拉伸占满 .map-side 整行。
遵循项目 CRLF 范式：二进制读 → 去 \r\n → rep(count==1 断言) → 写回 \n→\r\n。
"""
import io

PATH = 'index.html'

def rep(data, old, new):
    c = data.count(old)
    assert c == 1, 'count==1 expected, got %d for:\n%r' % (c, old[:60])
    return data.replace(old, new)

with io.open(PATH, 'rb') as f:
    raw = f.read()
data = raw.decode('utf-8').replace('\r\n', '\n')

# 1) 从 map-hint 移除「重新定位」按钮（移到地图右下角）
data = rep(data,
    '        • 接入真实足迹：<code>regions</code> 用 <code>lon/lat</code> 经纬度（东经、北纬为正）；给作品加 <code>img</code> 字段即可换成真实照片。 <button id="relocateBtn" class="relocate" type="button">重新定位</button>',
    '        • 接入真实足迹：<code>regions</code> 用 <code>lon/lat</code> 经纬度（东经、北纬为正）；给作品加 <code>img</code> 字段即可换成真实照片。')

# 2) 在地图图例后插入右下角「重新定位」按钮
data = rep(data,
    '      <div class="map-legend" id="mapLegend" aria-hidden="true"></div>',
    '      <div class="map-legend" id="mapLegend" aria-hidden="true"></div>\n      <button id="relocateBtn" class="map-relocate" type="button">重新定位</button>')

# 3) 地图 sec-sub：去掉"点击标点（或列表）放大地图并自动筛选作品。"
data = rep(data,
    '    <div class="sec-sub">一幅按真实国家边界绘制的精细世界地图，缓慢横向漂移如地球自转；金色航线以「我的位置」（进入页面自动读取定位）为基点，连向每一处拍摄地。点击标点（或列表）放大地图并自动筛选作品。连接本地库后，这里会按真实经纬度呈现你的足迹。</div>',
    '    <div class="sec-sub">一幅按真实国家边界绘制的精细世界地图，缓慢横向漂移如地球自转；金色航线以「我的位置」为基点，连向每一处拍摄地。连接本地库后，这里会按真实经纬度呈现你的足迹。</div>')

# 4) 系列 sec-sub：去掉操作手册句
data = rep(data,
    '    <div class="sec-sub">按光线与天象整理的长期系列 —— 只展示有作品的分类，卡片封面即该系列真实作品。点击卡片筛选图库，点分组「查看全部」以浮层浏览整组。</div>',
    '    <div class="sec-sub">按光线与天象整理的长期系列 —— 只展示有作品的分类，卡片封面即该系列真实作品。</div>')

# 5) 图库 sec-sub：几乎整段都是操作手册，精简为描述句
data = rep(data,
    '    <div class="sec-sub">按年份浏览，区分横构图 / 竖构图，支持关键词搜索与排序。按 <kbd>/</kbd> 可快速聚焦搜索框，点亮右上角星标可收藏（点「仅看收藏」只看喜欢的）；点击照片查看完整 EXIF，灯箱内可用 ← → 翻页、Home / End 跳首尾。筛选状态会写入网址，可直接分享当前视图。</div>',
    '    <div class="sec-sub">按年份浏览，区分横构图 / 竖构图，支持关键词搜索与排序。</div>')

# 6) 修图 sec-sub：去掉"不用再点「内嵌编辑」"
data = rep(data,
    '    <div class="sec-sub">编辑台常驻下方左列，锁定作品后画布自动就位 —— 不用再点「内嵌编辑」。引擎按真实 EXIF（ISO / 快门）决定降噪强度，按直方图实测决定各环节幅度，每一步改了什么都会列出来，并基于曝光、器材与天文数据给出拍摄建议。修图在本地客户端执行，输出 <code>*_retouched.jpg</code>，绝不覆盖原片。</div>',
    '    <div class="sec-sub">编辑台常驻下方左列，锁定作品后画布自动就位。引擎按真实 EXIF（ISO / 快门）决定降噪强度，按直方图实测决定各环节幅度，每一步改了什么都会列出来，并基于曝光、器材与天文数据给出拍摄建议。修图在本地客户端执行，输出 <code>*_retouched.jpg</code>，绝不覆盖原片。</div>')

# 7) 地图悬停预览提示：未钉住时的操作指令变体置空
data = rep(data,
    "pinned?'已筛选该地区':'悬停预览 · 点击查看全部'",
    "pinned?'已筛选该地区':''")

# 8) 检视面板空态提示：去掉"点击底部胶片条..."
data = rep(data,
    '点击底部<b>胶片条</b>中的任意作品，即可在此查看元数据、参数与地图定位。',
    '暂未选中作品。')

# 9) AI 建议空态：去掉"连接本地库并启用修图引擎后..."
data = rep(data,
    '连接本地库并启用修图引擎后，这里会给出适配本图的预设建议。',
    '暂无预设建议。')

# 10) 胶片条空态：去掉"连接本地库后这里会出现胶片条"
data = rep(data,
    '暂无作品或收藏，连接本地库后这里会出现胶片条。',
    '暂无作品或收藏。')

# 11) 编辑器副标题：去掉操作手册式描述
data = rep(data,
    '画笔涂抹要消除的区域 → AI 消除；裁剪/旋转/拉直/调整即时预览，应用后输出 *_edited.*',
    'AI 消除 · 裁剪 / 旋转 / 拉直 / 调整，应用后输出 *_edited.*')

# 12) 删除编辑台「去画廊点缩略图...」提示
data = rep(data,
    '        <p class="hint">修图结果与编辑台都只针对锁定的一张；去画廊点缩略图「🖌 跳转修图」即可换图。</p>\n',
    '')

# 13) 修图空态：去掉"→ 点「开始智能修图」"
data = rep(data,
    '从右侧锁定一幅作品 → 点「开始智能修图」。摄影建议不依赖客户端，选完作品即自动生成。',
    '锁定一幅作品即可开始智能修图。摄影建议不依赖客户端，选完作品即自动生成。')

# 14) 注入 CSS：隐藏交互说明 + 重新排版（插在首个 </style> 之前，确保在 fabric-lib 之前）
css = (
    '\n'
    '  /* 1.7.8 隐藏交互说明 + 重新排版 */\n'
    '  .map-hint{display:none!important}\n'
    '  .lb-hint{display:none!important}\n'
    '  .map-side .region-list{flex:1 1 320px;min-width:280px}\n'
    '  .map-relocate{position:absolute;right:12px;bottom:12px;z-index:7;font-size:12.5px;line-height:1;'
    'padding:7px 13px;border-radius:999px;border:1px solid rgba(255,255,255,.28);'
    'background:rgba(32,32,30,.82);color:#fff;cursor:pointer;'
    '-webkit-backdrop-filter:blur(6px);backdrop-filter:blur(6px);transition:background .18s,transform .18s}\n'
    '  .map-relocate:hover{background:rgba(32,32,30,.95);transform:translateY(-1px)}\n'
    '  body.dark .map-relocate{border-color:rgba(61,220,151,.4);color:#d8ffe9}\n'
    '  .map-preview .mp-tip:empty{display:none}\n'
)
idx_style = data.find('</style>')
idx_fabric = data.find('<script id="fabric-lib">')
assert idx_style != -1, 'no </style> found'
assert idx_fabric == -1 or idx_style < idx_fabric, 'CSS must be inserted before fabric-lib script'
data = data[:idx_style] + css + data[idx_style:]

with io.open(PATH, 'wb') as f:
    f.write(data.replace('\n', '\r\n').encode('utf-8'))

print('OK: 1.7.8 hide interaction hints + relayout applied')
