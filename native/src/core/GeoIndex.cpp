// P10 离线逆地理编码实现 —— 数据：exe 内嵌 :/data/mapdata.json（P2 同源）
#include "GeoIndex.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cmath>

namespace {
constexpr double kPxPerDeg = 1000.0 / 360.0;   // x/y 同尺度（2.78 px/°）
constexpr double kCityDegProv = 1.6;           // 救 region/prov 的城市距离上限
constexpr double kCityDegName = 1.0;           // 给 ct（城市点）的距离上限

double toPxX(double lon) { return (lon + 180.0) / 360.0 * 1000.0; }
double toPxY(double lat) { return (90.0 - lat) / 180.0 * 500.0; }

// 射线法（pts 扁平 [x0,y0,...]）
bool pip(double px, double py, const QVector<double>& pts)
{
    const int n = pts.size() / 2;
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const double xi = pts[2 * i], yi = pts[2 * i + 1];
        const double xj = pts[2 * j], yj = pts[2 * j + 1];
        if ((yi > py) != (yj > py)
            && px < (xj - xi) * (py - yi) / (yj - yi) + xi)
            inside = !inside;
    }
    return inside;
}
} // namespace

GeoIndex& GeoIndex::instance()
{
    static GeoIndex g;
    return g;
}

bool GeoIndex::ensureLoaded()
{
    if (loaded_) return !rings_.isEmpty();
    loaded_ = true;
    QFile f(QStringLiteral(":/data/mapdata.json"));
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject provs = root.value(QStringLiteral("provinces")).toObject();
    for (QJsonObject::const_iterator it = provs.constBegin(); it != provs.constEnd(); ++it) {
        const QString rid = it.key();
        const QJsonArray plist = it.value().toArray();
        for (const QJsonValue& pv : plist) {
            const QJsonArray item = pv.toArray();
            const QString key = item.at(0).toString();
            const QJsonArray ringsArr = item.at(1).toArray();
            for (const QJsonValue& rv : ringsArr) {
                const QJsonArray a = rv.toArray();
                if (a.size() < 8) continue;
                Ring r;
                r.region = rid;
                r.prov = key;
                r.pts.reserve(a.size());
                for (int i = 0; i < a.size(); i += 2) {
                    const double x = a.at(i).toDouble(), y = a.at(i + 1).toDouble();
                    r.pts.append(x); r.pts.append(y);
                    r.x0 = std::min(r.x0, x); r.x1 = std::max(r.x1, x);
                    r.y0 = std::min(r.y0, y); r.y1 = std::max(r.y1, y);
                }
                rings_.push_back(std::move(r));
            }
        }
    }
    // 城市表（_gen_geo_cities.py 对齐数据集生成；手工覆盖含沿海城市）
    static const struct { const char* name; const char* region; const char* prov;
                          double lon, lat; } kCities[] = {
// 由 _gen_geo_cities.py 生成：城市 → region/省键/经纬度（数据集 mapdata.json 对齐）
    {"北京", "china", "Beijing Municipality", 116.41, 39.90},
    {"上海", "china", "Shanghai Municipality", 121.47, 31.23},
    {"广州", "china", "Guangzhou Province", 113.26, 23.13},
    {"深圳", "china", "Guangdong Province", 114.06, 22.55},
    {"成都", "china", "Sichuan Province", 104.07, 30.57},
    {"昆明", "china", "Yunnan Province", 102.83, 24.88},
    {"杭州", "china", "Zhejiang Province", 120.15, 30.27},
    {"西安", "china", "Shaanxi Province", 108.94, 34.34},
    {"武汉", "china", "Hubei Province", 114.31, 30.59},
    {"长沙", "china", "Hunan Province", 112.94, 28.23},
    {"乌鲁木齐", "china", "Xinjiang Uyghur Autonomous Region", 87.62, 43.83},
    {"拉萨", "china", "Tibet Autonomous Region", 91.11, 29.66},
    {"哈尔滨", "china", "Heilongjiang Province", 126.53, 45.80},
    {"大连", "china", "Liaoning Province", 121.61, 38.91},
    {"厦门", "china", "Fujian Province", 118.09, 24.48},
    {"桂林", "china", "Guangxi Zhuang Autonomous Region", 110.29, 25.27},
    {"张家界", "china", "Hunan Province", 110.48, 29.13},
    {"西宁", "china", "Qinghai Province", 101.78, 36.62},
    {"兰州", "china", "Gansu Province", 103.83, 36.06},
    {"贵阳", "china", "Guizhou Province", 106.63, 26.65},
    {"南京", "china", "Jiangsu Province", 118.80, 32.06},
    {"青岛", "china", "Shandong Province", 120.38, 36.07},
    {"沈阳", "china", "Liaoning Province", 123.43, 41.80},
    {"长春", "china", "Jilin Province", 125.32, 43.90},
    {"呼和浩特", "china", "Inner Mongolia Autonomous Region", 111.75, 40.84},
    {"南宁", "china", "Guangxi Zhuang Autonomous Region", 108.37, 22.82},
    {"海口", "china", "Hainan Province", 110.20, 20.04},
    {"台北", "china", "Taiwan Province", 121.56, 25.03},
    {"香港", "china", "Hong Kong Special Administrative Region", 114.17, 22.32},
    {"澳门", "china", "Macau Special Administrative Region", 113.55, 22.19},
    {"大理", "china", "Yunnan Province", 100.27, 25.61},
    {"丽江", "china", "Yunnan Province", 100.23, 26.86},
    {"遵义", "china", "Guizhou Province", 106.93, 27.73},
    {"纽约", "usa", "New York", -74.01, 40.71},
    {"洛杉矶", "usa", "California", -118.24, 34.05},
    {"旧金山", "usa", "California", -122.42, 37.77},
    {"西雅图", "usa", "Washington", -122.33, 47.61},
    {"拉斯维加斯", "usa", "Nevada", -115.14, 36.17},
    {"凤凰城", "usa", "Arizona", -112.07, 33.45},
    {"丹佛", "usa", "Colorado", -104.99, 39.74},
    {"芝加哥", "usa", "Illinois", -87.63, 41.88},
    {"迈阿密", "usa", "Florida", -80.19, 25.76},
    {"安克雷奇", "usa", "Alaska", -149.90, 61.22},
    {"火奴鲁鲁", "usa", "Hawaii", -157.86, 21.31},
    {"盐湖城", "usa", "Utah", -111.89, 40.76},
    {"东京", "japan", "Tokyo", 139.69, 35.69},
    {"京都", "japan", "Kyoto Prefecture", 135.77, 35.01},
    {"大阪", "japan", "Osaka Prefecture", 135.50, 34.69},
    {"札幌", "japan", "Hokkaido", 141.35, 43.06},
    {"名古屋", "japan", "Aichi Prefecture", 136.91, 35.18},
    {"福冈", "japan", "Fukuoka Prefecture", 130.40, 33.59},
    {"那霸", "japan", "Okinawa Prefecture", 127.68, 26.21},
    {"奥斯陆", "norway", "Oslo", 10.75, 59.91},
    {"卑尔根", "norway", "Vestland", 5.32, 60.39},
    {"特罗姆瑟", "norway", "Troms og Finnmark", 18.96, 69.65},
    {"雷讷", "norway", "Nordland", 10.89, 67.93},
    {"斯沃尔维尔", "norway", "Nordland", 14.08, 68.23},
    {"雷克雅未克", "iceland", "Capital Region", -21.94, 64.15},
    {"维克", "iceland", "Southern Region", -19.01, 63.42},
    {"阿克雷里", "iceland", "Northeastern Region", -18.09, 65.68},
    {"伊萨菲厄泽", "iceland", "Westfjords", -23.54, 65.92},
    {"赫本", "iceland", "Eastern Region", -14.43, 64.25},
    {"奥克兰", "newzealand", "Auckland Region", 174.76, -36.85},
    {"惠灵顿", "newzealand", "Wellington Region", 174.78, -41.29},
    {"基督城", "newzealand", "Canterbury Region", 172.64, -43.53},
    {"皇后镇", "newzealand", "Otago Region", 168.66, -45.03},
    {"但尼丁", "newzealand", "Otago Region", 170.50, -45.87},
    {"瓦纳卡", "newzealand", "Otago Region", 169.15, -44.70},
    {"马拉喀什", "morocco", "Marrakech-Safi", -7.99, 31.63},
    {"卡萨布兰卡", "morocco", "Casablanca-Settat", -7.59, 33.57},
    {"非斯", "morocco", "Fez-Meknes", -5.00, 34.03},
    {"丹吉尔", "morocco", "Tangier-Tetouan-Al Hoceima", -5.80, 35.78},
    {"瓦尔扎扎特", "morocco", "Drâa-Tafilalet", -6.91, 30.99},
    {"梅祖卡", "morocco", "Drâa-Tafilalet", -4.42, 31.08},
    {"温得和克", "namibia", "Khomas", 17.08, -22.56},
    {"鲸湾港", "namibia", "Erongo", 14.51, -22.96},
    {"斯瓦科普蒙德", "namibia", "Erongo", 14.46, -22.68},
    {"基特曼斯胡普", "namibia", "Karas", 18.26, -26.32},
    {"利马", "peru", "Municipalidad Metropolitana de Lima", -77.03, -12.05},
    {"库斯科", "peru", "Cusco", -71.97, -13.53},
    {"阿雷基帕", "peru", "Arequipa", -71.54, -16.41},
    {"普诺", "peru", "Puno", -70.03, -15.84},
    {"伊卡", "peru", "Ica", -75.73, -14.07},
    {"瓦拉斯", "peru", "Ancash", -77.53, -9.53},
    {"拉巴斯", "bolivia", "La Paz", -68.15, -16.50},
    {"苏克雷", "bolivia", "Chuquisaca", -65.26, -19.03},
    {"乌尤尼", "bolivia", "Potosí", -66.98, -20.46},
    {"波托西", "bolivia", "Potosí", -65.75, -19.58},
    {"科恰班巴", "bolivia", "Cochabamba", -66.16, -17.39},
    };
    for (const auto& c : kCities)
        cities_.append({ QString::fromUtf8(c.name), QString::fromUtf8(c.region),
                         QString::fromUtf8(c.prov), c.lon, c.lat });
    return !rings_.isEmpty();
}

