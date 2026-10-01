#pragma once
#include <QWidget>
#include <QPixmap>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QPointF>
#include <QHash>
#include <QColor>
#include <QPainter>

// 预投影坐标系：所有环/省/经纬都落在 [0,MAP_W]x[0,MAP_H] 的等距圆柱平面
struct Ring { QVector<QPointF> pts; };

// 高精度世界要素：外环 + 湖泊内环（OddEven 镂空）；来自 Natural Earth 50m
struct WorldFeature {
    QVector<QPointF> exterior;
    QVector<QVector<QPointF>> holes;
};

struct RegionInfo {
    QString id;
    QString name;
    double lon = 0, lat = 0;
    QPointF  proj;            // eqX/eqY 投到预投影空间
};

struct ProvinceInfo {
    QString name;            // 英文原名
    Ring    ring;
    QPointF ctr;             // cx,cy（预投影）
    QString zh;              // 中文名（provZh 命中）
    QRectF  bbox;            // 预投影包围盒
};

class MapView : public QWidget {
    Q_OBJECT
public:
    enum Level { World, Region, Province };

    static constexpr double MAP_W = 1000.0, MAP_H = 500.0;
    explicit MapView(QWidget* parent = nullptr);

    // 导航 API（由上层面包屑/返回按钮调用）
    void enterRegion(const QString& id);
    void enterProvince(const QString& name);
    void enterCountry(int featureIndex);   // 无专辑国家 → 国家视图（国界轮廓 + 该国 GPS 作品）
    void goUp();
    Level level() const { return level_; }
    QString breadcrumb() const;

    // P16.1 数据诚实：注入真实 GPS 照片数（尊重隐私门由调用方计算），
    // 取代 paintEvent 中原硬编码的「足迹 0」。
    void setGpsCount(int n) { gpsCount_ = n < 0 ? 0 : n; update(); }

    // P16.4 足迹点：注入真实照片 GPS 坐标（预投影空间，已在调用方按隐私门过滤），
    // 在世界/大区/省三级以翡翠光点呈现你的实际拍摄足迹。
    void setGpsPoints(const QVector<QPointF>& pts);

    // P16.4.2 金色航线基点（「我的位置」，经纬度，默认上海）。设置后重新烘焙世界层。
    void setHomePoint(double lon, double lat);
    double homeLon() const { return homeLon_; }
    double homeLat() const { return homeLat_; }
    // P16.4.2 有作品的大区 id 列表：其中心点也作为航线目标（与 web 版「连向每一处拍摄地」一致）
    void setArcRegionIds(const QStringList& ids);

    // A 现状墨韵·抛光：像素自测探针（offscreen 渲染世界层 + 翡翠像素统计）
    void qaShootA(const QString& path);
    // A 下钻区/省视图像素自测：强制 enterRegion(id) 后离屏渲染该大区省视图 + 省面像素统计
    void qaShootRegion(const QString& path, const QString& id);
    // A 点击链路自测：合成鼠标事件实测「世界锚点→大区」「大区省面→省」两级命中
    void qaClickProbe();
    // A 国家视图像素自测：按国家名 enterCountry → 离屏渲染国界轮廓 + 分色统计
    void qaShootCountry(const QString& path, const QString& name);
    // A 爆炸弹层像素自测：进入省视图 + 合成缩略图 → 离屏渲染终态 + 分色统计
    void qaShootBurst(const QString& path);

    // 省视图「爆炸弹层」：MapPage 注入该省照片缩略图后，从省中心爆炸弹出
    // 金色连线 + 圆角照片卡（冲击波双环 + 逐张错峰飞出）。reduceMotion 直接终态。
    // paths 与 thumbs 一一对应（点击卡片 → openPhotoRequested 打开灯箱）。
    void startBurst(const QVector<QPixmap>& thumbs, const QStringList& paths);
    bool burstActive() const { return burstActive_; }

    // 国家视图（无省级数据的国家）：世界层点任意国家即下钻到国界轮廓 +
    // 该国 GPS 足迹/航线；点国内触发爆炸弹层（该国 GPS 作品）
    void setGpsPaths(const QStringList& paths) { gpsPaths_ = paths; }

signals:
    void breadcrumbChanged(const QString& text);
    void picked(const QString& key, const QString& label, bool isProvince); // 区域/省点击
    // 省视图点击 → 请求爆炸弹层（MapPage 按省收集缩略图后回灌 startBurst）
    void burstRequested(const QString& key, const QString& label);
    // 爆炸卡片点击 → 打开该照片灯箱
    void openPhotoRequested(const QString& path);
    // 进入国家视图（MapPage 更新统计：该国 GPS 作品数）
    void countryPicked(const QString& name, const QStringList& paths);
    // 国家视图点击国内 → 请求该国爆炸弹层
    void countryBurstRequested(const QString& name, const QStringList& paths);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;   // 世界层滚轮缩放（光标锚定）

private:
    void loadData();
    void buildWorldPixmap();                 // 离屏渲染静态世界（环填充）
    void renderWorldInto(QPainter& p, const QRectF& dst) const;
    void renderRegion(QPainter& p, const QRectF& dst) const;
    void renderProvince(QPainter& p, const QRectF& dst) const;
    // P16.4 在大区/省视图按拟合变换投影真实 GPS 足迹点（裁剪到 dst）
    void drawGpsDots(QPainter& p, const QRectF& bbox, const QRectF& dst) const;
    // P16.4.2 金色航线：以基点连向每处 GPS 足迹（二次贝塞尔向上拱起，短路径过接缝）
    void drawArcs(QPainter& p, const QRectF& bbox, const QRectF& dst) const;
    // P16.4.2 航线目标集 = GPS 足迹点 ∪ 有作品大区的中心点（预投影空间）
    QVector<QPointF> arcTargets() const;

