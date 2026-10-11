// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 2.33.0 线程安全：AI 异步任务（智能分类 / 人脸检测 / 人物聚类）的**唯一写回入口**。
//
// 修复前的真实竞态（三处同形）：MainWindow 把 `Library*` 直接交给 QtConcurrent worker，
// worker 里调 `lib->setCategories(...)` / `setPersons(...)` / `setFaceEmbeddings(...)` /
// `applyClusters(...)`。两条同时成立：
//   ① `la_rw` 这条 QSqlDatabase 连接由 GUI 线程惰性创建，SQLite 句柄不可跨线程使用
//      （Qt 明令：连接与其使用线程绑定），并发使用是 UB；
//   ② 这些 mutator 在 COMMIT 后同步内存副本 `photos_`/`pathIx_`，而 GalleryModel 正按
//      `const QVector<Photo>*` 引用读它们 → 撕裂读 / 索引错位。
// 加锁救不了 ②：模型手里是引用，锁管不到它解引用的那一刻。所以正解是**单一写者**：
// worker 只算（HTTP/解码本来就线程局部），落库一律投递回 GUI 线程执行。
//
// 用法约束（铁律 6/21 的线程版）：
//   · 构造在 GUI 线程：`guiCtx` 必须是所在线程即目标写线程的 QObject（生产里传 MainWindow this）；
//   · `submit*` 可在任意线程调用；
//   · `guiCtx` 析构后 Qt 自动丢弃排队任务（invokeMethod 的 context 语义），故 `lib_` 随
//     MainWindow 一起销毁也不会悬垂 —— 这是把 context 而不是裸函数排队的原因。
#include <QObject>
#include <QVector>
#include <functional>
#include "Library.h"

class AiWriter {
public:
    AiWriter(Library* lib, QObject* guiCtx) : lib_(lib), guiCtx_(guiCtx) {}

    // 空批不投递：与 Library 各 mutator 的「空批=成功空操作」语义一致，省一次排队。
    void categories(const QVector<CatItem>& items);
    void persons(const QVector<PersonItem>& items);
    void faceEmbeddings(const QVector<FaceItem>& items);
    void clusters(const QVector<ClusterItem>& items);

    // ★QA 反例钩子：让 submit* 在**调用线程**当场落库 —— 即修复前的形态。
    // 它只改变「写在哪个线程」，不改变任何期望值（铁律 22）；门禁的期望恒为 GUI 线程。
    static void setForceInlineForQa(bool on);
    static bool forceInlineForQa();

private:
    using Job = std::function<void(Library*)>;
    void submit(const Job& job);

    Library* lib_ = nullptr;
    QObject* guiCtx_ = nullptr;
};
