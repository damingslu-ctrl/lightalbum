#include "qaExit.h"

#include <QCoreApplication>
#include <QProcessEnvironment>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

namespace QaExit {

namespace {

// 已自退标记：requestExit() 幂等（多个门禁钩子可能都请求退出，只允许生效一次）。
bool g_exited = false;

// 门禁辅助变量黑名单 —— 这些 LA_* 变量不是「门禁名」，看门狗归因时应排除。
//
// ★ 为什么用「排除清单」而不是「门禁白名单」：两者都靠人工维护，但失效方向
//   相反。白名单漏了新门禁 → 看门狗不挂 → 又回到「静默挂死」，即本次事故的形态；
//   排除清单漏了新辅助变量 → 只是看门狗归因消息里门名不好看，退出行为完全不受影响。
//   故取失效方向安全的排除清单。
const char* const kAuxVars[] = {
    // 运行器/看门狗自身
    "LA_GATE", "LA_WATCHDOG_MS",
    // 扫描/库/缩略图路径
    "LA_SCAN_ROOT", "LA_SCAN_DB", "LA_SCAN_THUMBS", "LA_SCAN_DEBUG",
    "LA_DB", "LA_THUMBS", "LA_SCALE_DB",
    // 外部服务/模型
    "LA_LLM_URL", "LA_LLM_MODEL", "LA_INPAINT_MODEL",
    // 行为开关（正常交互也可能设置）
    "LA_REDUCED_MOTION", "LA_GEO_OFF", "LA_HOME_DEBUG", "LA_LUMEN_DEBUG",
    "LA_LIGHTBOX_HOVER",
    // 断言参数（数值开关，不是门禁）
    "LA_SCALE_MIN", "LA_SCALE_MAP_MS", "LA_SCALE_PERF_FIRST",
    "LA_SCALE_PERF_JUMPS", "LA_SCALE_PERF_SCROLL",
    "LA_THUMB_BUDGET1", "LA_THUMB_BUDGET2", "LA_THUMB_FEATURE_PX",
    "LA_THUMB_TILE_PX", "LA_THUMB_QA_KEEP",
    "LA_BATCH_QA_KEEP", "LA_BATCH_QA_LAMPASS",
    "LA_BATCH_TX_QA_FAIL_AT", "LA_BATCH_TX_QA_NOLOCK",
    // 截图模式修饰
    "LA_UI_SHOT_SOLID",
    // 门禁数据目录（金样对拍）
    "LA_GOLD_DIR",
};

bool isAuxVar(const QString& name)
{
    for (const char* a : kAuxVars)
        if (name == QLatin1String(a)) return true;
    return false;
}

} // namespace

QString activeGate()
{
    // 显式指定优先：运行器（qa_runner.ps1）与 LA_EXIT_QA 的子进程都设 LA_GATE，
    // 避免「辅助变量排序」影响归因准确性。
    const QString explicitGate = qEnvironmentVariable("LA_GATE");
    if (!explicitGate.isEmpty()) return explicitGate;

    // 回退：扫描环境变量里第一个非辅助的 LA_*（序数升序 → 结果稳定可断言）。
    QStringList names = QProcessEnvironment::systemEnvironment().keys();
    names.sort();
    for (const QString& n : names) {
        if (!n.startsWith(QLatin1String("LA_"))) continue;
        if (isAuxVar(n)) continue;
        return n;
    }
    return QString();
}

void requestExit(int code)
{
    if (g_exited) return;   // 幂等：第二个调用点（如导入汇总框 + 页面钩子）不重复触发
    g_exited = true;
    // ★ 无条件排队，绝不同步 quit()。构造期注册的事件也会在 exec() 启动后触发，
    //   事件循环内调用仅延迟一个事件迭代 —— 一条路径覆盖两种时机（见 qaExit.h 铁律 2）。
    QTimer::singleShot(0, QCoreApplication::instance(), [code] {
        QCoreApplication::exit(code);
    });
}

void armWatchdog()
{
    const QString gate = activeGate();
    if (gate.isEmpty()) return;   // 非门禁场景（真实交互启动）→ 不挂，绝不杀用户进程

    // 预算：默认 20s。必须 < qa_runner.ps1 的 -Timeout(25s)，否则 runner 先 kill，
    // 拿不到归因，只剩一个无信息的 [KILL]（本次事故的原始症状）。
    int budgetMs = qEnvironmentVariableIntValue("LA_WATCHDOG_MS");
    if (budgetMs <= 0) budgetMs = 20000;

    QTimer::singleShot(budgetMs, QCoreApplication::instance(), [gate, budgetMs] {
        // 走到这里 = 事件循环活着，但没有任何门禁钩子请求退出。
        // 即「构造期裸调 quit()」这类时序缺陷（2026-10-03 LA_CULL_SMOKE 事故）。
        std::fprintf(stderr, "LA_WATCHDOG FAIL gate=%s budget_ms=%d reason=no-exit-request\n",
                     qPrintable(gate), budgetMs);
        std::fflush(stderr);
        // _exit 而非 exit()：此刻事件循环状态不可信，且可能有半初始化对象，
        // 走析构反而可能挂住。门禁进程生命周期短，跳过析构是安全的。
        std::_Exit(3);
    });
}

} // namespace QaExit