    QPointF preToWidget(const QPointF& pre, const QRectF& dst, double scale,
                         double offX = 0) const;
    double fitScale(const QRectF& bbox, const QRectF& dst) const;
    QRectF fitRect(const QRectF& bbox, const QRectF& dst, double& outScale) const;
    void computeRegionBBox();
    void computeProvinceBBox(ProvinceInfo& pv);
    QString zhOf(const QString& en) const;

    // 足迹地图「很精细」优化（墨韵·抛光 2）
    void renderFootDotSprite();                 // 预渲染翡翠足迹点精灵（blit，避免逐帧梯度）
    void drawFootprintDot(QPainter& p, const QPointF& c, double coreR, int dens) const;
    QVector<int> computeDensity() const;        // 局部密度（网格法 O(n)）
    void drawGraticule(QPainter& p, const QRectF& bbox, const QRectF& dst) const; // 经纬网格
    void drawLabel(QPainter& p, const QPointF& pos, const QString& text, const QColor& fill) const; // 描边 halo
    void drawLegend(QPainter& p) const;          // 世界视图图例

    // 数据
    static constexpr double DRIFT_SPEED = 4.5;   // px/s（预投影空间）
    QVector<WorldFeature> worldFeatures_;     // 高精度世界（Natural Earth 50m）
    QVector<RegionInfo> regions_;
    QJsonObject         provJson_;              // region -> [ [name,ring,cx,cy], ... ]
    QJsonObject         provZh_;
    QVector<ProvinceInfo> curProvinces_;        // 当前大区的省
    QString             curRegionId_;
    QString             curRegionName_;
    ProvinceInfo        curProvince_;
    bool                hasCurProvince_ = false;

    Level               level_ = World;
    QPixmap             worldPix_;              // 离屏静态世界
    double              driftPx_ = 0;
    QElapsedTimer       timer_;
    QTimer              driftTimer_;
    bool                reduceMotion_ = false;
    QString             hoverHint_;            // 鼠标悬停提示（省名）
    int                 gpsCount_ = 0;         // P16.1 真实 GPS 足迹数（含隐私门下计数）
    QVector<QPointF>    gpsPts_;               // P16.4 真实照片 GPS 足迹点（预投影空间）
    double              homeLon_ = 121.47;     // P16.4.2 航线基点（默认上海，经纬度）
    double              homeLat_ = 31.23;
    QStringList         arcRegionIds_;         // P16.4.2 有作品的大区（中心点为航线目标）

    // 很精细优化：状态与缓存
    double              arcDashOffset_ = 0;     // 流动航线虚线相位
    QPixmap             footDotPix_;            // 翡翠足迹点精灵缓存

    // 世界层国家轮廓命中：锚点落入其中的要素 → 大区下钻（点国家任意处即下钻）
    void assignFeatureRegions();                 // loadData 时一次性标记 + 预计算 bbox
    QVector<int>    featureRegion_;              // worldFeatures_ → regions_ 下标（-1=无作品国家）
    QVector<QRectF> featureBbox_;                // 世界要素 bbox（命中早退）
    QVector<QString> worldNames_;                // 世界要素国家名（与 worldFeatures_ 对齐）

    // 国家视图状态（level_==Region 且 curProvinces_ 为空时生效）
    void renderCountryOnly(QPainter& p, const QRectF& dst) const;
    int         countryFeature_ = -1;            // 当前国家视图对应的世界要素
    QStringList countryPaths_;                   // 该国 GPS 照片路径（enterCountry 时算好）
    QVector<QPointF> countryPts_;                // 与 countryPaths_ 对应的预投影点（点击命中用）
    QStringList gpsPaths_;                       // 与 gpsPts_ 一一对应的照片路径
    void focusGeometry(QRectF& bbox, QPointF& ctr) const;   // 爆炸焦点（省=省bbox/中心，国家=国bbox/中心）

    // 世界层滚轮缩放（光标锚定）+ 初始居中中国
    void    centerWorldOn(double preX, double preY);  // 把预投影点置于视口中心（含漂移/纵向偏移）
    double  worldOffYClamped() const;                 // 纵向偏移按当前 zoom 夹取
    double  worldZoom_  = 1.0;                        // 缩放倍率 [1,4]
    double  worldOffY_  = 0.0;                        // 纵向视口偏移（预投影×scale 像素）
    bool    worldInited_ = false;                     // 首帧已做「中国居中」初始化

    // 世界层拖拽平移：按下空白/海洋进入待命，拖动平移，原地松开=点空白（返回上一级）
    bool    panning_   = false;
    bool    panMoved_  = false;
    QPointF panStart_;
    QPointF panLast_;

    // 省视图爆炸弹层状态
    void drawBurst(QPainter& p, const QRectF& dst) const;
    QRectF burstCardRect(int i, const QRectF& dst) const;   // 稳态卡片矩形（绘制/命中共用）
    QVector<QPixmap> burstThumbs_;               // MapPage 注入的照片缩略图（≤10）
    QStringList      burstPaths_;                // 与 thumbs 一一对应的照片路径
    bool             burstActive_ = false;
    QElapsedTimer    burstClock_;
    QTimer           burstTimer_;                // 动画期 16ms 驱动 update，终态后自停
    qint64           burstTOverride_ = -1;       // QA：>=0 时强制动画时间（终态渲染用）
};
