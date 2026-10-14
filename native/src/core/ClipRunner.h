// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
#include <QJsonDocument>
#include <QString>

#include "AiSubprocessRunner.h"

// 2.40.0 第四轮 T8：clipinfer 薄门面（spec §2.3）——Classifier 的历史外部分类服务桥
// （回环端口的 multipart POST 探活/分类两路）净替换后消费的唯一入口；线程机械全在
// AiSubprocessRunner 底座，本层只做 exe 候选链装配 + kind/ID/JSON 语义映射。
// FaceRunner（T4 评审过的同型模板）只作模式参照，零引用零改动。
class ClipRunner {
public:
    // probe（spec §2.3 语义）：进程可拉起且 READY 1 且模型齐备。rst 透出细分：
    // NoExe=引擎文件缺 / BlockedNoModel=模型 sibling 缺或 READY 0 保活臂 /
    // Timeout|Crashed=拉起失败。任意非 GUI 线程（GUI 线程 = 底座响亮拒 Busy）。
    static bool probe(AiSubprocessRunner::Rst* rst = nullptr);

    // classifyOne（单张一次往返；派发单 R-18 名，spec §2.3 的「逐张化」落点）：
    // REQ kind="jpg"（clipinfer 唯一合法词，clipinfer.cpp:232 违例即 ERR badframe）
    // body=UTF-8 图像绝对路径（R-75 订正：是路径原字节，不是 JPEG 字节，spec §5）。
    // out=RES JSON 解析文档（解析后即弃，无缓存表，§8 内存纪律）。返回 false =
    // Rst≠Ok 或 JSON 破损——调用侧「失败→该张放过」分支按 Rst≠Ok 视同 !ok
    // （spec §2.3 逐字保留该容错；Classifier 侧即「坏图不中断整批」）。
    static bool classifyOne(const QString& absPath, QJsonDocument& out);

private:
    // 进程级单例底座（static-duration 故意不析构：子进程随父进程句柄关闭见 stdin
    // EOF 自然退出——clipinfer/stub 主循环同型，无孤儿驻留）。
    static AiSubprocessRunner& runner();
};
