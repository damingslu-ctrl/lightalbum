// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 方向 C·智能相册：相似照片去重对话框。
// 只读浏览去重组（保留者 + 相似缩略图 + 原因），并提供"标记其余为待删"
// 的可逆动作（经 markRequested → GalleryPage::batchMarkRequested → 库落 mark）。
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
    DedupDialog(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent = nullptr);
signals:
    void markRequested(const QStringList& paths);   // 用户点"标记其余为待删"
private:
    void scan();
    void buildGroup(const DupGroup& g, QVBoxLayout* parent);
    void onMarkGroup(const QStringList& dupPaths, QWidget* w);
    const QVector<Photo>* photos_ = nullptr;
    QString                thumbsDir_;
    QHash<QString, QString> thumbOf_;   // path -> thumbFile
    QLabel*                summary_ = nullptr;
    QVBoxLayout*           listLay_ = nullptr;
};
