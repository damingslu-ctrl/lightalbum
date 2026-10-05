// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
// 方向 C·人脸识别（方案A）：face_server 本地服务的 C++ 客户端桥（端口 8767）。
// 协议严格对齐 D:/lightalbum_ai/face_server.py：
//   · probe() : GET {url}/health，仅回环地址（isLocalUrl 守卫），返回 {ok&&loaded}
//   · detect() : 取库内「person 为空」的照片，逐张 POST JSON {"path":p.path}
//                到 {url}/face/detect（按路径读盘，**不传图像字节**，绝不碰原图），
//                faces 非空 → 写 person=defaultPerson()（默认标签，用户后续 UI 重命名）；
//                复用 lib->setPersons 单事务写回。
// 本地守卫：url 仅允许 127.0.0.1 / localhost / ::1，防止照片外泄。
// 不引入聚类算法（用户裁定）：所有检测到人脸共享默认标签；人物区分与重命名
// 交给用户在 GalleryPage 人物 chips 右键完成。
//
// QA 钩子 setSyntheticForQa：非空时 detect 不走网络、不探测，直接套用结果，
// 供 LA_PERSON_QA 门禁做确定断言（写回 + 检索契约）。
#include <QString>
#include <QVector>
#include <functional>
#include "Library.h"

class PersonDetector {
public:
    // 探测人脸服务是否就绪（仅回环）。返回 ok && loaded。
    static bool probe(const QString& url = QStringLiteral("http://127.0.0.1:8767"));

    // 给库内「尚未标注人物」的照片检测人脸并写回 person。
    //   lib        : 本机库（读 photos()，写 setPersons）
    //   url        : face_server 基址（默认回环 8767）
    //   limit      : 最大处理张数（<0=不限；QA 用小值）
    //   progress   : (done,total,name) 进度回调
    //   cancel     : 返回 true 即尽快中止（在途请求跑完）
    // 守恒：只写 person 列，不增删照片。失败即放行（坏图不中断整批）。
    // 已标注（person 非空）的照片跳过，避免覆盖用户重命名结果。
    static bool detect(Library* lib,
                       const QString& url = QStringLiteral("http://127.0.0.1:8767"),
                       int limit = -1,
                       std::function<void(int, int, const QString&)> progress = {},
                       std::function<bool()> cancel = {});

    // 默认人物标签（所有检测到人脸共享，用户在 UI 重命名）。
    static const QString& defaultPerson();

    // ---- QA 钩子（反例/确定路径用）----
    static void setSyntheticForQa(const QVector<PersonItem>& items);  // 非空→绕过网络
    static void clearSyntheticForQa();
};
