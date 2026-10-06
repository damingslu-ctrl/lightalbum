#pragma once
// P23 图库 E「杂志拼贴」：自定义非均匀网格视图，替代 QListView(IconMode) 的均匀网格。
// 首图（row 0）占 2×2 作为 feature 大图，其余卡片按列数向下流式填充。
// 方向 D3：窗口化虚拟化——只实例化「可见视口 + 前后缓冲」的池化瓦片，
// content_ 设为虚拟总高（feature 2×2 + 流式网格）驱动滚动条，滚动/缩放时按可见行重绑池化瓦片。
// 复用 GalleryModel（modelReset→重建、dataChanged→刷新该行）与 Lumen::paintGalleryCard，
// 与 chips 筛选/隐私门/灯箱入口完全兼容。对外 API（featureGeometry / tileGeometry /
// tileCount / columnCount / prefetchVisible / photoActivated）语义与虚拟化前一致。
#include <QScrollArea>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QSet>
#include <QPoint>
#include <QTimer>

class GalleryModel;
class GalleryTile;

class EditorialView : public QScrollArea {
    Q_OBJECT
public:
    explicit EditorialView(GalleryModel* model, QWidget* parent = nullptr);

    // 仅预取当前可见（及前后缓冲）卡片缩略图，替代原 QListView indexAt 逻辑
    void prefetchVisible();
    static int qaKeyboardCheck();   // F3 a11y 键盘可达门禁（offscreen）
    // 2.22.0 骨架屏 QA：设定/读取当前相位与减动效旗标（门禁据此复现不同帧）
    static void   qaSetSkeletonPhase(double phase, bool reduceMotion);
    static double qaSkeletonPhase();
    // 缺陷①回归：反例注入（模拟修复前 updateWindow 不重绑几何）+ 几何一致性断言
    static void   qaSetNoReflow(bool v);
    // 方向 D 第六步：性能回归反例注入——置 >0 时每帧绘制额外睡眠该毫秒数，
    // 精确复现"绘制退化"；runScaleRealPerfQa 的 LA_SCALE_REAL_PERF_QA_FORCE_BROKEN 调用。
    static void   qaSetPaintLagMsForQa(int ms);
    int           qaReflowCheck() const;

signals:
    void photoActivated(const QString& path);   // 点击卡片 → 沉浸式灯箱
    // 2.22.0 骨架屏：可见区是否存在"未就绪"卡片。视图据此挂载/停用脉冲计时器，
    // 避免常驻空转（只在真有骨架时才耗 CPU）。
    void skeletonVisibilityChanged(bool any);
    // D5b：选中集变化（供 GalleryPage 显隐批量工具条 + 更新计数文案）
    void selectionChanged(int count);

public slots:
    void rebuild();                              // modelReset → 重建全部瓦片池
    void relayout();                             // 重算列数/单元格/虚拟高并刷新窗口（feature 2×2 + 流式）

public:
    int  tileCount() const { return total_; }
    int  columnCount() const { return cols_; }
    QRect featureGeometry() const;          // 首图（row0）几何
    QRect tileGeometry(int i) const;        // 第 i 张瓦片几何

    // ---- D5b：多选（无修饰=单选并打开灯箱；Ctrl=切换；Shift=范围）----
    const QSet<int>& selectedRows() const { return selRows_; }
    QStringList selectedPaths() const;              // 选中行 -> Photo::path 列表
    void selectAll();                // 全选（当前过滤后的全部行）
    void clearSelection();
    void toggleRow(int row);                        // Ctrl 语义（QA 入口复用）
    // 点击一行：mods 决定单选/多选/范围；**未按 Ctrl/Shift 时才发 photoActivated**
    void clickRow(int row, Qt::KeyboardModifiers mods);
    // 选中集变化后统一把选中态刷到瓦片
    void refreshSelectionVisual();

private slots:
    void updateSkeletonTimer();
    void onDataChanged(const QModelIndex& top, const QModelIndex& bottom,
                       const QVector<int>& roles);
    void updateWindow();                         // 窗口化：可见行 → 池化瓦片映射

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    GalleryTile* acquire();                      // 取一个空闲池化瓦片（无则新建）
    void        recycle(GalleryTile* t);         // 回收瓦片到空闲池（隐藏）
    void        computeLayout();                 // 列数/单元格/虚拟高/流式单元列表
    QRect        featureRect() const;            // 首图（row0）rect
    QRect        cellRect(int i) const;          // 第 i 张（i>=1）流式 rect

    QTimer* skelTimer_ = nullptr;                // 2.22.0 骨架脉冲计时器（按需启停）
    GalleryModel* model_ = nullptr;
    QWidget*      content_ = nullptr;            // 虚拟内容宿主（固定虚拟尺寸，非布局）
    QVector<GalleryTile*> pool_;                 // 全部曾创建的瓦片（不删除，循环复用）
    QVector<GalleryTile*> free_;                 // 当前空闲可复用瓦片
    QHash<int, GalleryTile*> bound_;             // row → 当前可见绑定的瓦片

    int  total_ = 0;                            // 模型总行数（逻辑瓦片数）
    int  cols_  = 4;
    int  cellW_ = 0, cellH_ = 0;                // 单元格像素尺寸
    int  contentW_ = 0, contentH_ = 0;          // 虚拟内容尺寸（驱动滚动条）
    int  margin_  = 10;
    int  spacing_ = 6;
    QVector<QPoint> avail_;                     // 流式单元 (row,col)，索引 i-1 → 第 i 张（i>=1）
    int  selectedRow_ = -1;      // 键盘焦点行（Shift 范围锚点）
    QSet<int> selRows_;          // D5b：多选集（模型行号）
    bool relayoutPending_ = false;
};
