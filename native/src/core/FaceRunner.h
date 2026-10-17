// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
#include <QJsonDocument>
#include <QString>

#include "AiSubprocessRunner.h"

// 2.40.0 第四轮 T4：faceinfer 薄门面（spec §2.3）——PersonDetector 净替换后消费的
// 唯一入口；线程机械全在 AiSubprocessRunner 底座，本层只做 exe 候选链装配 +
// kind/ID/JSON 语义映射。EraseRunner/EraseProto 先例只作模式参照，零引用零改动。
class FaceRunner {
public:
    // probe（spec §2.3 语义）：进程可拉起且 READY 1 且模型齐备。rst 透出细分：
    // NoExe=引擎文件缺 / BlockedNoModel=模型 sibling 缺或 READY 0 保活臂 /
    // Timeout|Crashed=拉起失败。任意非 GUI 线程（GUI 线程 = 底座响亮拒 Busy）。
    static bool probe(AiSubprocessRunner::Rst* rst = nullptr);

    // detect：REQ kind="det"（T2 评审裁定 canonical；faceinfer 仍同义接受旧 "path"，
    // spec §5 已订正）body=UTF-8 图像绝对路径。out=RES JSON 解析文档（解析后即弃，
    // 无缓存表，§8 内存纪律）。返回 false = Rst≠Ok 或 JSON 破损——调用侧
    // 「失败→整张放过」分支按 Rst≠Ok 视同 !ok（spec §2.3 逐字保留该容错）。
    static bool detect(const QString& absPath, QJsonDocument& out);

    // ---- QA 专用（生产零调用）：拆掉单例底座的热子进程，使下一次 probe/detect 按
    // 调用方**当时**的进程 env 冷启动新子进程。桩的 LA_FACE_FORCE_* 模式只在子进程
    // 启动时读一次，而底座复用热子进程——同一门内要先后跑「全失败轮」与「干净轮」
    // 时必须换血，否则第二轮继承第一轮的桩模式（#75 G9 FIX-16 双臂）。零语义改动，
    // 机械见 AiSubprocessRunner::qaRetireChildForQa。----
    static void qaRetireChildForQa();

private:
    // 进程级单例底座（static-duration 故意不析构：子进程随父进程句柄关闭见 stdin
    // EOF 自然退出——faceinfer/stub 主循环同型，无孤儿驻留）。
    static AiSubprocessRunner& runner();
};
