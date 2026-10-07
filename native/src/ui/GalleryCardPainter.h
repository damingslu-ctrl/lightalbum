#pragma once
// 图库卡片统一绘制：QListView 委托 与 杂志拼贴 EditorialView 共用同一套视觉，
// 保证「现状卡片」观感在两种视图下完全一致（P23 落地 E 杂志拼贴时抽出）。
#include <QPainter>
#include <QPixmap>
#include <QString>

namespace Lumen {

// 2.22.0 骨架屏：缩略图尚未就绪时的脉冲占位。
//
// —— 为什么要它（取证）——
// 改前 pm.isNull() 时只画一个「…」字符（GalleryCardPainter.cpp 旧 39-40 行），
// 大库首屏/快速滚动时满屏灰底 + 省略号，用户无法区分「正在加载」与「真的坏了」。
//
// —— 为什么是纯函数（铁律 20：动画计算必须被产品与门禁共用）——
// 脉冲强度若只存在于 paintEvent 里，门禁无法断言它；抽成纯函数后，
// 门禁可直接验证「脉冲动、范围对、reduce-motion 呈终态」，杜绝与实现漂移。
//
// phase ∈ [0,1) 为归一化时间相位；返回 0..1 的亮度系数。
// reduceMotion=true 时**返回终态 1.0**（不做脉冲动画，静态呈现）——
// 对齐 MotionPref 的减动效约定。
double skeletonPulse(double phase, bool reduceMotion = false);

enum class SkeletonState {
    Ready,       // 有图可画（正常）
    Loading,     // 缩略图未就绪 → 脉冲骨架
    Missing,     // 文件缺失 → 「图片缺失」文字（诚实标注，非骨架）
};

// 骨架态判定：产品与门禁共用的**唯一**判别式。
// 判据：没图 且 未标记 missing → 正在加载（骨架）；标了 missing → 真缺失。
SkeletonState skeletonState(const QPixmap& pm, bool miss);

void paintGalleryCard(QPainter* p, const QRectF& card,
                      const QString& title, const QString& sub,
                      const QString& orient, bool miss, int rating,
                      const QPixmap& pm, bool feature, bool hov, bool sel,
                      double skelPhase = 0.0, bool reduceMotion = false,
                      // 2.28.0 视频：isVideo 画播放徽标 + 时长标签（durSec<=0 则不显示时长）。
                      // 位置选**左上角**：右上已被横竖徽章占用、左下是标题/副行、右下是星级，
                      // 四角中唯一空闲位（铁律 30：新增可点击标记前须核对不吞既有命中区）。
                      bool isVideo = false, double durSec = 0.0);

// 2.22.0：绘制骨架占位（脉冲底 + 微光带）。phase 由调用方按时间推进并传入，
// 保证「相位」是显式参数而非隐藏的全局状态 → 可被门禁复现。
// 铁律 19：脉冲属逐帧动态元素，**只能画在动态 paint 段**，禁入任何静态烘焙层。
void paintSkeleton(QPainter* p, const QRectF& card, double phase,
                   bool reduceMotion = false);
}
