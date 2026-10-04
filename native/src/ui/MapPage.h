#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include "MapView.h"
#include <QDate>
#include <QComboBox>
#include "../core/Library.h"

class MapPage : public QWidget {
    Q_OBJECT
public:
    explicit MapPage(const QVector<Photo>* photos, const QString& thumbsDir,
                     QWidget* parent = nullptr);
    void setHidePrivate(bool hide);   // P16 隐私门：锁定时统计跳过私密照片
    MapView* view() const { return view_; }   // A 现状墨韵·抛光 QA 探针访问

signals:
    void requestGallery(const QString& regionId, const QString& provEn); // 在图库查看
    void requestLightbox(const QString& path);   // 爆炸卡片点击 → 灯箱看大图

private slots:
    void onPicked(const QString& key, const QString& label, bool isProvince);
    void onViewInGallery();
    void onRelocate();   // P16.4.2 重新定位航线基点（「我的位置」）
    void onBurstRequest(const QString& key, const QString& label); // 省视图爆炸弹层
    void onRecenter();   // 「⌂ 回到中国」：世界层回到默认视角
    void onPlayAll();    // 「▶ 播放本区作品」：省/大区/国家三层级通用的爆炸弹层
    void onCountryPicked(const QString& name, const QStringList& paths);   // 国家视图统计
    void onCountryBurst(const QString& name, const QStringList& paths);    // 国家视图爆炸弹层

private:
    int computeGpsCount() const;   // P16.1 真实 GPS 足迹计数（尊重隐私门）
    QVector<QPointF> computeGpsPoints(QStringList* pathsOut = nullptr, QVector<QDate>* datesOut = nullptr) const;   // P16.4 真实 GPS 足迹点（pathsOut 同步路径，datesOut 同步拍摄日期）
    QStringList computeArcRegionIds() const;     // P16.4.2 有作品的大区 id（航线目标）
    QList<int> computeFootYears() const;         // A 方向第三步：库中出现的全部年份（降序）
    // 足迹数据同步唯一入口（A 方向第三步）：pts/dates/paths 三者同源注入 + 计数。
    // 铁律：任何改变可见足迹集合的操作（年份切换 / 隐私门）都必须走这里，
    // 否则三者的下标对齐与缓存失效会各自漂移。
    void syncFootData();

private:
    MapView* view_ = nullptr;
    QLabel*  crumb_ = nullptr;
    QPushButton* back_ = nullptr;
    QPushButton* recenterBtn_ = nullptr;   // 迭代 1：世界层「⌂ 回到中国」
    QPushButton* relocBtn_ = nullptr;   // P16.4.2 重新定位
    QLabel*      homeHint_ = nullptr;   // P16.4.2 基点提示
    QString      homeName_ = QString::fromUtf8("上海");
    // 统计侧栏
    QWidget*     stats_ = nullptr;
    QLabel*      statsTitle_ = nullptr;
    QLabel*      statsBody_ = nullptr;
    QPushButton* viewBtn_ = nullptr;
    QPushButton* playBtn_ = nullptr;    // 迭代 2：「▶ 播放本区作品」
    QButtonGroup* footLayerGroup_ = nullptr;   // A 方向：光点/轨迹/热力 三态分段开关
    QPushButton* trackPlayBtn_ = nullptr;     // A 方向第二步：「▶ 播放足迹」
    QComboBox* yearBox_ = nullptr;             // A 方向第三步：按年筛选下拉
    const QVector<Photo>* photos_ = nullptr;
    QString     thumbsDir_;            // 缩略图目录（爆炸弹层取图用）
    QStringList lastCountryPaths_;      // 国家视图的该国 GPS 照片路径（播放按钮用）
    bool    hidePrivate_ = true;   // P16 隐私门（默认锁）
    int     footYear_ = 0;         // A 方向第三步：当前年份筛选（0=全部）
    QString lastRegion_;   // 最近一次 pick 的大区 id（空=世界）
    QString lastProv_;     // 最近一次 pick 的省 en 名（空=未选省）
    bool    lastIsProv_ = false;
};
