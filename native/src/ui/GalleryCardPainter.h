#pragma once
// 图库卡片统一绘制：QListView 委托 与 杂志拼贴 EditorialView 共用同一套视觉，
// 保证「现状卡片」观感在两种视图下完全一致（P23 落地 E 杂志拼贴时抽出）。
#include <QPainter>
#include <QPixmap>
#include <QString>

namespace Lumen {
// 在 card 矩形内绘制一张图库卡片。feature=true 时标题/副行字号放大（feature 大图）。
void paintGalleryCard(QPainter* p, const QRectF& card,
                      const QString& title, const QString& sub,
                      const QString& orient, bool miss, int rating,
                      const QPixmap& pm, bool feature, bool hov, bool sel);
}
