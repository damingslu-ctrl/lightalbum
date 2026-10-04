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
#include <QKeyEvent>
#include <QDate>

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
    enum FootLayer { FL_Dots = 0, FL_Track = 1, FL_Heat = 2 };

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

    // A 方向：足迹层时间维 + 三态开关（默认 FL_Dots 零回归）
    void setGpsDates(const QVector<QDate>& dates);
    void setFootLayerMode(int mode);    // 0=光点 1=轨迹 2=热力
    int  footLayerMode() const { return footLayer_; }

    // A 方向第三步：按年筛选足迹。year=0 表示全部年份。
    // 铁律：筛选在**注入点**（MapPage::computeGpsPoints）完成 —— gpsPts_/gpsDates_/
    // gpsPaths_ 三者按下标严格对齐（国家视图 gpsPaths_[i] 依赖它），故绝不能只过滤绘制。
    void setFootYearFilter(int year);
    int  footYearFilter() const { return footYearFilter_; }

    // A 方向第四步：热力色带（按密度分级）。rampId: 0=暖橙(默认,2.12 基线) 1=翡翠 2=绯红。
    // 铁律：色带定义唯一真源 = heatRamp()，绘制与门禁共用（禁在两处各写一份颜色表）。
    enum HeatRamp { HR_Warm = 0, HR_Jade = 1, HR_Crimson = 2 };
    void setHeatRamp(int rampId);
    int  heatRamp() const { return heatRamp_; }

    // A 方向第二步：轨迹播放（沿线按日期依次飞过播放头）
    void startTrackPlayback();          // 起播（已在播放则重置重来）
    void stopTrackPlayback();
    bool trackPlaying() const { return trackActive_; }
    // A 方向第二步：轨迹播放像素自测（箭头 + 播放头三时刻位置断言）
    void qaShootTrackAnim(const QString& path);
    // A 方向第三步：按年筛选自测（点数 + pts/paths/dates 下标对齐 + 幂等 + 0=全部）
    void qaShootYear(const QString& path);
    // A 方向第四步：热力色带分级自测（三档色相各存在 + 切换后像素变化）
    void qaShootHeatRamp(const QString& path);

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
    // A 帧耗时自测：LA_MAP_PERF=1 时逐层级渲染 20 帧打印平均毫秒/帧（先测再改）
    void qaPerf();
    // A 国家视图像素自测：按国家名 enterCountry → 离屏渲染国界轮廓 + 分色统计
    void qaShootCountry(const QString& path, const QString& name);
    // A 爆炸弹层像素自测：进入省视图 + 合成缩略图 → 离屏渲染终态 + 分色统计
    void qaShootBurst(const QString& path);
    // A 方向：轨迹/热力像素自测（离屏世界层 + 青/暖色像素统计）
    void qaShootTrack(const QString& path);
    void qaShootHeat(const QString& path);

    // F3 a11y·键盘可达（方向 F 第三步，2.11.0）：offscreen 门禁断言（焦点策略 + 键盘缩放）
    static int qaKeyboardCheck();

    // F 高 DPR 渲染（方向 F 第一步）：QA 钩子
    void qaBakeViewPix();                 // 强制烘焙下钻静态层（不依赖事件循环）
    int  qaViewPixDeviceWidth() const;   // viewPix_ 设备像素宽（验证 DPR 感知）

    // 省视图「爆炸弹层」：MapPage 注入该省照片缩略图后，从省中心爆炸弹出
    // 金色连线 + 圆角照片卡（冲击波双环 + 逐张错峰飞出）。reduceMotion 直接终态。
    // paths 与 thumbs 一一对应（点击卡片 → openPhotoRequested 打开灯箱）。
    void startBurst(const QVector<QPixmap>& thumbs, const QStringList& paths);
    bool burstActive() const { return burstActive_; }

    // 国家视图（无省级数据的国家）：世界层点任意国家即下钻到国界轮廓 +
    // 该国 GPS 足迹/航线；点国内触发爆炸弹层（该国 GPS 作品）
    void setGpsPaths(const QStringList& paths) { gpsPaths_ = paths; }
    void resetWorldView();   // 回到默认视角：缩放归 1 + 中国居中（顶部「⌂ 回到中国」用）

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
    void keyPressEvent(QKeyEvent*) override;  // F3 a11y：键盘导航（缩放/平移/下钻）

