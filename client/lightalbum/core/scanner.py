# -*- coding: utf-8 -*-
"""
目录扫描：递归枚举照片 + 从路径推导元数据。

设计要点
--------
1. 扫描与 EXIF 解耦：先快速枚举出文件清单（毫秒级，可立即回 UI 显示进度），
   EXIF 是逐张慢操作，由调用方分批喂进来。
2. 路径推导只在 EXIF 缺失时兜底 —— GPS 优先，其次才是目录名。
   目录名里常见中文地名，这里内置一份「中英文 -> 网页版 REGIONS / PROVINCES 键」的映射。
3. 跳过明显不是作品资源的目录（缩略图缓存、导出目录等），避免扫出几千张无用图。
"""
from __future__ import annotations

import os
import re

from .exif import IMG_EXT

# 扫描时跳过的目录（小写名匹配）
SKIP_DIRS = {
    '.git', 'node_modules', '__pycache__', '.svn', '.cache', 'cache',
    'thumbnails', 'thumbs', '.thumbnails', 'lightroom', '.lightroom',
    'exports', 'export', '导出', '备份', 'backup', '.trash', 'system volume information',
    # 客户端产物工作目录：外部编辑副本/无头渲染成片会经「回灌」正式入库，
    # 原目录里的这份工作文件绝不能再被扫成独立照片（否则双重入库）
    '_external_edit', '_develop',
    '$recycle.bin', 'venv', '.venv', 'site-packages',
}

# ------------------------------------------------- 地区识别（与网页版 REGIONS 对齐）
# REGIONS: iceland/norway/morocco/namibia/china/japan/newzealand/usa/peru/bolivia
REGION_ALIAS = {
    'iceland':   ['iceland', 'ísland', '冰岛', 'reykjavik', 'reykjavík'],
    'norway':    ['norway', 'norge', '挪威', 'lofoten', '罗弗敦', 'troms', '特罗姆瑟', 'nordland'],
    'morocco':   ['morocco', 'maroc', '摩洛哥', 'marrakech', '马拉喀什', 'sahara', '撒哈拉'],
    'namibia':   ['namibia', '纳米比亚', 'sossusvlei', 'deadvlei', 'namib'],
    'china':     ['china', '中国', 'cn', 'hunan', '湖南', 'anhui', '安徽', 'sichuan', '四川',
                  'yunnan', '云南', 'guangxi', '广西', 'xinjiang', '新疆', 'tibet', '西藏',
                  'zhangjiajie', '张家界', 'guilin', '桂林'],
    'japan':     ['japan', '日本', 'nippon', 'kyoto', '京都', 'hokkaido', '北海道', 'tokyo', '东京'],
    'newzealand': ['new zealand', 'newzealand', 'nz', '新西兰', 'canterbury', 'otago'],
    'usa':       ['usa', 'united states', 'america', '美国', 'arizona', '亚利桑那', 'utah', '犹他'],
    'peru':      ['peru', '秘鲁', 'cusco', '库斯科', 'puno'],
    'bolivia':   ['bolivia', '玻利维亚', 'potosi', '波托西', 'uyuni', '乌尤尼'],
}

# 系列识别（与网页版 SERIES 四个键对齐）
SERIES_ALIAS = {
    '蓝调时刻': ['blue', 'blues', '蓝调', 'twilight', '暮色', 'dusk', '夜幕'],
    '黄金时刻': ['golden', '黄金', 'sunrise', 'sunset', '日出', '日落', '晨光', '暮光', '晨曦'],
    '星汉灿烂': ['astro', 'star', 'stars', 'milky', '银河', '星空', '星轨', 'aurora', '极光', 'night', '夜景'],
    '日月交辉': ['sun', 'moon', '日月', '日月交辉', 'moonrise', '月出', 'eclipse', '日食', '月食'],
}

