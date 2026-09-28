// P8-2 路径推导实现 —— 移植自 client/lightalbum/core/scanner.py
// 匹配语义逐条对齐 Python（contains 子串 / 精确匹配 / 最长别名优先）。
#include "PathGuess.h"
#include <QRegularExpression>

namespace {

// ---- SKIP_DIRS（Python 同名集合，小写比较）----
const char* const kSkip[] = {
    ".git", "node_modules", "__pycache__", ".svn", ".cache", "cache",
    "thumbnails", "thumbs", ".thumbnails", "lightroom", ".lightroom",
    "exports", "export", "导出", "备份", "backup", ".trash",
    "system volume information", "_external_edit", "_develop",
    "$recycle.bin", "venv", ".venv", "site-packages",
};

// ---- IMG_EXT（Python exif.IMG_EXT）----
const char* const kImgExt[] = {
    ".jpg", ".jpeg", ".jpe", ".jfif", ".png", ".webp", ".tif", ".tiff",
    ".bmp", ".gif", ".avif", ".heic", ".heif",
};

// ---- 别名条目：key = 地区/系列键 或 省别名；value = 省规范键 ----
struct Alias { const char* key; const char* value; };

// REGION_ALIAS（顺序 = Python dict 顺序，先命中先赢；同 region 别名相邻）
const Alias kRegionAlias[] = {
    {"iceland", "iceland"}, {"iceland", "ísland"}, {"iceland", "冰岛"},
    {"iceland", "reykjavik"}, {"iceland", "reykjavík"},
    {"norway", "norway"}, {"norway", "norge"}, {"norway", "挪威"},
    {"norway", "lofoten"}, {"norway", "罗弗敦"}, {"norway", "troms"},
    {"norway", "特罗姆瑟"}, {"norway", "nordland"},
    {"morocco", "morocco"}, {"morocco", "maroc"}, {"morocco", "摩洛哥"},
    {"morocco", "marrakech"}, {"morocco", "马拉喀什"}, {"morocco", "sahara"},
    {"morocco", "撒哈拉"},
    {"namibia", "namibia"}, {"namibia", "纳米比亚"}, {"namibia", "sossusvlei"},
    {"namibia", "deadvlei"}, {"namibia", "namib"},
    {"china", "china"}, {"china", "中国"}, {"china", "cn"}, {"china", "hunan"},
    {"china", "湖南"}, {"china", "anhui"}, {"china", "安徽"}, {"china", "sichuan"},
    {"china", "四川"}, {"china", "yunnan"}, {"china", "云南"}, {"china", "guangxi"},
    {"china", "广西"}, {"china", "xinjiang"}, {"china", "新疆"}, {"china", "tibet"},
    {"china", "西藏"}, {"china", "zhangjiajie"}, {"china", "张家界"},
    {"china", "guilin"}, {"china", "桂林"},
    {"japan", "japan"}, {"japan", "日本"}, {"japan", "nippon"}, {"japan", "kyoto"},
    {"japan", "京都"}, {"japan", "hokkaido"}, {"japan", "北海道"},
    {"japan", "tokyo"}, {"japan", "东京"},
    {"newzealand", "new zealand"}, {"newzealand", "newzealand"},
    {"newzealand", "nz"}, {"newzealand", "新西兰"},
    {"newzealand", "canterbury"}, {"newzealand", "otago"},
    {"usa", "usa"}, {"usa", "united states"}, {"usa", "america"}, {"usa", "美国"},
    {"usa", "arizona"}, {"usa", "亚利桑那"}, {"usa", "utah"}, {"usa", "犹他"},
    {"peru", "peru"}, {"peru", "秘鲁"}, {"peru", "cusco"}, {"peru", "库斯科"},
    {"peru", "puno"},
    {"bolivia", "bolivia"}, {"bolivia", "玻利维亚"}, {"bolivia", "potosi"},
    {"bolivia", "波托西"}, {"bolivia", "uyuni"}, {"bolivia", "乌尤尼"},
};

// SERIES_ALIAS（key = 系列键，value = 别名）
const Alias kSeriesAlias[] = {
    {"蓝调时刻", "blue"}, {"蓝调时刻", "blues"}, {"蓝调时刻", "蓝调"},
    {"蓝调时刻", "twilight"}, {"蓝调时刻", "暮色"}, {"蓝调时刻", "dusk"},
    {"蓝调时刻", "夜幕"},
    {"黄金时刻", "golden"}, {"黄金时刻", "黄金"}, {"黄金时刻", "sunrise"},
    {"黄金时刻", "sunset"}, {"黄金时刻", "日出"}, {"黄金时刻", "日落"},
    {"黄金时刻", "晨光"}, {"黄金时刻", "暮光"}, {"黄金时刻", "晨曦"},
    {"星汉灿烂", "astro"}, {"星汉灿烂", "star"}, {"星汉灿烂", "stars"},
    {"星汉灿烂", "milky"}, {"星汉灿烂", "银河"}, {"星汉灿烂", "星空"},
    {"星汉灿烂", "星轨"}, {"星汉灿烂", "aurora"}, {"星汉灿烂", "极光"},
    {"星汉灿烂", "night"}, {"星汉灿烂", "夜景"},
    {"日月交辉", "sun"}, {"日月交辉", "moon"}, {"日月交辉", "日月"},
    {"日月交辉", "日月交辉"}, {"日月交辉", "moonrise"}, {"日月交辉", "月出"},
    {"日月交辉", "eclipse"}, {"日月交辉", "日食"}, {"日月交辉", "月食"},
};

// PROV_ALIAS（key = 别名，value = 规范英文键；顺序 = Python 字典序）
const Alias kProvChina[] = {
    {"湖南", "Hunan Province"}, {"安徽", "Anhui Province"}, {"四川", "Sichuan Province"},
    {"云南", "Yunnan Province"}, {"广西", "Guangxi Zhuang Autonomous Region"},
    {"新疆", "Xinjiang Uygur Autonomous Region"}, {"西藏", "Tibet Autonomous Region"},
    {"北京", "Beijing Municipality"}, {"上海", "Shanghai Municipality"},
    {"广东", "Guangdong Province"}, {"浙江", "Zhejiang Province"},
    {"江苏", "Jiangsu Province"}, {"福建", "Fujian Province"},
    {"山东", "Shandong Province"}, {"湖北", "Hubei Province"},
    {"陕西", "Shaanxi Province"}, {"甘肃", "Gansu Province"},
    {"青海", "Qinghai Province"}, {"内蒙古", "Inner Mongolia Autonomous Region"},
    {"黑龙江", "Heilongjiang Province"}, {"吉林", "Jilin Province"},
    {"辽宁", "Liaoning Province"}, {"河北", "Hebei Province"},
    {"河南", "Henan Province"}, {"山西", "Shanxi Province"},
    {"江西", "Jiangxi Province"}, {"贵州", "Guizhou Province"},
    {"海南", "Hainan Province"}, {"台湾", "Taiwan Province"},
    {"香港", "Hong Kong S.A.R."}, {"澳门", "Macau S.A.R."},
    {"重庆", "Chongqing Municipality"}, {"天津", "Tianjin Municipality"},
    {"宁夏", "Ningxia Hui Autonomous Region"},
    {"hunan", "Hunan Province"}, {"anhui", "Anhui Province"},
    {"sichuan", "Sichuan Province"}, {"yunnan", "Yunnan Province"},
    {"guangxi", "Guangxi Zhuang Autonomous Region"},
    {"xinjiang", "Xinjiang Uygur Autonomous Region"},
    {"tibet", "Tibet Autonomous Region"}, {"beijing", "Beijing Municipality"},
    {"shanghai", "Shanghai Municipality"},
};
const Alias kProvUsa[] = {
    {"arizona", "Arizona"}, {"utah", "Utah"}, {"california", "California"},
    {"nevada", "Nevada"}, {"colorado", "Colorado"}, {"wyoming", "Wyoming"},
    {"alaska", "Alaska"}, {"hawaii", "Hawaii"}, {"new mexico", "New Mexico"},
    {"texas", "Texas"}, {"oregon", "Oregon"}, {"montana", "Montana"},
    {"idaho", "Idaho"}, {"washington", "Washington"},
};
const Alias kProvJapan[] = {
    {"kyoto", "Kyoto"}, {"京都", "Kyoto"}, {"hokkaido", "Hokkaido"},
    {"北海道", "Hokkaido"}, {"tokyo", "Tokyo"}, {"东京", "Tokyo"},
    {"osaka", "Osaka"}, {"nagano", "Nagano"},
};
const Alias kProvNz[] = {
    {"canterbury", "Canterbury"}, {"otago", "Otago"},
    {"west coast", "West Coast"}, {"auckland", "Auckland"},
    {"wellington", "Wellington"},
};
const Alias kProvNorway[] = {
    {"nordland", "Nordland"}, {"诺尔兰", "Nordland"}, {"lofoten", "Nordland"},
    {"罗弗敦", "Nordland"}, {"troms", "Troms og Finnmark"},
    {"finnmark", "Troms og Finnmark"}, {"特罗姆瑟", "Troms og Finnmark"},
    {"oslo", "Oslo"}, {"vestland", "Vestland"}, {"rogaland", "Rogaland"},
    {"agder", "Agder"}, {"innlandet", "Innlandet"}, {"viken", "Viken"},
    {"trøndelag", "Trøndelag"}, {"trondelag", "Trøndelag"},
};
const Alias kProvIceland[] = {
    {"capital region", "Capital Region"}, {"reykjavik", "Capital Region"},
    {"雷克雅未克", "Capital Region"}, {"首都", "Capital Region"},
    {"southern region", "Southern Region"}, {"vik", "Southern Region"},
    {"维克", "Southern Region"}, {"southern peninsula", "Southern Peninsula"},
    {"western region", "Western Region"}, {"westfjords", "Westfjords"},
    {"西峡湾", "Westfjords"},
    {"northwestern region", "Northwestern Region"},
    {"northeastern region", "Northeastern Region"},
    {"eastern region", "Eastern Region"},
};
const Alias kProvMorocco[] = {
    {"marrakech", "Marrakech-Safi"}, {"marrakesh", "Marrakech-Safi"},
    {"马拉喀什", "Marrakech-Safi"}, {"casablanca", "Casablanca-Settat"},
    {"rabat", "Rabat-Salé-Kenitra"}, {"fez", "Fez-Meknes"},
    {"meknes", "Fez-Meknes"}, {"非斯", "Fez-Meknes"},
    {"tangier", "Tangier-Tetouan-Al Hoceima"}, {"sahara", "Drâa-Tafilalet"},
    {"merzouga", "Drâa-Tafilalet"}, {"撒哈拉", "Drâa-Tafilalet"},
    {"souss", "Souss-Massa"}, {"agadir", "Souss-Massa"},
};
const Alias kProvNamibia[] = {
    {"khomas", "Khomas"}, {"windhoek", "Khomas"}, {"温得和克", "Khomas"},
    {"hardap", "Hardap"}, {"karas", "Karas"}, {"erongo", "Erongo"},
    {"swakopmund", "Erongo"}, {"kunene", "Kunene"},
    {"otjozondjupa", "Otjozondjupa"}, {"omaheke", "Omaheke"},
    {"sossusvlei", "Hardap"}, {"deadvlei", "Hardap"}, {"namib", "Hardap"},
};
const Alias kProvPeru[] = {
    {"cusco", "Cusco"}, {"cuzco", "Cusco"}, {"库斯科", "Cusco"},
    {"puno", "Puno"}, {"普诺", "Puno"}, {"lima", "Lima"},
    {"arequipa", "Arequipa"}, {"ancash", "Ancash"}, {"huaraz", "Ancash"},
    {"ica", "Ica"}, {"junin", "Junín"}, {"junín", "Junín"},
};
const Alias kProvBolivia[] = {
    {"potosi", "Potosí"}, {"potosí", "Potosí"}, {"波托西", "Potosí"},
    {"oruro", "Oruro"}, {"la paz", "La Paz"}, {"lapaz", "La Paz"},
    {"santa cruz", "Santa Cruz"}, {"cochabamba", "Cochabamba"},
    {"tarija", "Tarija"}, {"uyuni", "Potosí"}, {"乌尤尼", "Potosí"},
};

struct ProvTable { const char* region; const Alias* items; int n; };
const ProvTable kProvTables[] = {
    {"china", kProvChina, int(sizeof(kProvChina) / sizeof(Alias))},
    {"usa", kProvUsa, int(sizeof(kProvUsa) / sizeof(Alias))},
    {"japan", kProvJapan, int(sizeof(kProvJapan) / sizeof(Alias))},
    {"newzealand", kProvNz, int(sizeof(kProvNz) / sizeof(Alias))},
    {"norway", kProvNorway, int(sizeof(kProvNorway) / sizeof(Alias))},
    {"iceland", kProvIceland, int(sizeof(kProvIceland) / sizeof(Alias))},
    {"morocco", kProvMorocco, int(sizeof(kProvMorocco) / sizeof(Alias))},
    {"namibia", kProvNamibia, int(sizeof(kProvNamibia) / sizeof(Alias))},
    {"peru", kProvPeru, int(sizeof(kProvPeru) / sizeof(Alias))},
    {"bolivia", kProvBolivia, int(sizeof(kProvBolivia) / sizeof(Alias))},
};

const char* const kCityStop[] = {
    "raw", "jpg", "jpeg", "edited", "导出", "精选", "selected", "原片", "成片", "原图",
};

bool isCityStop(const QString& s)
{
    const QString low = s.toLower();
    for (const char* x : kCityStop)
        if (low == QString::fromUtf8(x)) return true;
    return false;
}

const QRegularExpression& datePat(int i)
{
    // 与 Python _DATE_PAT 两条一致（含顺序）
    static const QRegularExpression p0(
        QStringLiteral("(20\\d{2})[-_.]?(\\d{2})[-_.]?(\\d{2})"));
    static const QRegularExpression p1(
        QStringLiteral("(20\\d{2})[-_.]?(1[0-2]|0[1-9])[-_.]?(3[01]|[12]\\d|0[1-9])"));
    return i == 0 ? p0 : p1;
}

// title_from_name：去扩展名 → 去日期前缀 → 去 IMG/DSC 器械前缀 → 符号转空格
QString titleFromName(const QString& name)
{
    const int dot = int(name.lastIndexOf(QLatin1Char('.')));
    QString base = (dot > 0) ? name.left(dot) : name;
    base.remove(QRegularExpression(
        QStringLiteral("^20\\d{2}[-_.]?\\d{2}[-_.]?\\d{2}[-_.\\s]*")));
    base.remove(QRegularExpression(
        QStringLiteral("^(IMG|DSC|DSC_|IMG_|P|_DSC)\\d+[-_\\s]*"),
        QRegularExpression::CaseInsensitiveOption));
    base.replace(QLatin1Char('_'), QLatin1Char(' '))
        .replace(QLatin1Char('-'), QLatin1Char(' '));
    base = base.trimmed();
    if (base.isEmpty())
        base = (dot > 0) ? name.left(dot) : name;
    return base;
}

QString guessDate(const QString& name, const QStringList& dirs)
{
    const QString dirBlob = dirs.join(QLatin1Char(' '));
    for (const QString& src : { name, dirBlob }) {
        for (int pi = 0; pi < 2; ++pi) {
            const QRegularExpressionMatch m = datePat(pi).match(src);
            if (!m.hasMatch()) continue;
            const int mo = m.captured(2).toInt();
            const int d = m.captured(3).toInt();
            if (mo >= 1 && mo <= 12 && d >= 1 && d <= 31)
                return QStringLiteral("%1-%2-%3")
                    .arg(m.captured(1),
                         QString::number(mo).rightJustified(2, QLatin1Char('0')),
                         QString::number(d).rightJustified(2, QLatin1Char('0')));
        }
    }
    return QString();
}

} // namespace

