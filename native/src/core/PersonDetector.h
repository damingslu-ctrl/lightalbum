// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 方向 C·人脸识别（方案A）：faceinfer 本地子进程人脸检测的 C++ 编排层。
// 2.40.0 第四轮 T4b：HTTP 桥（QNAM GET/POST → 本地 face_server 服务）净替换为
// FaceRunner 子进程门面（native/src/core/FaceRunner.h，底座 AiSubprocessRunner）：
//   · probe() : FaceRunner::probe() 语义——子进程可拉起 + READY 1 + 模型 sibling 齐备。
//   · detect() : 取库内「person 为空」的照片，逐张经 FaceRunner::detect（REQ kind="det"，
//                body=图像绝对路径，子进程按路径读盘，**不传图像字节**，绝不碰原图），
//                faces 非空 → 写 person=defaultPerson()（默认标签，用户后续 UI 重命名）；
//                复用 lib->setPersons 单事务写回。
// 隐私不变式：图像字节全程不出进程（子进程本机按路径读盘），与原「仅回环」守卫同旨；
//   传输层已无 URL，故无外泄面。
// 不引入聚类算法（用户裁定）：所有检测到人脸共享默认标签；人物区分与重命名
// 交给用户在 GalleryPage 人物 chips 右键完成。
//
// 线程：detect/detectEmbeddings/probe 均须在非 GUI 工作线程调用（底座对 GUI 线程
//   调用响亮拒 Q_ASSERT + Busy）；落库恒经 AiWriter 回 GUI 线程（core/AiWriter.h）。
//
// QA 钩子 setSyntheticForQa：非空时 detect 不触子进程、不探测，直接套用结果，
// 供 LA_PERSON_QA 门禁做确定断言（写回 + 检索契约）。
#include <QString>
#include <QVector>
#include <functional>
#include "AiWriter.h"
#include "Library.h"

class PersonDetector {
public:
    // 探测 faceinfer 引擎是否就绪（拉起 + READY 1 + 模型齐备）。非 GUI 线程调用。
    static bool probe();

    // 给「尚未标注人物」的照片检测人脸并写回 person。
    //   photos   : 库内照片**快照**（调用方在 GUI 线程取好副本；本函数可在 worker 线程跑，
    //              全程不碰 Library —— 见 core/AiWriter.h 的线程归属约束）
    //   writer   : 写回投递器（落库恒在 GUI 线程）
    //   limit    : 最大处理张数（<0=不限；QA 用小值）
    //   progress : (done,total,name) 进度回调
    //   cancel   : 返回 true 即尽快中止（在途请求跑完）
    // 守恒：只写 person 列，不增删照片。单张失败即放行（坏图/坏引擎不中断整批）。
    // 已标注（person 非空）的照片跳过，避免覆盖用户重命名结果。
    // 返回值：**「本轮是否全部干净跑完」**，不是「是否跑过」——probe 假、任一可见失败
    // （传输失败或应答 ok!=true）、或取消中断 → false；由 #75 G9 的 ERR 子轮咬住。
    // GUI 侧（MainWindow::runPersonDetect）据此在未就绪/中断/成功三支间如实分流。
    static bool detect(const QVector<Photo>& photos, AiWriter& writer,
                       int limit = -1,
                       std::function<void(int, int, const QString&)> progress = {},
                       std::function<bool()> cancel = {});

    // 默认人物标签（所有检测到人脸共享，用户在 UI 重命名）。
    static const QString& defaultPerson();

    // ---- 方向 C·人物自动聚类：抽取代表脸 embedding 并落库 ----
    // 遍历 person 为空照片，逐张经 FaceRunner::detect，取 faces[] 中 score 最大者 embedding
    // （512-d）→ float32 小端 BLOB，经 AiWriter 写回 face_emb 列。
    // 已标注（person 非空）照片跳过；无脸不放行错误标签、单张失败放过（不中断整批）。
    // 守恒：只写 face_emb 列。返回值语义同 detect（「本轮全部干净」，非「是否跑过」）。
    static bool detectEmbeddings(const QVector<Photo>& photos, AiWriter& writer,
                                 int limit = -1,
                                 std::function<void(int, int, const QString&)> progress = {},
                                 std::function<bool()> cancel = {});

    // ---- QA 钩子（反例/确定路径用）----
    static void setSyntheticForQa(const QVector<PersonItem>& items);  // 非空→绕过网络（detect）
    static void clearSyntheticForQa();
    // QA 合成钩子：非空时 detectEmbeddings 绕过网络，直接套用 {path, emb}（embedding 抽取）。
    static void setSyntheticEmbForQa(const QVector<FaceItem>& items);
    static void clearSyntheticEmbForQa();
};
