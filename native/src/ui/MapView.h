#pragma once
#include <QWidget>
#include <QPixmap>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QString>
#include <QPointF>

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

    QPointF preToWidget(const QPointF& pre, const QRectF& dst, double scale,
                         double offX = 0) const;
    double fitScale(const QRectF& bbox, const QRectF& dst) const;
    QRectF fitRect(const QRectF& bbox, const QRectF& dst, double& outScale) const;
    void computeRegionBBox();
    void computeProvinceBBox(ProvinceInfo& pv);
    QString zhOf(const QString& en) const;

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
};
