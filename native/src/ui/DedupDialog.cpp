// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "DedupDialog.h"
#include "../core/PhotoDedup.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QPixmap>
#include <QDir>
#include <QCoreApplication>

static void parseDim(const QString& dim, int& w, int& h)
{
    w = h = 0;
    const int x = dim.indexOf(QLatin1Char('x'));
    if (x < 0) return;
    w = dim.left(x).toInt();
    h = dim.mid(x + 1).toInt();
}

static QPixmap loadThumb(const QString& thumbsDir, const QString& thumbFile, int sz)
{
    QPixmap pm(QDir(thumbsDir).filePath(thumbFile));
    if (pm.isNull()) return QPixmap();
    return pm.scaled(sz, sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

DedupDialog::DedupDialog(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent)
    : QDialog(parent), photos_(photos), thumbsDir_(thumbsDir)
{
    setWindowTitle(QCoreApplication::translate("LightAlbum", "相似照片"));
    resize(760, 580);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    summary_ = new QLabel(this);
    summary_->setStyleSheet(QStringLiteral("color:#8da996;font-size:13px;"));
    root->addWidget(summary_);

    auto* sa = new QScrollArea(this);
    sa->setWidgetResizable(true);
    sa->setStyleSheet(QStringLiteral(
        "background:#060806;border:1px solid #1c2620;border-radius:10px;"));
    auto* list = new QWidget(sa);
    listLay_ = new QVBoxLayout(list);
    listLay_->setContentsMargins(12, 12, 12, 12);
    listLay_->setSpacing(12);
    sa->setWidget(list);
    root->addWidget(sa, 1);

    if (photos_) {
        for (const Photo& p : *photos_)
            thumbOf_.insert(p.path, p.thumbFile);
        scan();
    } else {
        summary_->setText(QCoreApplication::translate("LightAlbum", "未找到相似照片"));
    }
}

void DedupDialog::scan()
{
    QVector<DedupPhoto> dps;
    dps.reserve(photos_->size());
    for (const Photo& p : *photos_) {
        DedupPhoto d;
        d.path     = p.path;
        d.thumbFile = p.thumbFile;
        parseDim(p.dim, d.origW, d.origH);
        dps.append(d);
    }
    const QVector<DupGroup> groups = PhotoDedup::findGroups(dps, thumbsDir_, 8, true);

    int dupTotal = 0;
    for (const DupGroup& g : groups) {
        dupTotal += g.dupPaths.size();
        buildGroup(g, listLay_);
    }
    if (groups.isEmpty())
        summary_->setText(QCoreApplication::translate("LightAlbum", "未找到相似照片"));
    else
        summary_->setText(QCoreApplication::translate(
            "LightAlbum", "发现 %1 组相似照片，共 %2 张可清理")
                .arg(groups.size()).arg(dupTotal));
}

void DedupDialog::buildGroup(const DupGroup& g, QVBoxLayout* parent)
{
    auto* w = new QWidget(this);
    w->setStyleSheet(QStringLiteral(
        "background:#0e1410;border:1px solid #1c2620;border-radius:10px;"));
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(12);

    // 保留者缩略图
    auto* kept = new QLabel(w);
    kept->setPixmap(loadThumb(thumbsDir_, thumbOf_.value(g.keptPath), 96));
    lay->addWidget(kept);

    // 右侧：原因 + 重复缩略图行 + 动作
    auto* col = new QVBoxLayout;
    col->setSpacing(6);
    auto* reason = new QLabel(g.reason, w);
    reason->setStyleSheet(QStringLiteral("color:#d9e4dc;font-size:12px;"));
    col->addWidget(reason);

    auto* dups = new QWidget(w);
    auto* dlay = new QHBoxLayout(dups);
    dlay->setContentsMargins(0, 0, 0, 0);
    dlay->setSpacing(4);
    for (const QString& dp : g.dupPaths) {
        auto* lab = new QLabel(dups);
        lab->setPixmap(loadThumb(thumbsDir_, thumbOf_.value(dp), 48));
        dlay->addWidget(lab);
    }
    col->addWidget(dups);

    auto* markBtn = new QPushButton(
        QCoreApplication::translate("LightAlbum", "标记其余为待删"), w);
    markBtn->setCursor(Qt::PointingHandCursor);
    markBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#131a15;border:1px solid #26312a;border-radius:8px;"
        "padding:3px 10px;color:#8da996;font-size:12px;}"
        "QPushButton:hover{border-color:#4fc08a;color:#d9e4dc;}"));
    const QStringList dupsCopy = g.dupPaths;
    connect(markBtn, &QPushButton::clicked, this, [this, dupsCopy, w]() {
        onMarkGroup(dupsCopy, w);
    });
    col->addWidget(markBtn);
    lay->addLayout(col, 1);

    parent->addWidget(w);
}

void DedupDialog::onMarkGroup(const QStringList& dupPaths, QWidget* w)
{
    emit markRequested(dupPaths);
    w->deleteLater();   // 标记后从列表移除该组（已处理）
}
