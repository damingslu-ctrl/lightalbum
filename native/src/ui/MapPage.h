#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include "MapView.h"
#include "../core/Library.h"

class MapPage : public QWidget {
    Q_OBJECT
public:
    explicit MapPage(const QVector<Photo>* photos, QWidget* parent = nullptr);
    void setHidePrivate(bool hide);   // P16 隐私门：锁定时统计跳过私密照片
    MapView* view() const { return view_; }   // A 现状墨韵·抛光 QA 探针访问

signals:
    void requestGallery(const QString& regionId, const QString& provEn); // 在图库查看

private slots:
    void onPicked(const QString& key, const QString& label, bool isProvince);
    void onViewInGallery();
    void onRelocate();   // P16.4.2 重新定位航线基点（「我的位置」）

private:
    int computeGpsCount() const;   // P16.1 真实 GPS 足迹计数（尊重隐私门）
    QVector<QPointF> computeGpsPoints() const;   // P16.4 真实 GPS 足迹点（预投影空间）
    QStringList computeArcRegionIds() const;     // P16.4.2 有作品的大区 id（航线目标）

private:
    MapView* view_ = nullptr;
    QLabel*  crumb_ = nullptr;
    QPushButton* back_ = nullptr;
    QPushButton* relocBtn_ = nullptr;   // P16.4.2 重新定位
    QLabel*      homeHint_ = nullptr;   // P16.4.2 基点提示
    QString      homeName_ = QString::fromUtf8("上海");
    // 统计侧栏
    QWidget*     stats_ = nullptr;
    QLabel*      statsTitle_ = nullptr;
    QLabel*      statsBody_ = nullptr;
    QPushButton* viewBtn_ = nullptr;
    const QVector<Photo>* photos_ = nullptr;
    bool    hidePrivate_ = true;   // P16 隐私门（默认锁）
    QString lastRegion_;   // 最近一次 pick 的大区 id（空=世界）
    QString lastProv_;     // 最近一次 pick 的省 en 名（空=未选省）
    bool    lastIsProv_ = false;
};
