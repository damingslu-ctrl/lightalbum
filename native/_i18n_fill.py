#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Fill English translations into LightAlbum_en.ts from a curated dict.

Source keys are the PLAIN (entity-decoded) form as extracted by lupdate.
English values are plain text; <, >, & are re-escaped on write so the
few HTML-rich strings stay valid TS XML.
"""
import re

TS = r"D:\Test\workbuddy\2026-09-18-09-00-13\native\i18n\LightAlbum_en.ts"

DICT = {
    "摄影日志": "Photo Log",
    "全部": "All",
    "导出 CSV": "Export CSV",
    "光圈分布": "Aperture Distribution",
    "常用景深与通光量": "Depth of field & light gathering",
    "快门速度": "Shutter Speed",
    "从高速定格到长曝": "From high-speed freeze to long exposure",
    "感光度 ISO": "ISO Sensitivity",
    "画质与噪点的权衡": "Image quality vs. noise",
    "镜头焦距": "Focal Length",
    "广角到长焦的取舍": "Wide to telephoto trade-offs",
    "器材使用": "Gear Usage",
    "机身 / 镜头出勤率": "Camera body / lens usage rate",
    "按月的拍摄活跃度": "Monthly Shooting Activity",
    "一年里的创作节律": "Your Creative Rhythm Over the Year",
    "其他": "Other",
    "幅": "shots",
    "使用机身": "Camera Body Used",
    "已收藏": "Favorited",
    "最常机身": "Most Used Body",
    "最常焦段": "Most Used Focal Length",
    "导出摄影日志 CSV": "Export Photo Log CSV",
    "未知": "Unknown",
    "严重欠曝": "Severely Underexposed",
    "严重过曝": "Severely Overexposed",
    "欠曝": "Underexposed",
    "过曝": "Overexposed",
    "正常": "Normal",
    "预处理 %1/%2": "Preprocessing %1/%2",
    "完全重复（MD5 相同）": "Exact Duplicate (identical MD5)",
    "高度相似（感知哈希）": "Near-Duplicate (perceptual hash)",
    "分辨率过低": "Resolution Too Low",
    "疑似模糊/对焦不实": "Possibly Blurry / Out of Focus",
    "无图像": "No Image",
    "原图": "Original",
    "修后": "Edited",
    "无缩略图": "No Thumbnail",
    "♥ 喜欢": "\u2665 Liked",
    "已略过": "Skipped",
    "已锁定": "Locked",
    "本地精选": "Local Pick",
    "喜": "L",
    "略": "S",
    "锁": "K",
    "引擎倾向": "Engine Suggestion",
    "已分析": "Analyzed",
    "候选": "Candidates",
    "废片": "Rejects",
    "去重组": "Duplicate Groups",
    "模糊": "Blurry",
    "曝光": "Exposure",
    "解码失败": "Decode Failed",
    "开始分析": "Start Analysis",
    "目标张数": "Target Count",
    "张": "shots",
    "只看喜欢": "Liked Only",
    "只看略过": "Skipped Only",
    "只看锁定": "Locked Only",
    "基于本地缩略图 · 零云端 · 失败即放行": "Local thumbnails only · zero cloud · fail-open",
    "开始对决": "Start Duel",
    "喜欢": "Like",
    "略过": "Skip",
    "锁定": "Lock",
    "解除": "Unlock",
    "锁定区": "Locked Zone",
    "返回总览 Esc": "Back to Overview (Esc)",
    "左留 ←": "Keep Left ←",
    "双弃 ↓": "Drop Both ↓",
    "双锁 S": "Lock Both (S)",
    "右留 →": "Keep Right →",
    "引擎按分预选（金框）· 败者自动标「略过」· 幸存者留用": "Engine pre-selects by score (gold frame) · losers auto-tagged \"Skipped\" · survivors kept",
    "审美档案": "Aesthetic Profile",
    "无": "None",
    "· 已喜欢": "· Liked",
    "· 已略过": "· Skipped",
    "· 已锁定": "· Locked",
    "锁定区（%1）": "Locked Zone (%1)",
    "锁定照片会出现在这里 · 选中卡片后点「锁定」": "Locked photos appear here · select a card then tap \"Lock\"",
    "保留作品会出现在这条胶片 · 选中后点 ♥/锁": "Kept shots appear on this strip · select then tap \u2665/Lock",
    "广角": "Wide",
    "长焦": "Telephoto",
    "大光圈": "Large Aperture",
    "弱光": "Low Light",
    "人像": "Portrait",
    "黑白": "Monochrome",
    "样本较少（%1 张），仅供参考；这只是设备与场景分布的客观统计，不等于读懂了你的审美。": "Few samples (%1 shots), for reference only; this is an objective tally of gear and scene, not a read of your taste.",
    "基于 %1 张作品 · 设备/场景分布，非审美判定 · 零云端": "Based on %1 shots · gear/scene distribution, not a taste judgment · zero cloud",
    "样本 %1 张 · 仅本地可测维度": "Sample %1 shots · local-only measurable dimensions",
    "无可用缩略图": "No Usable Thumbnails",
    "分析中 0/%1": "Analyzing 0/%1",
    "分析中 %1/%2": "Analyzing %1/%2",
    "完成 · 候选 %1 / 废片 %2 / 去重 %3 组": "Done · %1 candidates / %2 rejects / %3 duplicate groups",
    "开始对决（%1 组）": "Start Duel (%1 groups)",
    "对决完成 · 共 %1 组 · 点「返回总览」查看战果": "Duel complete · %1 groups · tap \"Back to Overview\" for results",
    "第 %1 / %2 组 · 本组已决 %3 · 在场 %4": "Group %1 / %2 · %3 decided · %4 present",
    "— 手动 —": "— Manual —",
    "照片": "Photo",
    "预设": "Presets",
    "智能建议：": "Smart Suggestion:",
    "未调整 · 零云端": "Unadjusted · zero cloud",
    "智能增强": "Smart Enhance",
    "AI 智能体": "AI Agent",
    "重置": "Reset",
    "↶ 撤销": "↶ Undo",
    "↷ 重做": "↷ Redo",
    "导出副本": "Export Copy",
    "滤镜预设": "Filter Presets",
    "几何编辑": "Geometry",
    "旋转⟲": "Rotate ⟲",
    "旋转⟳": "Rotate ⟳",
    "⇋ 水平": "Flip ⇋ H",
    "⇅ 垂直": "Flip ⇅ V",
    "向左旋转 90°": "Rotate Left 90°",
    "向右旋转 90°": "Rotate Right 90°",
    "水平翻转（镜像）": "Flip Horizontal (mirror)",
    "垂直翻转（镜像）": "Flip Vertical (mirror)",
    "拉直 ±10°": "Straighten ±10°",
    "自由": "Free",
    "应用裁剪": "Apply Crop",
    "重置几何": "Reset Geometry",
    "✂ 裁剪": "✂ Crop",
    "拉直": "Straighten",
    "裁剪": "Crop",
    "🖌 蒙版模式": "🖌 Mask Mode",
    "清除蒙版": "Clear Mask",
    "局部应用": "Apply Locally",
    "笔刷大小（像素）": "Brush Size (px)",
    "🧹 AI消除": "🧹 AI Erase",
    "未配置": "Not Configured",
    "模型槽位：%1 · 执行器未接入 · 零云端": "Model slot: %1 · executor not connected · zero cloud",
    "笔刷": "Brush",
    "◐ 对比": "◐ Compare",
    "拖动金色分割线对比原图与修后 · 零云端": "Drag the gold split line to compare original vs edited · zero cloud",
    "AI 智能体（Lumen × 本地 LLM）": "AI Agent (Lumen × local LLM)",
    "目标（可选）：如 风光通透 / 胶片感": "Goal (optional): e.g. crisp landscape / film look",
    "描述目标，本地测量 → 本地 LLM 决策 → 所见即所得导出。": "Describe your goal, local measurement → local LLM decides → WYSIWYG export.",
    "%1 · 零云端": "%1 · zero cloud",
    "AI 智能体决策中…（本地测量 → 本地 LLM）": "AI agent deciding… (local measurement → local LLM)",
    "智能体决策失败：%1": "Agent decision failed: %1",
    "AI 智能体 · %1 · %2 · 零云端": "AI Agent · %1 · %2 · zero cloud",
    "AI 智能体 · 本地规则兜底 · %1 · 零云端": "AI Agent · local rule fallback · %1 · zero cloud",
    "%1 依据：%2": "%1 basis: %2",
    "涂抹要调整的区域 · 零云端": "Paint the area to adjust · zero cloud",
    "退出蒙版 · 蒙版保留可再局部应用": "Exit mask · mask retained for later local apply",
    "蒙版已清除 · 零云端": "Mask cleared · zero cloud",
    "先在蒙版模式涂抹要调整的区域": "First paint the area to adjust in mask mode",
    "局部应用失败": "Local apply failed",
    "局部调整已应用 · 零云端": "Local adjustment applied · zero cloud",
    "拖角缩放 / 框内拖移，「应用裁剪」提交 · 零云端": "Drag corners to scale / move inside frame, submit with \"Apply Crop\" · zero cloud",
    "拖动金色分割线对比 · 零云端": "Drag the gold split line to compare · zero cloud",
    "先用蒙版模式涂抹要消除的区域": "First paint the area to erase in mask mode",
    "任务#%1 %2 · %3": "Task #%1 %2 · %3",
    "AI消除结果已回填 · 零云端": "AI erase result written back · zero cloud",
    "导出修后副本": "Export Edited Copy",
    "已导出 · %1 · 零云端": "Exported · %1 · zero cloud",
    "导出失败": "Export Failed",
    "曝光均衡，无需修正": "Exposure balanced, no correction needed",
    "← 返回": "← Back",
    "世界": "World",
    "回到默认视角：缩放归 1、中国居中": "Return to default view: zoom reset to 1, China centered",
    "◎ 重新定位": "◎ Relocate",
    "设置金色航线的起点（我的位置）": "Set the start of the gold route (my location)",
    "区域统计": "Region Stats",
    "尚未选择区域": "No Region Selected",
    "在图库查看 →": "View in Library →",
    "▶ 播放本区作品": "▶ Play This Region's Photos",
    "用爆炸弹层播放该省/国家/大区的全部作品（最多 10 张）": "Play all photos of this province/country/region with an explosion overlay (up to 10)",
    "本库作品总计：<b>%1</b> 幅<br>": "Total works in library: <b>%1</b> shots<br>",
    "该省作品：<b>%1</b> 幅": "Photos in this province: <b>%1</b> shots",
    "该大区作品：<b>%1</b> 幅": "Photos in this region: <b>%1</b> shots",
    "<br><span style='color:#5a6b60;'>（该区域暂无 GPS 作品，可导入带 GPS 的照片）</span>": "<br><span style='color:#5a6b60;'>(No GPS photos in this region yet; import photos with GPS)</span>",
    "该国家/地区 GPS 作品：<b>%1</b> 幅": "GPS photos in this country/region: <b>%1</b> shots",
    "<br><span style='color:#5a6b60;'>（该国暂无 GPS 作品）</span>": "<br><span style='color:#5a6b60;'>(No GPS photos in this country yet)</span>",
    "重新定位 · 航线基点": "Relocate · Route Origin",
    "自定义经纬度…": "Custom lat/long…",
    "经度 (东经为正)": "Longitude (east positive)",
    "纬度 (北纬为正)": "Latitude (north positive)",
    "确定": "OK",
    "取消": "Cancel",
    "%1 · %2 幅": "%1 · %2 shots",
    "%1 — %2 · %3 幅": "%1 — %2 · %3 shots",
    "%1 幅": "%1 shots",
    "光影集": "LightAlbum",
    "光": "L",
    "导入目录": "Import Folder",
    "导入 iCloud 相册": "Import iCloud Photos",
    "←  上一章": "← Previous Chapter",
    "下一章  →": "Next Chapter →",
    "定位覆盖 %1 / %2 幅 · 共 %3 章": "Location coverage %1 / %2 shots · %3 chapters total",
    "搜索 地点 / 时间 / 评分 / 关键词…": "Search place / time / rating / keyword…",
    "🔒 私密": "🔒 Private",
    "取消选择": "Clear Selection",
    "已选 %1 张": "%1 selected",
    "将选中的 %1 张设为 %2 星": "Set %1 selected to %2 stars",
    "全部 %1": "All %1",
    "其他 %1": "Other %1",
    "未分类 %1": "Uncategorized %1",
    "全部人物": "All People",
    "已筛": "Filtered",
    "未分类": "Uncategorized",
    "分类：%1": "Category: %1",
    "人物：%1": "Person: %1",
    "已选 %1": "%1 selected",
    "显示 <b>%1</b> / %2 幅": "Showing <b>%1</b> / %2 shots",
    "显示 <b>%1</b> / %2 幅%3": "Showing <b>%1</b> / %2 shots%3",
    "显示 <b>%1</b> / %2 幅（区域筛选）": "Showing <b>%1</b> / %2 shots (region filter)",
    "评分 %1": "Rate %1",
    "🔓 已私密": "🔓 Unprivate",
    "修图台 →": "Retouch Studio →",
    "关闭": "Close",
    "首页": "Home",
    "搜索标题 / 文件名 / 器材…": "Search title / filename / gear…",
    "隐私库": "Private Vault",
    "搜索（图库筛选）": "Search (library filter)",
    "导入照片": "Import Photos",
    "批量评分": "Batch Rate",
    "请求 %1 张，成功 %2 张。%3": "Requested %1, succeeded %2. %3",
    "已原子提交。": "Atomically committed.",
    "失败并已整体回滚：%1": "Failed and fully rolled back: %1",
    "批量私密": "Batch Private",
    "设置隐私库密码": "Set Private Vault Password",
    "为隐私库设置密码（标记为私密的照片需输入密码才可见）：": "Set a password for the private vault (photos marked private require the password to view):",
    "确认密码": "Confirm Password",
    "再输入一次相同密码：": "Enter the same password again:",
    "两次输入不一致，未设置。": "The two entries do not match; not set.",
    "🔓 隐私库(已解锁)": "🔓 Private Vault (unlocked)",
    "解锁隐私库": "Unlock Private Vault",
    "输入隐私库密码：": "Enter private vault password:",
    "密码不正确。": "Incorrect password.",
    "🔒 隐私库": "🔒 Private Vault",
    "即将推出": "Coming Soon",
    "光影集 LightAlbum %1": "LightAlbum %1",
    "核心客户端 · 本地优先": "Core client · local-first",
    "导入目录…": "Import Folder…",
    "正在检测 iCloud…": "Detecting iCloud…",
    "未检测到 iCloud 本地相册（%1 个文件待同步）": "No local iCloud photo library detected (%1 files pending sync)",
    "iCloud 本地目录 %1 共 %2 个文件": "iCloud local directory %1, %2 files total",
    "已检测：%1 个文件": "Detected: %1 files",
    "开始导入": "Start Import",
    "改用手动选择…": "Use manual selection…",
    "选择要导入的照片目录": "Choose photo folder to import",
    "正在扫描导入…": "Scanning import…",
    "云端占位跳过 %1 张": "Cloud placeholders skipped: %1",
    "错误 %1 条： %2": "Errors: %1 — %2",
    "· 完成": "· Done",
    "重启载入": "Reload",
    "稍后": "Later",
    "足迹地图 · 10 大区 / 233 省 · 本库 GPS 足迹 %1（滚轮缩放 · 拖动平移）": "Footprint map · 10 regions / 233 provinces · %1 GPS footprints in library (scroll to zoom · drag to pan)",
    "缩放 ×%1": "Zoom ×%1",
    # keys with leading whitespace / embedded newlines
    " 张": " shots",
    " · 已喜欢": " · Liked",
    " · 已略过": " · Skipped",
    " · 已锁定": " · Locked",
    " · 完成": " · Done",
    "%1\n依据：%2": "%1\nbasis: %2",
    "iCloud 本地目录\n%1\n共 %2 个文件": "iCloud local directory\n%1\n%2 files total",
    "\n云端占位跳过 %1 张": "\nCloud placeholders skipped: %1",
    "\n\n错误 %1 条：\n%2": "\n\nErrors: %1 — %2",
}


def decode_entities(s: str) -> str:
    return (s.replace("&lt;", "<").replace("&gt;", ">")
             .replace("&apos;", "'").replace("&quot;", '"')
             .replace("&amp;", "&"))


def encode_entities(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def main():
    with open(TS, "r", encoding="utf-8") as f:
        txt = f.read()

    # Operate per <message> block so a filled block never spans into the
    # next unfinished translation (which would corrupt the file).
    block_pat = re.compile(r"<message>.*?</message>", re.S)

    matched = 0
    missing = []

    def repl_block(m):
        nonlocal matched
        blk = m.group(0)
        if 'type="unfinished"' not in blk:
            return blk
        sm = re.search(r"<source>(.*?)</source>", blk, re.S)
        if not sm:
            return blk
        raw = sm.group(1)
        plain = decode_entities(raw)
        if plain in DICT:
            matched += 1
            en = encode_entities(DICT[plain])
            return blk.replace(
                '<translation type="unfinished"></translation>',
                "<translation>%s</translation>" % en,
                1,
            )
        missing.append(plain)
        return blk

    out = block_pat.sub(repl_block, txt)
    with open(TS, "w", encoding="utf-8") as f:
        f.write(out)

    print("Filled %d translations" % matched)
    if missing:
        print("MISSING (%d):" % len(missing))
        for x in missing:
            print("  -", repr(x))


if __name__ == "__main__":
    main()
