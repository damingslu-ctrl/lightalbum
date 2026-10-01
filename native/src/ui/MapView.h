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

signals:
    void breadcrumbChanged(const QString& text);
    void picked(const QString& key, const QString& label, bool isProvince); // 区域/省点击

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

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
    QVector<Ring>       worldRings_;
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
};
