// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
#include <QObject>
#include <QProcess>
#include <QImage>
#include <QString>
#include <atomic>
#include <cstdint>

// 2.36.0 线程化：Runner 宿主在专属 QThread 上（QProcess 线程亲和：created/used 同线程）。
// 内部 waitFor* 同步逻辑一字不动，只是执行线程换到 worker；GUI 侧一律经 queued 投递
// （submitAsync/qaKillChild）+ queued 信号（taskFinished）交互。同步 ready()/run()
// 仍公开——给"当前线程即 owner"的旧路径（LA_ERASE_MODEL_QA 直接构造）保持兼容。
class EraseRunner : public QObject {
    Q_OBJECT
public:
    explicit EraseRunner(QObject* parent = nullptr);
    ~EraseRunner() override;

    // 纯文件探针（findExe + 同目录 lama.onnx 校验，stub 豁免规则同前）：零 spawn、零阻塞，
    // 任意线程可调。EraseBridge::modelReady 用它，GUI 线程不再等任何进程握手。
    static bool executorAvailable();

    bool ready();                                        // spawn + await READY（阻塞；仅限 owner 线程）
    bool run(quint32 id, const QImage& base, const QImage& mask, QImage& out, QString* err);
    bool processId(qint64& pid) const;                   // 读 atomic 缓存（跨线程安全）
    void qaKillChild();
    static void qaInjectBadSize(bool on);                // QA 反例钩子（Task 5/9 用）
    static void qaSetForceNoExecutor(bool on);           // QA 钩子：令探针/findExe 判空 -> blocked_no_model
    QString exePath() const { return exePath_; }

public slots:
    // 在 owner 线程执行完整同步任务，终态经 taskFinished queued 回调用方。
    void submitAsync(quint32 id, QImage base, QImage mask);
    void shutdownOwned();                                // 终止子进程（bridge 拆除序列用）

signals:
    void taskFinished(quint32 id, bool ok, QImage out, QString err);

private:
    bool ensureStarted();
    QProcess* proc_ = nullptr;
    QString exePath_;
    bool ready_ = false;
    std::atomic<qint64> pidCache_{0};                    // worker 写（spawn/kill 后），任意线程读
};