namespace PathGuess {

bool isSkipDir(const QString& name)
{
    const QString low = name.toLower();
    for (const char* s : kSkip)
        if (low == QString::fromUtf8(s)) return true;
    return false;
}

bool isPhoto(const QString& fileName)
{
    const int dot = int(fileName.lastIndexOf(QLatin1Char('.')));
    if (dot <= 0) return false;
    const QString stem = fileName.left(dot).toLower();
    if (stem.endsWith(QString::fromUtf8("_retouched"))) return false;
    const QString ext = fileName.mid(dot).toLower();
    for (const char* e : kImgExt)
        if (ext == QString::fromUtf8(e)) return true;
    return false;
}

PathInfo guessFromPath(const QString& rel)
{
    PathInfo out;
    const QStringList parts = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return out;
    const QString name = parts.last();
    const QStringList dirs = parts.mid(0, parts.size() - 1);

    // ---- 地区：joinedLow = ' / '.join(lower(parts))；别名子串命中即定
    QString joinedLow;
    for (int i = 0; i < parts.size(); ++i) {
        if (i) joinedLow += QString::fromUtf8(" / ");
        joinedLow += parts[i].toLower();
    }
    constexpr int kRegionN = int(sizeof(kRegionAlias) / sizeof(Alias));
    QString rid;
    for (int i = 0; i < kRegionN && rid.isEmpty();) {
        const QString key = QString::fromUtf8(kRegionAlias[i].key);
        int j = i;
        for (; j < kRegionN && QString::fromUtf8(kRegionAlias[j].key) == key; ++j) {
            if (joinedLow.contains(QString::fromUtf8(kRegionAlias[j].value),
                                   Qt::CaseInsensitive)) {
                rid = key;
                break;
            }
        }
        i = (j > i) ? j : i + 1;
    }
    out.r = rid;   // 空则无地区；系列/日期/标题仍无条件计算（Python guess_from_path 同款）

    // ---- 省份：先整段精确匹配，再最长别名子串兜底
    const ProvTable* tbl = nullptr;
    for (const ProvTable& t : kProvTables)
        if (rid == QString::fromUtf8(t.region)) { tbl = &t; break; }
    if (tbl) {
        for (const QString& p : parts) {
            for (int i = 0; i < tbl->n; ++i) {
                const QString a = QString::fromUtf8(tbl->items[i].key);
                if (p == a || p.toLower() == a) {
                    out.pv = QString::fromUtf8(tbl->items[i].value);
                    break;
                }
            }
            if (!out.pv.isEmpty()) break;
        }
        if (out.pv.isEmpty()) {
            QString best;
            for (int i = 0; i < tbl->n; ++i) {
                const QString a = QString::fromUtf8(tbl->items[i].key);
                if (joinedLow.contains(a, Qt::CaseInsensitive) && a.size() > best.size()) {
                    out.pv = QString::fromUtf8(tbl->items[i].value);
                    best = a;
                }
            }
        }
    }

    // ---- 系列：blob = name + ' ' + dirs；最长别名命中
    const QString blob = (name + QLatin1Char(' ') + dirs.join(QLatin1Char(' '))).toLower();
    constexpr int kSeriesN = int(sizeof(kSeriesAlias) / sizeof(Alias));
    int bestLen = 0;
    for (int i = 0; i < kSeriesN; ++i) {
        const QString a = QString::fromUtf8(kSeriesAlias[i].value);
        if (blob.contains(a, Qt::CaseInsensitive) && a.size() > bestLen) {
            out.s = QString::fromUtf8(kSeriesAlias[i].key);
            bestLen = a.size();
        }
    }

    // ---- 日期
    out.d = guessDate(name, dirs);

    // ---- 城市：省目录的下一层（跳过工作目录名；仍映射到同一省则不取）
    if (!out.pv.isEmpty() && tbl) {
        for (int i = 0; i < dirs.size(); ++i) {
            bool provDir = false;
            for (int k = 0; k < tbl->n && !provDir; ++k) {
                const QString a = QString::fromUtf8(tbl->items[k].key);
                if (QString::fromUtf8(tbl->items[k].value) == out.pv
                    && (dirs[i] == a || dirs[i].toLower() == a))
                    provDir = true;
            }
            if (!provDir) continue;
            int j = i + 1;
            while (j < dirs.size() && isCityStop(dirs[j])) ++j;
            if (j < dirs.size()) {
                bool samePv = false;
                for (int k = 0; k < tbl->n && !samePv; ++k) {
                    const QString a = QString::fromUtf8(tbl->items[k].key);
                    if (QString::fromUtf8(tbl->items[k].value) == out.pv
                        && (dirs[j] == a || dirs[j].toLower() == a))
                        samePv = true;
                }
                if (!samePv) out.ct = dirs[j];
            }
            break;
        }
    }

    out.t = titleFromName(name);
    return out;
}

} // namespace PathGuess