# 省份识别：按地区分组的「别名 -> PROVINCES 规范英文键」。
# 覆盖全部 10 个国家 —— 早先只写了 china/usa/japan/newzealand 四国，
# 导致挪威、冰岛这类国家下钻时省份为空（测试暴露的真 bug）。
# 别名一律小写比较；中文名来自网页版 PROV_ZH 的反查。
PROV_ALIAS = {
    'china': {
        '湖南': 'Hunan Province', '安徽': 'Anhui Province', '四川': 'Sichuan Province',
        '云南': 'Yunnan Province', '广西': 'Guangxi Zhuang Autonomous Region',
        '新疆': 'Xinjiang Uygur Autonomous Region', '西藏': 'Tibet Autonomous Region',
        '北京': 'Beijing Municipality', '上海': 'Shanghai Municipality',
        '广东': 'Guangdong Province', '浙江': 'Zhejiang Province', '江苏': 'Jiangsu Province',
        '福建': 'Fujian Province', '山东': 'Shandong Province', '湖北': 'Hubei Province',
        '陕西': 'Shaanxi Province', '甘肃': 'Gansu Province', '青海': 'Qinghai Province',
        '内蒙古': 'Inner Mongolia Autonomous Region', '黑龙江': 'Heilongjiang Province',
        '吉林': 'Jilin Province', '辽宁': 'Liaoning Province', '河北': 'Hebei Province',
        '河南': 'Henan Province', '山西': 'Shanxi Province', '江西': 'Jiangxi Province',
        '贵州': 'Guizhou Province', '海南': 'Hainan Province', '台湾': 'Taiwan Province',
        '香港': 'Hong Kong S.A.R.', '澳门': 'Macau S.A.R.', '重庆': 'Chongqing Municipality',
        '天津': 'Tianjin Municipality', '宁夏': 'Ningxia Hui Autonomous Region',
        'hunan': 'Hunan Province', 'anhui': 'Anhui Province', 'sichuan': 'Sichuan Province',
        'yunnan': 'Yunnan Province', 'guangxi': 'Guangxi Zhuang Autonomous Region',
        'xinjiang': 'Xinjiang Uygur Autonomous Region', 'tibet': 'Tibet Autonomous Region',
        'beijing': 'Beijing Municipality', 'shanghai': 'Shanghai Municipality',
    },
    'usa': {
        'arizona': 'Arizona', 'utah': 'Utah', 'california': 'California', 'nevada': 'Nevada',
        'colorado': 'Colorado', 'wyoming': 'Wyoming', 'alaska': 'Alaska', 'hawaii': 'Hawaii',
        'new mexico': 'New Mexico', 'texas': 'Texas', 'oregon': 'Oregon', 'montana': 'Montana',
        'idaho': 'Idaho', 'washington': 'Washington',
    },
    'japan': {
        'kyoto': 'Kyoto', '京都': 'Kyoto', 'hokkaido': 'Hokkaido', '北海道': 'Hokkaido',
        'tokyo': 'Tokyo', '东京': 'Tokyo', 'osaka': 'Osaka', 'nagano': 'Nagano',
    },
    'newzealand': {
        'canterbury': 'Canterbury', 'otago': 'Otago', 'west coast': 'West Coast',
        'auckland': 'Auckland', 'wellington': 'Wellington',
    },
    'norway': {
        'nordland': 'Nordland', '诺尔兰': 'Nordland', 'lofoten': 'Nordland', '罗弗敦': 'Nordland',
        'troms': 'Troms og Finnmark', 'finnmark': 'Troms og Finnmark',
        '特罗姆瑟': 'Troms og Finnmark', 'oslo': 'Oslo', 'vestland': 'Vestland',
        'rogaland': 'Rogaland', 'agder': 'Agder', 'innlandet': 'Innlandet',
        'viken': 'Viken', 'trøndelag': 'Trøndelag', 'trondelag': 'Trøndelag',
    },
    'iceland': {
        'capital region': 'Capital Region', 'reykjavik': 'Capital Region',
        '雷克雅未克': 'Capital Region', '首都': 'Capital Region',
        'southern region': 'Southern Region', 'vik': 'Southern Region', '维克': 'Southern Region',
        'southern peninsula': 'Southern Peninsula',
        'western region': 'Western Region', 'westfjords': 'Westfjords', '西峡湾': 'Westfjords',
        'northwestern region': 'Northwestern Region', 'northeastern region': 'Northeastern Region',
        'eastern region': 'Eastern Region',
    },
    'morocco': {
        'marrakech': 'Marrakech-Safi', 'marrakesh': 'Marrakech-Safi', '马拉喀什': 'Marrakech-Safi',
        'casablanca': 'Casablanca-Settat', 'rabat': 'Rabat-Salé-Kenitra',
        'fez': 'Fez-Meknes', 'meknes': 'Fez-Meknes', '非斯': 'Fez-Meknes',
        'tangier': 'Tangier-Tetouan-Al Hoceima', 'sahara': 'Drâa-Tafilalet',
        'merzouga': 'Drâa-Tafilalet', '撒哈拉': 'Drâa-Tafilalet',
        'souss': 'Souss-Massa', 'agadir': 'Souss-Massa',
    },
    'namibia': {
        'khomas': 'Khomas', 'windhoek': 'Khomas', '温得和克': 'Khomas',
        'hardap': 'Hardap', 'karas': 'Karas', 'erongo': 'Erongo', 'swakopmund': 'Erongo',
        'kunene': 'Kunene', 'otjozondjupa': 'Otjozondjupa', 'omaheke': 'Omaheke',
        'sossusvlei': 'Hardap', 'deadvlei': 'Hardap', 'namib': 'Hardap',
    },
    'peru': {
        'cusco': 'Cusco', 'cuzco': 'Cusco', '库斯科': 'Cusco',
        'puno': 'Puno', '普诺': 'Puno', 'lima': 'Lima', 'arequipa': 'Arequipa',
        'ancash': 'Ancash', 'huaraz': 'Ancash', 'ica': 'Ica', 'junin': 'Junín', 'junín': 'Junín',
    },
    'bolivia': {
        'potosi': 'Potosí', 'potosí': 'Potosí', '波托西': 'Potosí',
        'oruro': 'Oruro', 'la paz': 'La Paz', 'lapaz': 'La Paz',
        'santa cruz': 'Santa Cruz', 'cochabamba': 'Cochabamba', 'tarija': 'Tarija',
        'uyuni': 'Potosí', '乌尤尼': 'Potosí',
    },
}

