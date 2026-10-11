// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "AiWriter.h"

#include <QMetaObject>
#include <atomic>

// 反例开关（默认关）：见 AiWriter.h 的说明。用 atomic —— 它由 GUI 线程的门禁设置、
// 由 worker 线程在 submit 里读取，裸 bool 又是一次数据竞争（那正是本文件要消灭的东西）。
static std::atomic<bool> g_forceInlineForQa{false};

void AiWriter::setForceInlineForQa(bool on) { g_forceInlineForQa.store(on); }
bool AiWriter::forceInlineForQa() { return g_forceInlineForQa.load(); }

void AiWriter::submit(const Job& job)
{
    if (!lib_ || !guiCtx_) return;
    if (g_forceInlineForQa.load()) { job(lib_); return; }   // 反例：调用线程直接写
    Library* lib = lib_;
    // context 重载：guiCtx_ 析构则任务被 Qt 丢弃（不会拿已销毁的 Library 做事后诸葛）。
    // 显式 QueuedConnection：worker 线程与 GUI 线程必然跨线程；GUI 线程自调用时也一律
    // 排队（行为只有一种，门禁断言不必猜「这次是同步还是异步」）。
    QMetaObject::invokeMethod(guiCtx_, [lib, job] { job(lib); }, Qt::QueuedConnection);
}

void AiWriter::categories(const QVector<CatItem>& items)
{
    if (items.isEmpty()) return;
    const QVector<CatItem> byVal = items;   // 按值捕获：worker 的局部批不活在投递之后
    submit([byVal](Library* lib) { lib->setCategories(byVal); });
}

void AiWriter::persons(const QVector<PersonItem>& items)
{
    if (items.isEmpty()) return;
    const QVector<PersonItem> byVal = items;
    submit([byVal](Library* lib) { lib->setPersons(byVal); });
}

void AiWriter::faceEmbeddings(const QVector<FaceItem>& items)
{
    if (items.isEmpty()) return;
    const QVector<FaceItem> byVal = items;
    submit([byVal](Library* lib) { lib->setFaceEmbeddings(byVal); });
}

void AiWriter::clusters(const QVector<ClusterItem>& items)
{
    if (items.isEmpty()) return;
    const QVector<ClusterItem> byVal = items;
    submit([byVal](Library* lib) { lib->applyClusters(byVal); });
}
