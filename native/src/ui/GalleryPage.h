#pragma once
// 图库页：QListView(IconMode) 窗口化网格 + 自绘委托（卡片/横竖徽章/标题/缺失标记）
// P16：分类 chips 条（CLIP 本地批量分类写库后读库筛选）+ 人物筛选条 + 隐私门同步
#include <QWidget>
#include "EditorialView.h"  // P23 E 杂志拼贴视图
#include <QLabel>
#include <QStringList>
#include <QTimer>
#include "GalleryModel.h"
#include "ThumbCache.h"

class GalleryDelegate;
class QPushButton;

class GalleryPage : public QWidget {
    Q_OBJECT
public:
    GalleryPage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent = nullptr);
    void applyFilter(const QString& q);
    void applyGeoFilter(const QString& regionId, const QString& provEn); // 地图跳转：大区/省筛选
    void applyCategoryFilter(const QString& cat);                        // P16 分类 chips
    void setHidePrivate(bool hide);                                      // P16 隐私门（重建 chips 计数）
    void refresh();                                                      // 灯箱改评分/私密后刷新
    void qaShootE(const QString& png);                                   // P23 E 像素/几何自测（offscreen）
    QScrollBar* qaScrollBar() const;   // 方向 D2：offscreen 性能门访问 EditorialView 滚动条
    bool hidePrivate() const { return hidePrivate_; }   // D5b：批量私密后据此重跑隐私过滤
    // ---- D5b QA 入口（批量多选门用；等价于用户 Ctrl/Shift 点击）----
    QStringList qaSelectedPaths() const;
    int  qaSelectCount() const;
    void qaToggleRow(int row);
    void qaClearSelection();
    void qaSelectAll();
    bool qaBatchBarVisible() const;
    int  qaBatchBarStarCount() const;
    void qaClickRow(int row, bool ctrl, bool shift);
    // QA：直接触发批量动作（等价点★/🔒），供端到端断言 signal 载荷
    void qaFireBatchRating(int rating);
    void qaFireBatchPrivate(bool priv);

signals:
    void photoActivated(const QString& path);   // 点击卡片 → 沉浸式灯箱
    // ---- D5b：批量动作上报（视图层不持有写能力，一律 signal 交给 MainWindow 执行）----
    void batchRatingRequested(const QStringList& paths, int rating);
    void batchPrivateRequested(const QStringList& paths, bool priv);

private:
    void buildChips();          // P16：按当前数据 + 隐私门状态重建分类/人物 chips
    void updateCount();         // 统一计数文案（含各筛选状态注记）
    void restyleChips();        // 选中态高亮当前 chip

    const QVector<Photo>* photos_ = nullptr;
    GalleryModel model_;
    ThumbCache   cache_;
    EditorialView* view_ = nullptr;  // P23：原 QListView(IconMode) 均匀网格 -> 杂志拼贴
    QLabel*      count_ = nullptr;
    QTimer*      prefetchTimer_ = nullptr;
    QWidget*     chipsBar_ = nullptr;         // P16 分类 chips（无分类数据时整条隐藏）
    QVector<QPushButton*> catChips_;
    QWidget*     personBar_ = nullptr;        // P16 人物 chips（无人物数据时隐藏）
    QVector<QPushButton*> personChips_;
    QString      activeCat_;                  // 当前分类（空=全部；GalleryModel::kNoCategory=未分类）
    QString      activePerson_;
    QString      searchText_;                 // 搜索框状态（计数注记用）
    bool         hidePrivate_ = true;
    // ---- D5b：批量工具条（选中非空才显示；★1-5 / 🔒私密 / 取消选择）----
    QWidget*     batchBar_ = nullptr;
    QLabel*      batchInfo_ = nullptr;
    QVector<QPushButton*> starBtns_;
    QPushButton* privBtn_ = nullptr;
    int          selCount_ = 0;
    void buildBatchBar();
    void onSelectionChanged(int count);
    void emitBatchRating(int rating);
    void emitBatchPrivate(bool priv);
private slots:
    void prefetch();
};
