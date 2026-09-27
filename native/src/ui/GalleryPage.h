#pragma once
// 图库页：QListView(IconMode) 窗口化网格 + 自绘委托（卡片/横竖徽章/标题/缺失标记）
#include <QWidget>
#include <QListView>
#include <QLabel>
#include <QTimer>
#include "GalleryModel.h"
#include "ThumbCache.h"

class GalleryDelegate;

class GalleryPage : public QWidget {
    Q_OBJECT
public:
    GalleryPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent = nullptr);
    void applyFilter(const QString& q);

private:
    const QVector<Photo>* photos_ = nullptr;
    GalleryModel model_;
    ThumbCache   cache_;
    QListView*   view_ = nullptr;
    QLabel*      count_ = nullptr;
    QTimer*      prefetchTimer_ = nullptr;
private slots:
    void prefetch();
};