private:
    void paintMap(QPainter& p);   // 绘制主体（paintEvent 与帧耗时自测共用调度）
    void loadData();
    void buildWorldPixmap();                 // 离屏渲染静态世界（环填充）
    void renderWorldInto(QPainter& p, const QRectF& dst) const;
    void renderRegion(QPainter& p, const QRectF& dst) const;
    void renderRegionStatic(QPainter& p, const QRectF& dst) const;   // 静态部分（网格/省面/标签/足迹点）
    void renderProvince(QPainter& p, const QRectF& dst) const;
    void renderProvinceStatic(QPainter& p, const QRectF& dst) const; // 静态部分（轮廓/网格/标签/足迹点）
    // P16.4 在大区/省视图按拟合变换投影真实 GPS 足迹点（裁剪到 dst）
    void drawGpsDots(QPainter& p, const QRectF& bbox, const QRectF& dst) const;
    // P16.4.2 金色航线：以基点连向每处 GPS 足迹（二次贝塞尔向上拱起，短路径过接缝）
    void drawArcs(QPainter& p, const QRectF& bbox, const QRectF& dst) const;
    // P16.4.2 航线目标集 = GPS 足迹点 ∪ 有作品大区的中心点（预投影空间）
    QVector<QPointF> arcTargets() const;

    // A 方向：轨迹折线 + 热力面（复用 viewOrigin / 预投影空间，与 drawGpsDots 同源）
    QVector<int> sortedTrackIdx() const;                         // 按 gpsDates_ 升序
    void drawTrajectoryWorld(QPainter& p) const;                 // 世界层
    void drawHeatWorld(QPainter& p) const;
    void drawTrajectory(QPainter& p, const QRectF& bbox, const QRectF& dst) const;  // 下钻层
    void drawHeat(QPainter& p, const QRectF& bbox, const QRectF& dst) const;
    // A 方向第四步：热力色带唯一真源。返回 {中心色, 中段色, 边缘透明色}，
    // 颜色按密度分三档（低/中/高）。绘制与门禁共用（禁在两处各写一份颜色表）。
    struct HeatColors { QColor c0, c1, c2; };
    HeatColors heatRamp(int dens) const;
    // 选中的色带（每档三色），供门禁断言与 UI 预览共用
    HeatColors heatRampOf(int rampId, int tier) const;
    // A 方向第二步：方向箭头（静态烘焙层内，一次栅格化）+ 播放头（动态层）
    void drawTrackArrows(QPainter& p, const QVector<QPointF>& pts) const;   // pts 已变换到目标坐标系
    void drawTrackPlayback(QPainter& p) const;                                // 动态层：播放头 + 拖尾
    // 播放头在折线上的归一化位置（0..1）→ 预投影点；绘制与门禁共用的唯一真源。
    // frac<0 或 >1 时钳到端点。纯函数（无副作用），故门禁断言与产品绘制必然同源。
    QPointF trackPosAt(double frac) const;

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
    QVector<QDate>     gpsDates_;              // A 方向：与 gpsPts_ 逐点对齐的拍摄日期
    int                footLayer_ = FL_Dots;   // A 方向：足迹层模式（默认光点，零回归）
    int                footYearFilter_ = 0;    // A 方向第三步：按年筛选（0=全部年份）
    int                heatRamp_ = HR_Warm;    // A 方向第四步：热力色带（默认暖橙=2.12 基线）
    int                heatRampForceTier_ = -1; // QA 反例：>=0 时强制单档（抹掉密度分级）

    // A 方向第二步：轨迹播放状态（动态层，禁进静态烘焙层否则每帧重栅格化破 PERF）
    QTimer           trackTimer_;             // 16ms 驱动，播放期挂载、播完自停
    QElapsedTimer    trackClock_;             // 播放计时（reduceMotion_ 下直接终态）
    bool             trackActive_ = false;    // 播放中
    qint64           trackTOverride_ = -1;    // QA：>=0 时强制播放时刻（0..1 归一化×kTrackDurMs）
    static constexpr int kTrackDurMs = 2600;  // 单次播完总时长（按段数分配，见 trackPosAt）
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
    double  defaultCenterX() const;                   // 默认视口中心 x（中国锚点，缺省 105°E）
    double  worldOffYClamped() const;                 // 纵向偏移按当前 zoom 夹取
    double  worldZoom_  = 1.0;                        // 缩放倍率 [1,4]
    double  worldOffY_  = 0.0;                        // 纵向视口偏移（预投影×scale 像素）
    bool    worldInited_ = false;                     // 首帧已做「中国居中」初始化

    // 世界层拖拽平移：按下空白/海洋进入待命，拖动平移，原地松开=点空白（返回上一级）
    bool    panning_   = false;
    bool    panMoved_  = false;
    QPointF panStart_;
    QPointF panLast_;

    // 流畅性：下钻视图「静态层」缓存（网格/省面/国界/标签/足迹点每视图只栅格化一次）
    void invalidateViewCache() { viewPixValid_ = false; }
    void bakeViewPix(const QRectF& dst);  // 下钻静态层 DPR 感知烘焙（paintMap 调用）
    QPixmap viewPix_;
    bool    viewPixValid_ = false;
    // 下钻视图滚轮缩放（1–8x，光标锚定）+ 拖拽平移；viewOrigin 为变换唯一真源
    QPointF viewOrigin(const QRectF& bbox, const QRectF& dst, double& s) const;
    void    clampViewPan(const QRectF& bbox, const QRectF& dst, double s);
    void    resetViewZoom();                      // 进入视图时重置缩放/平移
    // F3 a11y：键盘导航（滚轮缩放 / 拖拽平移 / 下钻 的键盘等价，视口中心锚定）
    void    zoomBy(double factor);                // factor>1 放大
    void    panBy(double dx, double dy);          // 屏幕像素平移
    void    drillAtCenter();                      // 视口中心下钻 / 返回上一级
    double  viewZoom_ = 1.0;
    QPointF viewPan_;

    QRectF  curViewBbox_;                            // 当前视图 bbox（动态航线拟合用）
    mutable QVector<int>        densCache_;          // 密度分级缓存（gpsPts_ 变更时失效）
    mutable bool                densDirty_  = true;
    struct CacheEntry { QPixmap pm; QPointF off; };
    mutable QHash<QString, CacheEntry> labelPixCache_;  // 文字 halo 预渲染缓存（按 文本|色|字体）
    mutable QVector<QPointF> arcTargetsCache_;          // 航线目标集缓存（gpsPts_/arcRegionIds_ 变更时失效）
    mutable bool             arcTargetsDirty_ = true;

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
