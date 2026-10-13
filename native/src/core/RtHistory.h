#pragma once
// 修图页非破坏式撤销/重做：与 UI 无关的纯状态机（可独立 headless 单测）。
// 编辑状态 = 修调参数 + AI 计划 + 几何变换；原图（geoSrc_）只读、永不被改，
// 撤销/重做只是把快照重新套用到原图上重算出图，天然非破坏。
#include <QMap>
#include <QString>
#include <QRectF>
#include <QVector>
#include <QImage>
#include <QStringList>

struct RtEditState {
    QMap<QString, double> cur;        // 修调参数（归一量）
    QMap<QString, double> agentPlan;  // 生效中的 AI 13 项计划
    bool        agentMode = false;
    int         rot = 0;               // 0..3 顺时针 90° 步进
    bool        flipH = false, flipV = false;
    double      angle = 0.0;           // 拉直角度（度）
    double      aspect = 0.0;          // 裁剪宽高比，0 = 自由
    QRectF      cropNorm;              // 交互裁剪框（归一化）；无效 = 未裁
    QImage      enh;                   // 2.38.0 R1：一键增强结果（null=未用）
    QStringList ops;                   // 已应用算子序列（重放）

    bool operator==(const RtEditState& o) const {
        return cur == o.cur && agentPlan == o.agentPlan && agentMode == o.agentMode
            && rot == o.rot && flipH == o.flipH && flipV == o.flipV
            && qFuzzyCompare(angle, o.angle) && qFuzzyCompare(aspect, o.aspect)
            && cropNorm == o.cropNorm
            && enh == o.enh && ops == o.ops;
    }
    bool operator!=(const RtEditState& o) const { return !(*this == o); }
};

// 线性撤销/重做栈：current() 即当前状态；push 提交新状态（截断 redo 尾、跳过无变化）；
// undo/redo 移动游标并返回新的 current。空栈时 current() 返回默认构造状态。
class RtHistory {
public:
    void clear(const RtEditState& init) { hist_.clear(); hist_.append(init); pos_ = 0; }

    void push(const RtEditState& s) {
        if (pos_ >= 0 && pos_ < hist_.size() && hist_[pos_] == s) return; // 无变化不记
        if (pos_ < hist_.size() - 1) hist_.resize(pos_ + 1);              // 截断 redo 尾
        hist_.append(s);
        pos_ = hist_.size() - 1;
    }

    bool canUndo() const { return pos_ > 0; }
    bool canRedo() const { return pos_ < hist_.size() - 1; }

    const RtEditState& undo() { if (canUndo()) --pos_; return hist_[pos_]; }
    const RtEditState& redo() { if (canRedo()) ++pos_; return hist_[pos_]; }

    const RtEditState& current() const { return hist_[pos_]; }
    int size() const { return hist_.size(); }
    int pos()  const { return pos_; }

private:
    QVector<RtEditState> hist_;
    int pos_ = 0;
};