# 城市提示：识别到地区/省之后，取其下一层目录作为城市
CITY_STOPWORDS = {'raw', 'jpg', 'jpeg', 'edited', '导出', '精选', 'selected', '原片', '成片', '原图'}

_DATE_PAT = [
    re.compile(r'(20\d{2})[-_.]?(\d{2})[-_.]?(\d{2})'),
    re.compile(r'(20\d{2})[-_.]?(1[0-2]|0[1-9])[-_.]?(3[01]|[12]\d|0[1-9])'),
]
_YEAR_PAT = re.compile(r'(20\d{2})')


# 智能修图的派生产物后缀 —— 它们是原片的衍生版本，不是独立作品，
# 必须从扫描里排除，否则「修图 → 重扫」每次都会把修过的图当新照片
# 重复入库（实测踩过：3 张修完一重扫变 6 张）。
RETOUCH_SUFFIX = '_retouched'


def is_photo(name):
    stem = os.path.splitext(name)[0].lower()
    if stem.endswith(RETOUCH_SUFFIX):
        return False
    return os.path.splitext(name)[1].lower() in IMG_EXT


def scan_dir(root, on_progress=None, max_files=20000):
    """递归扫描目录，返回相对路径列表（正斜杠）。

    on_progress(found, scanned) 每 200 个文件回调一次，供 UI 显示进度。
    """
    root = os.path.abspath(root)
    found, scanned = [], 0
    for dirpath, dirnames, filenames in os.walk(root):
        # 就地剪枝跳过目录（比扫完再过滤快得多）
        dirnames[:] = [d for d in dirnames if d.lower() not in SKIP_DIRS and not d.startswith('.')]
        rel_dir = os.path.relpath(dirpath, root).replace('\\', '/')
        for fn in filenames:
            scanned += 1
            if scanned % 200 == 0 and on_progress:
                try:
                    on_progress(len(found), scanned)
                except Exception:
                    pass
            if not is_photo(fn):
                continue
            rel = (rel_dir + '/' + fn) if rel_dir != '.' else fn
            found.append(rel)
            if len(found) >= max_files:
                return found
    if on_progress:
        try:
            on_progress(len(found), scanned)
        except Exception:
            pass
    return found


