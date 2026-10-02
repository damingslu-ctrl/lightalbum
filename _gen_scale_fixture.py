#!/usr/bin/env python3
# 生成方向 D「规模化与流畅性」回归门禁用的合成真实库 fixture。
# 规模固定 6425 张（镜像用户真库规模），覆盖多年份/多省份/GPS/分类/人物，
# 缩略图故意指向不存在文件（ThumbCache 负缓存兜底，验证缺失缩略图不崩）。
# 仅用于 LA_SCALE_SMOKE 默认规模化回归；LA_SCALE_DB 可指向用户真库覆盖。
import sqlite3, json, os, random

random.seed(20261002)
N = 6425
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_scale_fixture.db")
if os.path.exists(OUT):
    os.remove(OUT)

c = sqlite3.connect(OUT)
c.executescript("""
CREATE TABLE photos(
  path TEXT PRIMARY KEY, root TEXT, rel TEXT, src TEXT, size INTEGER,
  mtime INTEGER, thumb TEXT, exif_json TEXT, missing INTEGER,
  seen_at TEXT, category TEXT, cat_score REAL, cat_at TEXT,
  rating INTEGER, person TEXT);
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT);
CREATE TABLE vault(id INTEGER PRIMARY KEY, path TEXT, name TEXT, enc TEXT,
  thumb TEXT, size INTEGER, mtime INTEGER, created_at TEXT);
""")

provs = [
    ("china", "安徽省", "黄山市"), ("china", "北京市", "北京"), ("china", "四川省", "成都市"),
    ("china", "云南省", "丽江市"), ("china", "浙江省", "杭州市"), ("china", "广东省", "广州市"),
    ("china", "陕西省", "西安市"), ("china", "山东省", "青岛市"), ("china", "福建省", "厦门市"),
    ("china", "西藏自治区", "拉萨市"), ("china", "新疆维吾尔自治区", "乌鲁木齐市"),
    ("japan", "Tokyo", "东京"), ("iceland", "Capital Region", "雷克雅未克"),
    ("france", "Île-de-France", "巴黎"), ("usa", "California", "旧金山"),
]
cats = ["人像", "风光", "夜景·星空", "城市·建筑", "微距·花卉", "动物", "美食", "黑白", ""]
cams = ["Canon EOS R5", "Sony A7IV", "Nikon Z7", "iPhone 14 Pro", "Fujifilm X-T5",
        "Leica Q3", "Panasonic S5"]
persons = ["", "", "阿明", "小月", "老陈", "Lina", "Tom"]
lens = ["RF24-70mm F2.8", "FE 35mm F1.8", "Z 50mm f/1.8", "23mm f/2", "16-35mm F4"]
years = list(range(2008, 2026))

rows = []
for i in range(N):
    region, prov, city = provs[i % len(provs)]
    year = random.choice(years)
    month = random.randint(1, 12)
    day = random.randint(1, 28)
    date = f"{year}-{month:02d}-{day:02d}"
    has_gps = (i % 5) != 0  # 约 80% 有 GPS
    lo = round(random.uniform(-180, 180), 6)
    la = round(random.uniform(-60, 70), 6)
    cam = random.choice(cams)
    cat = random.choice(cats)
    person = random.choice(persons)
    rating = random.choice([0, 0, 0, 3, 4, 5, 5])
    exif = {
        "t": f"作品_{i:05d}", "d": date, "tm": f"{random.randint(0,23):02d}:{random.randint(0,59):02d}:00",
        "cam": cam, "lens": random.choice(lens), "ap": random.choice(["f/1.8", "f/2.8", "f/8", "f/11"]),
        "sh": random.choice(["1/250", "1/60s", "1/2000"]), "dim": f"{random.choice([6000,4000,8000])}x{random.choice([4000,3000,6000])}",
        "o": random.choice(["landscape", "portrait"]), "r": region, "pv": prov, "ct": city,
        "y": year, "iso": random.choice([100, 200, 400, 800, 1600]),
        "focal": random.choice([16, 24, 35, 50, 85, 135, 200]),
    }
    if has_gps:
        exif["lo"] = lo
        exif["la"] = la
    path = f"scale_lib/img_{i:05d}.jpg"
    rel = f"img_{i:05d}.jpg"
    thumb = f"scale_lib/thumb_{i:05d}.jpg"
    rows.append((
        path, "scale_lib", rel, "native", 1_000_000 + i, 1_700_000_000 + i,
        thumb, json.dumps(exif, ensure_ascii=False), 0, date,
        cat, 0.9, date, rating, person,
    ))

c.executemany(
    "INSERT INTO photos(path,root,rel,src,size,mtime,thumb,exif_json,missing,seen_at,category,cat_score,cat_at,rating,person)"
    " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", rows)
c.execute("INSERT INTO meta(key,value) VALUES('schema','scale_fixture_v1')")
c.commit()
c.close()
print(f"WROTE {OUT} photos={N} size={os.path.getsize(OUT)}")
