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

signals:
    void requestGallery(const QString& regionId, const QString& provEn); // 在图库查看

private slots:
    void onPicked(const QString& key, const QString& label, bool isProvince);
    void onViewInGallery();

private:
    MapView* view_ = nullptr;
    QLabel*  crumb_ = nullptr;
    QPushButton* back_ = nullptr;
    // 统计侧栏
    QWidget*     stats_ = nullptr;
    QLabel*      statsTitle_ = nullptr;
    QLabel*      statsBody_ = nullptr;
    QPushButton* viewBtn_ = nullptr;
    const QVector<Photo>* photos_ = nullptr;
    QString lastRegion_;   // 最近一次 pick 的大区 id（空=世界）
    QString lastProv_;     // 最近一次 pick 的省 en 名（空=未选省）
    bool    lastIsProv_ = false;
};
