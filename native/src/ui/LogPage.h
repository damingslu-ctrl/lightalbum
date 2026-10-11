#pragma once
// P16.4 摄影日志（日志页）：平移 web 版 renderStats 的 EXIF 分析。
// 内容：KPI 条 + 自动洞察 + 6 张图表（光圈/快门/ISO/焦距 柱状，器材 饼图，按月活跃度 柱状）。
// P30-C 控制台风格：顶栏筛选 chips（年份/机身）+ 导出 CSV；紧凑等宽数字 + 淡网格底。
// 数据诚实铁律：缺失 EXIF 不硬塞进末档（与 web 一致）；隐私门关闭时跳过 priv 照片。
#include <QWidget>
#include <QLabel>
#include <QVector>
#include <QStringList>
#include <QColor>
#include <QPushButton>
#include <QString>
#include "../core/Library.h"

// 柱状图（横向自适应宽度，圆角柱 + 数值 + 横轴标签）
class BarChart : public QWidget {
    Q_OBJECT
public:
    explicit BarChart(QWidget* parent = nullptr);
    void setData(const QStringList& labels, const QList<int>& values, const QColor& color);
    QSize sizeHint() const override { return QSize(240, 150); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QStringList labels_;
    QList<int>  values_;
    QColor      color_ = QColor(0xc1, 0x9a, 0x4b);
};

// 饼图（器材组合占比）
class PieChart : public QWidget {
    Q_OBJECT
public:
    explicit PieChart(QWidget* parent = nullptr);
    void setData(const QStringList& keys, const QList<int>& values, const QStringList& colors);
    QSize sizeHint() const override { return QSize(150, 150); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QStringList keys_;
    QList<int>  values_;
    QStringList colors_;
};

class LogPage : public QWidget {
    Q_OBJECT
public:
    explicit LogPage(const QVector<Photo>* photos, QWidget* parent = nullptr);
    void setHidePrivate(bool hide);   // P16 隐私门：锁定时统计跳过私密照片
private:
    void recompute();                 // 依据 photos_ + 筛选/隐私 重算所有图表
    void exportCsv();                 // P30-C：导出当前筛选范围的 EXIF CSV
    void rebuildFilterChips();        // 2.33 收纳：年份/机身筛选条重建（前 8 平铺 + 溢出下拉）
    const QVector<Photo>* photos_ = nullptr;
    bool    hidePrivate_ = true;      // P16 隐私门（默认锁）

    QWidget*  kpiRow_    = nullptr;
    QLabel*   insight_   = nullptr;
    BarChart* apChart_   = nullptr;
    BarChart* shChart_   = nullptr;
    BarChart* isoChart_  = nullptr;
    BarChart* focChart_  = nullptr;
    PieChart* gearChart_ = nullptr;
    QLabel*   gearLegend_= nullptr;
    BarChart* monthChart_= nullptr;

    // P30-C 控制台风格
    int                 filtYear_ = 0;          // 0 = 全部
    QString             filtCam_;                // 空 = 全部
    QStringList         yList_;                  // 2.33：可选年份（升序，rebuild 时复用）
    QStringList         cList_;                  // 2.33：可选机身
    QWidget*            toolbar_   = nullptr;
    QWidget*            chipsHost_ = nullptr;    // 2.33：筛选 chips 宿主（fillChipRow 重建）
    QPushButton*        exportBtn_ = nullptr;
    QLabel*             foot_      = nullptr;
};
