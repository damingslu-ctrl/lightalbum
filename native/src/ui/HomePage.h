#pragma once
// ============================================================================
// P17 沉浸式首页「Quiet Frame」—— 吸收自用户参考视频的版式语言（非内容）：
//   全屏黑底 + 衬线标题 + 左侧章节面板（当前章封面模糊渐变打底）
//   + 主区全出血轮播（横滑 + 细箭头 + 01/N 计数 + 城市角标）
//   + 底部分段进度条（每张一段，当前金色）。
// 内容铁律（数据诚实）：章节 = 真实库按 城市→省→大区 兜底分组，只取有定位的照片；
//   跳过 priv / missing / 无缩略图；计数、年份、关键词全部来自真实数据，绝不编造。
// ============================================================================
#include <QWidget>
#include <QVector>
#include <QStringList>
#include "../core/Library.h"

class QLabel;
class QFrame;
class QToolButton;
class QTimer;
class QVBoxLayout;

class HomePage : public QWidget {
    Q_OBJECT
public:
    // 章节（纯数据，可无 GUI 冒烟）：startNo 在排序截断后赋 1..N
    struct Chapter {
        QString city, prov, region;     // 分组键（展示兜底链 city→prov→region）
        QString title;                  // 大标题（城市中文名 / 省英文名 / 大区）
        QString capsLine;               // "CHAPTER 01 · GUANGDONG PROVINCE · CHINA"
        QString subtitle;               // "2019 — 2021 · 128 幅"（真实计数）
        QString metaLine;               // "2021 · GUANGZHOU · ROLL 01"
        int     startNo = 0;
        int     yearMin = 0, yearMax = 0;
        int     locatedTotal = 0, totalCount = 0;   // 定位覆盖率（诚实展示）
        QStringList keywords;           // CLIP 真实分类 top（≤3，非空才算）
        QVector<const Photo*> photos;   // 日期倒序（该章内）
    };

    // 纯逻辑分组（不碰 GUI，可 LA_HOME_SMOKE 无头调用）
    static QVector<Chapter> buildChapters(const QVector<Photo>& photos,
                                          int maxChapters = 12);

    HomePage(Library* lib, const QString& thumbsDir, QWidget* parent = nullptr);

    void setAnimations(bool on);          // 冒烟/截图/减弱动效 → 关滑动与自动轮播

signals:
    void navigateTo(int navRow);          // 顶部文字导航 → MainWindow::onNav
    void photoActivated(const QString& path);   // 点图 → 沉浸式灯箱
    void importRequested();               // 空库 → 导入目录
    void icloudImportRequested();         // 2.6.0：空库 → 导入 iCloud 相册

protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    void rebuild();                       // 库快照 → 章节 → 首章上屏
    void setChapter(int idx, bool animateStage = true);
    void refreshPanel();                  // 章节面板文字 / 背景打底 / 网格高亮
    void rebuildProgress();               // 底部分段条（≤48 段逐张，否则连续比例）
    void stageChanged(int idx, int total);// Stage 回调：计数 / 角标 / 进度

    Library* lib_ = nullptr;
    QString  thumbsDir_;
    bool     anim_ = true;

    QVector<Chapter> chapters_;
    int      chapterIdx_ = 0;

    // 顶部条
    QWidget*  topBar_ = nullptr;
    QVector<QToolButton*> navTexts_;      // 首页…关于（7 项，行号即导航行）
    // 主体
    QWidget*  panel_ = nullptr;           // 左侧章节面板（自绘模糊打底）
    QWidget*  stage_ = nullptr;           // 主区轮播（HomePage.cpp 内定义 Stage）
    QWidget*  segBar_ = nullptr;          // 底部分段条（自绘）
    // 面板内文字件
    QLabel*   capsLbl_ = nullptr;
    QLabel*   titleLbl_ = nullptr;
    QFrame*   goldRule_ = nullptr;
    QLabel*   subtitleLbl_ = nullptr;
    QLabel*   keywordLbl_ = nullptr;
    QWidget*  grid_ = nullptr;            // 章节号网格
    QVector<QToolButton*> gridBtns_;
    QLabel*   coverLbl_ = nullptr;        // 定位覆盖（诚实）
    QLabel*   metaLbl_ = nullptr;
    QToolButton* prevChapBtn_ = nullptr;
    QToolButton* nextChapBtn_ = nullptr;
    QTimer*   autoTimer_ = nullptr;       // 自动轮播（尊重减弱动效）
};
