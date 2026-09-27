#include "MapPage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

MapPage::MapPage(QWidget* parent) : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 顶部面包屑条
    auto* bar = new QWidget(this);
    bar->setStyleSheet(QStringLiteral("background:#0b0e0c;border-bottom:1px solid #2a2f2c;"));
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(16, 8, 16, 8);

    back_ = new QPushButton(QStringLiteral("\u2190 返回"), bar);
    back_->setEnabled(false);
    back_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#181d1a;border:1px solid #2a2f2c;border-radius:8px;"
        "padding:5px 12px;color:#c9a24b;}"
        "QPushButton:disabled{color:#5a5f5b;}"));
    crumb_ = new QLabel(QStringLiteral("世界"), bar);
    crumb_->setStyleSheet(QStringLiteral("color:#e8e2d4;font-size:13px;"));
    bl->addWidget(back_);
    bl->addWidget(crumb_);
    bl->addStretch(1);
    lay->addWidget(bar);

    view_ = new MapView(this);
    lay->addWidget(view_, 1);

    connect(view_, &MapView::breadcrumbChanged, crumb_, &QLabel::setText);
    connect(back_, &QPushButton::clicked, view_, &MapView::goUp);
    connect(view_, &MapView::breadcrumbChanged, this, [this](const QString& t) {
        Q_UNUSED(t);
        back_->setEnabled(view_->level() != MapView::World);
    });
}
