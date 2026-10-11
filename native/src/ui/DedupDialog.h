// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 方向 C·智能相册：相似照片去重对话框。
// 只读浏览去重组（保留者 + 相似缩略图 + 原因），并提供"标记其余为待删"
// 的可逆动作（经 markRequested → GalleryPage::batchMarkRequested → 库落 mark）。
// 2.33.0：①隐私门（锁定时私密照片根本不进入分析输入集）；
//         ②分析改后台线程（describe 逐张解码缩略图 + 算 MD5/pHash 不再冻住 GUI），
//           分组与文案仍留在 GUI 线程（QTranslator 非线程安全）。
#include <QDialog>
#include <QVector>
#include <QHash>
#include <QStringList>
#include "../core/Library.h"
#include "../core/PhotoDedup.h"

class QVBoxLayout;
class QLabel;
class QWidget;

class DedupDialog : public QDialog {
    Q_OBJECT
public:
    DedupDialog(const QVector<Photo>* photos, const QString& thumbsDir,
                bool hidePrivate, QWidget* parent = nullptr);
    // ---- QA 入口（隐私门 + 后台分析门；等价于用户打开对话框后等待分析结束）----
    bool qaWaitFinished(int timeoutMs = 15000) const;  // 自旋事件循环直到分析落地
    QStringList qaInputPaths() const;                  // 实际进入分析的 path 集（隐私过滤在此处生效）
    const QVector<DupGroup>& qaGroups() const { return groups_; }
    // ---- QA 钩子（反例用，打真实实现而非测试侧伪造）----
    // 打开后注入点的隐私过滤失效（私密照照样进 inputs_）→ 隐私门断言变红，
    // 证明断言有效（非空心）。
    static void setForcePrivacyLeakForQa(bool on);
signals:
    void markRequested(const QStringList& paths);   // 用户点"标记其余为待删"
private:
    void startScan();
    void buildGroup(const DupGroup& g, QVBoxLayout* parent);
    void onMarkGroup(const QStringList& dupPaths, QWidget* w);
    const QVector<Photo>* photos_ = nullptr;
    QString                thumbsDir_;
    bool                   hidePrivate_ = true;
    QVector<DedupPhoto>    inputs_;     // 隐私过滤后的待分析集（唯一入口）
    QHash<QString, QString> thumbOf_;   // path -> thumbFile（同样只含可见者）
    QLabel*                summary_ = nullptr;
    QVBoxLayout*           listLay_ = nullptr;
    QVector<DupGroup>      groups_;
    bool                   analysisDone_ = false;   // 后台分析是否已落地（含取消）
};
