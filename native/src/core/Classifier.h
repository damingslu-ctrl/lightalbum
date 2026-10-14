// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 方向 C·智能分类：CLIP 本地推理子进程（clipinfer）的 C++ 门面。
// 第四轮 T8 净替换（spec §2.3）：历史外部分类服务客户端桥（QNAM + multipart POST +
// GET 探活 + 回环地址守卫）整体退役——本类不再有任何网络语义；隐私边界由
// 「根本不开端口、照片不出进程」更强地继承（原回环守卫随传输层一起消失）。
//   · probe()   : 薄转接 ClipRunner::probe = 子进程可拉起且 READY 1 且模型两件齐备
//   · classify(): 取库内「未分类且已有缩略图」的照片，逐张 REQ kind="jpg"、
//                 body=缩略图绝对路径的 UTF-8 原字节（R-75 订正口径：是路径不是图像
//                 字节；子端按同一路径读盘，与退役前 part body 的 f.readAll() 逐字相同），
//                 归一化标签后经 AiWriter 写回（单事务原子；落库恒在 GUI 线程，
//                 本函数可在 worker 线程跑——它只碰快照与子进程，不碰 Library）。
// 批次调度：batch 形参（默认 32）保留为**调度分片**粒度，进度计数语义不变；传输逐张化
//   （单张一请求一应答）。分片宽度另受 kClipMaxBatch 上限钳制（见 Classifier.cpp 的
//   R-80 第 1 条缺陷记录：降语义连续分片，不丢弃、不报错）。
//
// QA 钩子 setSyntheticForQa：非空时 classify 不触子进程、不探测，直接套用结果，
// 供 LA_CLASSIFY_QA 门禁做确定断言（写回 + 检索契约）。
#include <QString>
#include <QVector>
#include <functional>
#include "AiWriter.h"
#include "Library.h"

class Classifier {
public:
    // 探测引擎是否就绪（拉起 clipinfer + READY 1 + 模型两件齐备）。T8 起签名去掉 url
    // 形参（spec §2.3 逐字：`static bool probe()`）= ClipRunner::probe 的薄转接：
    // 底座 Rst 细分（NoExe/BlockedNoModel/Timeout/Crashed）留在门面/日志侧，本层只回
    // 「引擎可用」布尔——与退役前 ok&&loaded 语义等价。
    // ⚠ 只能在非 GUI 线程调用（底座对 GUI 线程调用响亮拒 Q_ASSERT + Busy）。
    static bool probe();

    // 给库内未分类照片批量打标并写回。
    //   photos     : 库内照片**快照**（调用方在 GUI 线程取 `QVector<Photo>` 副本后交给
    //                worker；Library 无锁，worker 绝不持 Library* 读 photos()）
    //   writer     : 写回投递器（core/AiWriter）——落库恒在 GUI 线程
    //   thumbsDir  : 缩略图目录（与 Photo.thumbFile 拼接取图）
    //   batch      : 每批调度张数（默认 32；实际分片宽度 = min(batch, kClipMaxBatch)）
    //   limit      : 最大处理张数（<0=不限；QA 用小值）
    //   progress   : (done,total,name) 进度回调（done = 实际发出并被处置的张数，
    //                张间取消不再按整片超报——T8 修复波 F-2）
    //   cancel     : 返回 true 即尽快中止（检查点=每张之间，T8 语义收窄见 .cpp）
    // 守恒：只写 category 列，不增删照片。失败即放行（坏图不中断整批）。
    // 返回值语义（T8 修复波 F-1 收紧）：= 「本轮是否全部干净写回」，**不是**「是否跑过」。
    //   任一单张可见失败（引擎/传输失败、ok!=true 不采信）或被取消中断 → false；
    //   GUI 侧据此不再对全失败/中断报「已为 N 张照片写入场景分类」的假成功。
    //   签名零变化（spec §10:267），无新增文案。
    static bool classify(const QVector<Photo>& photos, AiWriter& writer,
                         const QString& thumbsDir,
                         int batch = 32, int limit = -1,
                         std::function<void(int, int, const QString&)> progress = {},
                         std::function<bool()> cancel = {});

    // 将任意 CLIP 标签归一到 8 固定类 + 其他（中文 UI 用）。确定性、可被门禁断言。
    static QString normalizeLabel(const QString& raw);

    // ---- QA 钩子（反例/确定路径用）----
    static void setSyntheticForQa(const QVector<CatItem>& items);  // 非空→绕过子进程
    static void clearSyntheticForQa();
};
