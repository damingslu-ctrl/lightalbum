#pragma once
// P16.4 摄影日志（日志页）：平移 web 版 renderStats 的 EXIF 分析。
// 内容：KPI 条 + 自动洞察 + 6 张图表（光圈/快门/ISO/焦距 柱状，器材 饼图，按月活跃度 柱状）。
// 数据诚实铁律：缺失 EXIF 不硬塞进末档（与 web 一致）；隐私门关闭时跳过 priv 照片。
#include <QWidget>
#include <QLabel>
#include <QVector>
#include <QStringList>
#include <QColor>
#include "../core/Library.h"

// 柱状图（横向自适应宽度，圆角柱 + 数值 + 横轴标签）
class BarChart : public QWidget {
    Q_OBJECT
public:
    explicit BarChart(QWidget* parent = nullptr);
    void setData(const QStringList& labels, const QVector<int>& values, const QColor& color);
    QSize sizeHint() const override { return QSize(360, 168); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QStringList labels_;
    QVector<int> values_;
    QColor      color_ = QColor(0xc1, 0x9a, 0x4b);
};

// 饼图（器材组合占比）
class PieChart : public QWidget {
    Q_OBJECT
public:
    explicit PieChart(QWidget* parent = nullptr);
    void setData(const QStringList& keys, const QVector<int>& values, const QStringList& colors);
    QSize sizeHint() const override { return QSize(180, 180); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QStringList keys_;
    QVector<int> values_;
    QStringList colors_;
};

class LogPage : public QWidget {
    Q_OBJECT
public:
    explicit LogPage(const QVector<Photo>* photos, QWidget* parent = nullptr);
    void setHidePrivate(bool hide);   // P16 隐私门：锁定时统计跳过私密照片

private:
    void recompute();                 // 依据 photos_ + hidePrivate_ 重算所有图表

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
};