GeoResult GeoIndex::resolve(double lat, double lon) const
{
    GeoResult out;
    if (rings_.isEmpty()) return out;
    const double px = toPxX(lon), py = toPxY(lat);

    // 主路径：点内省多边形 → (region, prov)
    QString rid, pv;
    for (const Ring& r : rings_) {
        if (px < r.x0 || px > r.x1 || py < r.y0 || py > r.y1) continue;
        if (pip(px, py, r.pts)) { rid = r.region; pv = r.prov; break; }
    }

    // 城市最近邻：给 ct（≤1.0°）；pip 未中时 ≤1.6° 还可救沿海缝隙的 region/prov
    QString ct, cityRid, cityPv;
    double best = 1e18;
    for (const City& c : cities_) {
        const double dx = (toPxX(c.lon) - px) / kPxPerDeg;
        const double dy = (toPxY(c.lat) - py) / kPxPerDeg;
        const double d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; ct = c.name; cityRid = c.region; cityPv = c.prov; }
    }
    const double deg = std::sqrt(best);
    if (deg <= kCityDegName) out.city = ct;

    if (!rid.isEmpty()) {
        out.region = rid;
        out.prov = pv;
    } else if (deg <= kCityDegProv) {
        out.region = cityRid;
        out.prov = cityPv;
    }
    return out;
}
