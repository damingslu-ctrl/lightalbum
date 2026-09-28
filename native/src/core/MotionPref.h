// P14 动效偏好 + 集中动效常量。
// ① 系统「减弱动效」(reduced-motion) 检测 —— 无障碍友好：
//    检测顺序 = 显式环境变量 LA_REDUCED_MOTION > Windows 系统设置 > 默认开启动效。
// ② 全客户端动效时长/节奏常量集中在此，便于「贴合手感」统一调参。
#pragma once
#include <cstdlib>

#ifdef Q_OS_WIN
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  ifndef SPI_GETCLIENTAREAANIMATION
#    define SPI_GETCLIENTAREAANIMATION 0x1042
#  endif
#endif

namespace Lumen {

// ---- 系统「减弱动效」偏好 ----
// Windows：「设置 → 辅助功能 → 视觉效果 → 动画效果」会同步 SPI_GETCLIENTAREAANIMATION。
// 关闭时返回 enabled=FALSE → 视为需要减弱动效。
inline bool systemReducedMotion()
{
    // 显式覆盖（测试/用户强制），优先级最高
    const char* e = std::getenv("LA_REDUCED_MOTION");
    if (e) {
        switch (e[0]) {
        case '1': case 't': case 'T': case 'y': case 'Y': return true;
        case '0': case 'f': case 'F': case 'n': case 'N': return false;
        default: break;
        }
    }
#ifdef Q_OS_WIN
    BOOL enabled = FALSE;
    if (::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)
        && !enabled) {
        return true;
    }
#endif
    return false;
}

// ---- 动效时长(ms)/节奏常量：一处调参，全端生效 ----
namespace Motion {
inline constexpr int    kSplashIntroMs    = 420;   // 开场整体淡入（原 360）
inline constexpr int    kSplashHoldMs     = 1300;  // 呼吸停留（原 1500）
inline constexpr int    kSplashContractMs = 720;   // 收缩进缝（原 680）
inline constexpr int    kExpandMs         = 700;   // 客户端从缝展开（原 620）
inline constexpr int    kNavFadeMs        = 170;   // 页面切换淡出（原 150）
inline constexpr int    kNavSlideMs       = 280;   // 页面上滑+淡入（原 260）
// 呼吸节拍：30fps 定时器（33ms），每帧相位增量 → 整周期 ≈ 2π/step ≈ 4.6s
inline constexpr int    kBreathIntervalMs = 33;
inline constexpr double kBreathStep       = 0.045;
} // namespace Motion

} // namespace Lumen
