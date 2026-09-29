#pragma once
// P23 图库 E「杂志拼贴」：自定义非均匀网格视图，替代 QListView(IconMode) 的均匀网格。
// 首图（row 0）占 2×2 作为 feature 大图，其余卡片按列数向下流式填充。
// 复用 GalleryModel（modelReset→重建、dataChanged→刷新该行）与 Lumen::paintGalleryCard，
// 与 chips 筛选/隐私门/灯箱入口完全兼容。
#include <QScrollArea>
#include <QGridLayout>
#include <QVector>

class GalleryModel;
class GalleryTile;

class EditorialView : public QScrollArea {
    Q_OBJECT
public:
    explicit EditorialView(GalleryModel* model, QWidget* parent = nullptr);

    // 仅预取当前可见（及前后缓冲）卡片缩略图，替代原 QListView indexAt 逻辑
    void prefetchVisible();

signals:
    void photoActivated(const QString& path);   // 点击卡片 → 沉浸式灯箱

public slots:
    void rebuild();                              // modelReset → 重建所有瓦片
    void relayout();                             // 重算列数并重新排布（feature 2×2 + 流式）

public:
    int  tileCount() const { return tiles_.size(); }
    int  columnCount() const { return cols_; }
    QRect featureGeometry() const;          // 首图（row0）几何
    QRect tileGeometry(int i) const;        // 第 i 张瓦片几何

private slots:
    void onDataChanged(const QModelIndex& top, const QModelIndex& bottom,
                       const QVector<int>& roles);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    GalleryModel* model_ = nullptr;
    QWidget*      content_ = nullptr;
    QGridLayout*  grid_ = nullptr;
    QVector<GalleryTile*> tiles_;
    int           cols_ = 4;
    int           selectedRow_ = -1;
    bool          relayoutPending_ = false;
};
