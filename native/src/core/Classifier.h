// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 方向 C·智能分类：CLIP 本地服务的 C++ 客户端桥。
// 协议严格对齐历史 Python 端 client/lightalbum/core/classifier.py：
//   · probe()   : GET {url}/health，仅回环地址（assert_local），返回 {ok&&loaded}
//   · classify(): 取库内「未分类且已有缩略图」的照片，POST multipart 到 /classify
//                 （字段名 files，batch=32，绝不碰原图），按 results[i] 映射回 path，
//                 归一化标签后写回 lib->setCategories（单事务原子）。
// 本地守卫：url 仅允许 127.0.0.1 / localhost / ::1，防止照片外泄。
//
// QA 钩子 setSyntheticForQa：非空时 classify 不走网络、不探测，直接套用结果，
// 供 LA_CLASSIFY_QA 门禁做确定断言（写回 + 检索契约）。
#include <QString>
#include <QVector>
#include <functional>
#include "Library.h"

class Classifier {
public:
    // 探测服务是否就绪（仅回环）。返回 ok && loaded。
    static bool probe(const QString& url = QStringLiteral("http://127.0.0.1:8766"));

    // 给库内未分类照片批量打标并写回。
    //   lib        : 本机库（读 photos()，写 setCategories）
    //   thumbsDir  : 缩略图目录（与 Photo.thumbFile 拼接取图）
    //   url        : CLIP 服务基址（默认回环 8766）
    //   batch      : 每批缩略图数（默认 32）
    //   limit      : 最大处理张数（<0=不限；QA 用小值）
    //   progress   : (done,total,name) 进度回调
    //   cancel     : 返回 true 即尽快中止（在途批次跑完）
    // 守恒：只写 category 列，不增删照片。失败即放行（坏图不中断整批）。
    static bool classify(Library* lib, const QString& thumbsDir,
                         const QString& url = QStringLiteral("http://127.0.0.1:8766"),
                         int batch = 32, int limit = -1,
                         std::function<void(int, int, const QString&)> progress = {},
                         std::function<bool()> cancel = {});

    // 将任意 CLIP 标签归一到 8 固定类 + 其他（中文 UI 用）。确定性、可被门禁断言。
    static QString normalizeLabel(const QString& raw);

    // ---- QA 钩子（反例/确定路径用）----
    static void setSyntheticForQa(const QVector<CatItem>& items);  // 非空→绕过网络
    static void clearSyntheticForQa();
};
