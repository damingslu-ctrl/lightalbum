// LightAlbum Native — 门禁自退契约（2.7.0）
//
// ★ 本文件存在的理由：一次真实的门禁挂死事故（LA_CULL_SMOKE，2026-10-03）。
//
//   事故：`CullPage::onFinished()` 在**构造期**同步调用 `QApplication::quit()`，
//   而 `app.exec()` 尚未进入。Qt 的 `quit()` 只是给「当前事件循环」发退出信号，
//   没有事件循环时它**什么都不做**（no-op）。于是 exe 打印完所有断言后照常进入
//   `app.exec()`，永久空转 —— 门禁表现为 `[KILL]`（超时被杀），看起来像
//   「环境噪声 / 控制台依赖」，实为**代码缺陷**。
//
//   最小复现（探针实证，见 CHANGELOG 2.7.0）：
//     模式 early    ：exec() 之前 quit()  → RC=124 挂死
//     模式 deferred ：事件循环内 quit()  → RC=0  正常退出
//
// ★ 铁律（自此写进代码，不再靠记忆）：
//   1) 任何门禁/探针要退出进程，**必须**经 `QaExit::requestExit()`，禁止裸调
//      `QApplication::quit()` / `QCoreApplication::exit()`。
//   2) 永远不要「判断是否在事件循环内再决定同步还是异步」——Qt 无公开 API
//      判断该状态，且猜错就是挂死。`requestExit()` 无条件走
//      `QTimer::singleShot(0, ...)`：构造期注册也会在 `exec()` 启动后立即触发，
//      事件循环内调用则延迟一个事件迭代（~0ms）。**一条路径覆盖两种时机。**
//   3) 看门狗 `armWatchdog()` 必须在 QApplication 构造后立刻调用：它抓的正是
//      「事件循环活着但没人 quit」这类缺陷（正是本次事故的形态）。
//      看门狗预算必须 < qa_runner.ps1 的 -Timeout，否则 runner 先 kill，
//      拿不到归因，只剩一个无信息的 [KILL]。
#ifndef QAEXIT_H
#define QAEXIT_H

#include <QString>

namespace QaExit {

/// 请求退出进程（幂等）。code 为退出码。
/// 无论调用点在事件循环内还是构造期，都保证最终真正退出。
void requestExit(int code = 0);

/// 挂载自退看门狗。仅在检测到 LA_* 门禁环境变量时调用（真实交互启动不挂，
/// 否则用户正常开相册 20 秒后会被杀）。
///
/// 超时未退出 → 打印 `LA_WATCHDOG FAIL gate=<门名> budget_ms=<ms>` 并 `_exit(3)`。
/// 预算取 `LA_WATCHDOG_MS`（缺省 20000ms，须小于 runner 的 25s）。
void armWatchdog();

/// 当前生效的门禁名（扫描环境变量里第一个 `LA_*` 且排除辅助变量），无则空串。
/// 供看门狗归因与 LA_EXIT_QA 断言使用。
QString activeGate();

} // namespace QaExit

#endif // QAEXIT_H