def guess_region(parts):
    """从路径片段猜地区 id。返回 (region_id, province_key, city)。"""
    low = [p.lower() for p in parts]
    joined_low = ' / '.join(low)
    rid = None
    for key, aliases in REGION_ALIAS.items():
        for a in aliases:
            if a.lower() in joined_low:
                rid = key
                break
        if rid:
            break
    if not rid:
        return None, '', ''

    pv = ''
    city = ''
    table = PROV_ALIAS.get(rid) or {}
    # 先按「完整目录片段」精确匹配（'湖南'、'Nordland'），
    # 再做子串匹配（'中国-湖南-张家界' 这种连写目录名）。
    for p in parts:
        if p in table:
            pv = table[p]
            break
        if p.lower() in table:
            pv = table[p.lower()]
            break
    if not pv:
        best = ''
        for a, v in table.items():
            if a.lower() in joined_low and len(a) > len(best):
                pv, best = v, a
    return rid, pv, city


def guess_series(name, parts):
    """从文件名 / 目录名猜系列。返回网页版 SERIES 的四个键之一，猜不出返回 ''。"""
    blob = (name + ' ' + ' '.join(parts)).lower()
    hit, best_len = '', 0
    for key, aliases in SERIES_ALIAS.items():
        for a in aliases:
            if a.lower() in blob and len(a) > best_len:
                hit, best_len = key, len(a)
    return hit


def guess_date(name, parts):
    """从文件名/目录名提取日期，返回 'YYYY-MM-DD'。"""
    for src in (name, ' '.join(parts)):
        for pat in _DATE_PAT:
            m = pat.search(src)
            if m:
                y, mo, d = m.group(1), int(m.group(2)), int(m.group(3))
                if 1 <= mo <= 12 and 1 <= d <= 31:
                    return '%s-%02d-%02d' % (y, mo, d)
    return ''


def title_from_name(name):
    """文件名 -> 标题：去扩展名、去日期前缀、下划线转空格。"""
    base = os.path.splitext(name)[0]
    base = re.sub(r'^20\d{2}[-_.]?\d{2}[-_.]?\d{2}[-_.\s]*', '', base)
    base = re.sub(r'^(IMG|DSC|DSC_|IMG_|P|_DSC)\d+[-_\s]*', '', base, flags=re.I)
    base = base.replace('_', ' ').replace('-', ' ').strip()
    return base or os.path.splitext(name)[0]


def guess_from_path(rel):
    """综合推导：系列 / 日期 / 标题 / 地区 / 省 / 城市。"""
    rel = rel.replace('\\', '/')
    parts = [p for p in rel.split('/') if p]
    name = parts[-1] if parts else rel
    dirs = parts[:-1]
    rid, pv, _city = guess_region(parts)
    series = guess_series(name, dirs)
    date = guess_date(name, dirs)
    # 城市：取「地区/省」目录的下一层。若下一层就是省份名本身，
    # 再往下取一层；遇到 raw/导出 这类工作目录则跳过。
    city = ''
    if rid and pv and dirs:
        for i, p in enumerate(dirs):
            tbl = PROV_ALIAS.get(rid) or {}
            if tbl.get(p) == pv or tbl.get(p.lower()) == pv:
                j = i + 1
                while j < len(dirs) and dirs[j].lower() in CITY_STOPWORDS:
                    j += 1
                if j < len(dirs) and tbl.get(dirs[j]) != pv:
                    city = dirs[j]
                break
    return {
        't': title_from_name(name),
        's': series,
        'd': date,
        'r': rid or '',
        'pv': pv,
        'ct': city,
    }
