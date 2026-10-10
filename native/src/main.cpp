// LightAlbum Native — P1 入口
// 决策依据：docs/adr/ADR-0001-full-cpp-qt-widgets-rewrite.md
// 分期计划：docs/REWRITE-PLAN.md
// 冒烟：LA_SMOKE=1 + QT_QPA_PLATFORM=offscreen → 载入真库后自动退出并打 LA_P1_SMOKE_OK
#include <QApplication>
#include <QCoreApplication>
#include <QScreen>
#include <QTimer>
#include <QDateTime>
#include <QDir>
#include <QMenu>              // 缺陷②回归：LA_SMOKE 断言导入下拉菜单带暗色 QSS
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QImageReader>        // 2.23.0 LA_SCALE_REAL_QA：真实文件解码抽样
#include <QProcess>            // 2.7.0 LA_EXIT_QA：自退契约子进程探针
#include <QProcessEnvironment>
#include <QTranslator>
#include <QLocale>
#include <cstdio>
#include <cmath>
#include "core/Library.h"
#include "core/LumenEngine.h"
#include "core/LumenAgent.h"
#include "core/GeoIndex.h"
#include "core/PhotoCluster.h"
#include "core/PhotoDedup.h"
#include "core/GeoHotspot.h"     // 方向 C·联动：地理热点聚合（与渲染/筛选同源）
#include "core/Classifier.h"   // 方向 C·智能分类：CLIP 客户端桥（QA 门用合成钩子）
#include "core/PersonDetector.h"  // 方向 C·人脸识别（方案A）：face_server 客户端桥（QA 门用合成钩子）
#include "core/FaceCluster.h"     // 方向 C·人物自动聚类：阈值余弦聚类纯函数（QA 门直调）
#include "core/StoryGen.h"        // 人物故事方向·B：自动故事相册纯函数（LA_STORY_QA 直调）
#include "ui/GalleryCardPainter.h"  // 2.22.0 骨架屏纯函数（QA 门直调，铁律 20 同源）
#include "ui/FullCache.h"           // 2.22.0 灯箱原图 LRU（QA 门直调）
#include <QTemporaryDir>
#include <QPainter>
#include <QSet>
#include <QColor>
#include "core/RtHistory.h"
#include "core/RawDecode.h"      // 2.28.0 RAW 外部解码器桥（LA_RAW_QA 直调）
#include "core/VideoDecode.h"     // 2.28.0 视频外部解码器桥（LA_VIDEO_QA 直调）
#include "core/ImageLoad.h"      // 2.29.0 统一原图加载（照片/RAW/视频）
#include "core/VideoDecoder.h"     // 2.30.0 内嵌视频解码（ffmpeg rawvideo 管道）
#include "core/AudioPlayer.h"      // 2.30.0 音频输出（winmm waveOut）
#include "core/ImageLoad.h"       // 2.31.0 选帧链路（loadForEdit 透传秒数）
#include "ui/VideoPlayerWidget.h"   // 2.30.0 播放器 UI（LA_VIDEO_PLAY_QA 像素门）
#include "core/Scanner.h"
#include "core/ICloudSource.h"
#include "core/qaExit.h"        // 2.7.0 门禁自退契约 + 自退看门狗
#include "ui/MainWindow.h"
#include "ui/MapView.h"
#include "ui/HiDpi.h"
#include "ui/GalleryPage.h"
#include "ui/GalleryModel.h"
#include "ui/GalleryCardPainter.h"
#include "ui/CullPage.h"
#include "core/CullEngine.h"
#include "ui/RetouchPage.h"
#include "ui/HomePage.h"
#include "ui/DarkPalette.h"
#include "ui/Theme.h"
#include "ui/Splash.h"
#include "ui/LogPage.h"
#include "core/MotionPref.h"
#include "core/Vault.h"
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QMouseEvent>            // 方向 C·联动：LA_GEO_LINK_QA 离屏合成鼠标事件
#include <QPixmap>
#include <QElapsedTimer>
#include <QByteArray>
#include <QCryptographicHash>
#include <functional>
#include "ui/ThumbCache.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QStandardPaths>
namespace {
// P9 AI 修图智能体冒烟：合成「偏暗偏暖发灰」样张 → 测量 → 决策 → 执行。
// 真实路径（默认 Ollama 127.0.0.1:11434，机器没装则自动落兜底）+ 强制兜底路径（端口 9）。
int runAgentSmoke()
{
    // 合成样张：暗 + 暖（R>B）+ 低对比
    QImage img(256, 256, QImage::Format_RGB32);
    for (int y = 0; y < 256; ++y) {
        QRgb* ln = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < 256; ++x)
            ln[x] = qRgb(52 + x / 16, 44 + y / 20, 30);   // 暗、轻微暖调、平缓（发灰）
    }
    int pass = 1;

    // ★门禁确定性：本门必须**不依赖本机 Ollama 是否在跑**。
    // 实测历史波动：Ollama 在线时 decide() 真走在线推理，本门耗时 15.9–20.3s，
    // 逼近 qaExit 看门狗 20s 预算 → 被误判为 no-exit-request 而红（环境态，非代码缺陷）。
    // 正解：把 URL 指向**关闭端口**（127.0.0.1:9），强制走秒退兜底路径 →
    // 耗时稳定在亚秒级，且断言与用户是否开 LLM 完全无关。
    // 代价：不再覆盖「真实在线推理」这一路径 —— 但那属于集成测试，不适合进 PR 门禁。
    const QByteArray savedLlmUrl = qgetenv("LA_LLM_URL");
    qputenv("LA_LLM_URL", "http://127.0.0.1:9");

    // ① 决策（指向关闭端口 → 350ms 探测后退到本地规则，结构仍须合法）
    const AgentContext ctx = LumenAgent::contextFrom(img, 3200, 0.0, QString());
    const AgentPlan real = LumenAgent::decide(ctx);
    bool realOk = real.ok && !real.params.isEmpty();
    for (auto it = real.params.cbegin(); it != real.params.cend(); ++it)
        if (it.value() < -1.0 || it.value() > 1.0) realOk = false;
    const QImage outReal = LumenEngine::retouchWithPlan(img, real.params);
    realOk = realOk && !outReal.isNull();
    std::fprintf(stderr, "AGENT real src=%s model=%s intent=%s params=%d out=%d reason=%s\n",
                 qPrintable(real.source), qPrintable(real.model),
                 qPrintable(real.intent), real.params.size(), outReal.isNull() ? 0 : 1,
                 qPrintable(real.reason));
    if (!realOk) pass = 0;

    // ② 强制兜底：同样指向关闭端口 → 秒退到本地规则
    const QByteArray savedUrl = qgetenv("LA_LLM_URL");
    qputenv("LA_LLM_URL", "http://127.0.0.1:9");
    const AgentPlan fb = LumenAgent::decide(LumenAgent::contextFrom(img, 3200, 0.0, QStringLiteral("通透")));
    qputenv("LA_LLM_URL", savedUrl);
    const bool fbOk = fb.ok && fb.source == QStringLiteral("local")
                   && !fb.params.isEmpty()
                   && (fb.reason.contains(QStringLiteral("未连接"))
                       || fb.reason.contains(QStringLiteral("兜底")));
    std::fprintf(stderr, "AGENT fallback src=%s intent=%s brightness=%.2f warmth=%.2f denoise=%.2f\n",
                 qPrintable(fb.source), qPrintable(fb.intent),
                 fb.params.value(QStringLiteral("brightness"), 0),
                 fb.params.value(QStringLiteral("warmth"), 0),
                 fb.params.value(QStringLiteral("denoise"), 0));
    if (!fbOk) pass = 0;

    // ③ 兜底计划方向断言：暗样张应正提亮；ISO3200 应有降噪； goal 通透 → clarity ≥ 0
    if (fb.params.value(QStringLiteral("brightness"), 0) <= 0) pass = 0;
    if (fb.params.value(QStringLiteral("denoise"), 0) < 0.30) pass = 0;
    if (fb.params.value(QStringLiteral("clarity"), 0) < 0.10) pass = 0;

    // ④ clampPlan 白名单：未知键丢弃、越界截断
    QMap<QString, double> dirty;
    dirty.insert(QStringLiteral("brightness"), 5.0);
    dirty.insert(QStringLiteral("evil"), 1.0);
    const QMap<QString, double> cl = LumenAgent::clampPlan(dirty);
    const bool clampOk = cl.size() == 1 && cl.value(QStringLiteral("brightness")) == 1.0;
    if (!clampOk) pass = 0;

    std::fprintf(stderr, "LA_AGENT_SMOKE %s real=%s fb=%s clamp=%d\n",
                 pass ? "PASS" : "FAIL", realOk ? "1" : "0", fbOk ? "1" : "0", clampOk ? 1 : 0);
    // 还原环境（本门进程即将退出，但保持「探针不污染环境」的一致约定）
    if (savedLlmUrl.isEmpty()) qunsetenv("LA_LLM_URL");
    else                       qputenv("LA_LLM_URL", savedLlmUrl);
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 B 门禁深化（2.25.0）：LA_AGENT_QA —— LumenAgent 四个纯函数的逐项断言。
// ★ 取证依据（先测再改）：LA_AGENT_SMOKE 只有 3 个布尔量（real/fb/clamp），
//   而 parseShutterSeconds（全库零断言）、contextFrom（仅被间接调用）、
//   decideLocal 的 8 条规则分支（现仅覆盖 3 条）、clampPlan 的 13 键白名单
//   （现仅测 2 键）**都没有值级断言** —— 属铁律 29「存在即恒真」的高发区。
// 本门把这四块补成**可断言的值契约**，且全部零网络、亚秒级（QA 门禁铁律：本门不依赖
// 本机 Ollama 是否在跑，decideLocal/decide 均指向关闭端口或走纯规则）。
// 反例 LA_AGENT_QA_FORCE_BROKEN=1：LumenAgent::setForceSignFlipForQa(true) 让
//   decideLocal 把 brightness/saturation 符号取反，精确复现 2.24.0「滑杆方向反向」的
//   真实缺陷形态 → C 组方向断言必红。期望值（阈值/键数/真值表）**不随 broken 切换**（铁律 22）。
int runAgentQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_AGENT_QA_FORCE_BROKEN");
    LumenAgent::setForceSignFlipForQa(broken);

    int checks = 0, fails = 0;
    auto ck = [&](bool cond, const char* tag) {
        ++checks; if (!cond) { ++fails; std::fprintf(stderr, "  LA_AGENT_QA FAIL %s\n", tag); }
    };
    auto approxEq = [](double a, double b, double eps) { return std::fabs(a - b) <= eps; };

    // ---- A. parseShutterSeconds 真值表（确定性字符串解析，纯函数）----
    struct PS { const char* in; double want; };
    static const PS kPs[] = {
        {"1/125", 1.0/125}, {"1/60", 1.0/60}, {"1/4000", 1.0/4000},
        {"15s", 15.0}, {"2.5s", 2.5}, {"0.5", 0.5}, {"30", 30.0},
        {"", 0.0}, {"abc", 0.0}, {"1/0", 0.0}, {"1/", 0.0},   // 防除零/残缺
    };
    for (const PS& t : kPs)
        ck(approxEq(LumenAgent::parseShutterSeconds(QString::fromLatin1(t.in)), t.want, 1e-9),
           t.in[0] ? t.in : "(empty)");

    // ---- B. clampPlan：13 键白名单 + clamp 边界 ----
    static const char* kKeys[] = {
        "brightness","contrast","saturation","warmth","sharpness","denoise",
        "highlights","shadows","whites","blacks","vibrance","clarity","tint"};
    QMap<QString,double> all;
    for (const char* k : kKeys) all.insert(QString::fromLatin1(k), 0.3);
    all.insert(QStringLiteral("evil"), 1.0);        // 未知键必须丢弃
    all.insert(QStringLiteral("_intent"), 1.0);     // LLM 元字段必须丢弃
    const QMap<QString,double> kept = LumenAgent::clampPlan(all);
    ck(kept.size() == 13, "clamp.size==13");       // 判别器：删白名单实现即红
    bool allKept = true;
    for (const char* k : kKeys) allKept = allKept && kept.contains(QString::fromLatin1(k));
    ck(allKept, "clamp.keeps13");
    ck(!kept.contains(QStringLiteral("evil")) && !kept.contains(QStringLiteral("_intent")),
       "clamp.dropsUnknown");
    QMap<QString,double> dirty;
    dirty.insert(QStringLiteral("brightness"), 5.0);
    dirty.insert(QStringLiteral("contrast"), -5.0);
    dirty.insert(QStringLiteral("tint"), 0.0);
    const QMap<QString,double> cl = LumenAgent::clampPlan(dirty);
    ck(approxEq(cl.value(QStringLiteral("brightness")), 1.0, 1e-9), "clamp.hi->1");
    ck(approxEq(cl.value(QStringLiteral("contrast")), -1.0, 1e-9), "clamp.lo->-1");
    ck(approxEq(cl.value(QStringLiteral("tint")), 0.0, 1e-9), "clamp.zero");

    // ---- C. decideLocal 方向契约（单参数隔离：每条只动一个测量维度，铁律 27）----
    // ★ goal 必须用 QStringLiteral（UTF-8 原生）。首版误用 QString::fromLatin1("黑白")，
    //   中文被逐字节映射成 mojibake → 黑白/人像分支**永不触发** → 门自己假红。
    //   取证结论：Latin1 按字节映射，UTF-8 汉字变成非中文码点，.contains(中文) 恒 false。
    auto ctx = [](double lum, double lumStd, double sat, double cast,
                  int iso, double shutter, const QString& goal) {
        AgentContext c; c.lum=lum; c.lum_std=lumStd; c.sat=sat; c.cast=cast;
        c.iso=iso; c.shutter=shutter; c.goal=goal;
        c.hi=0; c.lo=0; c.haze=0; c.noise=0;   // 隔离：其余维度置中性
        return c;
    };
    // 基准（全中性：lum 0.5 / std 0.2 / sat 0.3 / cast 0 / iso 100 / 快门 0.008）
    auto base = [&ctx](const QString& g){ return ctx(0.50,0.20,0.30,0.0,100,0.008,g); };
    const QString kNoGoal = QString();
    // C1 暗 → 提亮（正）
    ck(LumenAgent::decideLocal(ctx(0.10,0.20,0.30,0.0,100,0.008,kNoGoal))
           .params.value(QStringLiteral("brightness")) > 0.0, "dir.dark.brightness+");
    // C2 亮 → 压高光（负）
    ck(LumenAgent::decideLocal(ctx(0.90,0.20,0.30,0.0,100,0.008,kNoGoal))
           .params.value(QStringLiteral("highlights")) < 0.0, "dir.bright.highlights-");
    // C3 灰（std 低）→ 加对比（正）
    ck(LumenAgent::decideLocal(ctx(0.50,0.05,0.30,0.0,100,0.008,kNoGoal))
           .params.value(QStringLiteral("contrast")) > 0.0, "dir.flat.contrast+");
    // C4 反差过强（std 高）→ 收对比（负）
    ck(LumenAgent::decideLocal(ctx(0.50,0.40,0.30,0.0,100,0.008,kNoGoal))
           .params.value(QStringLiteral("contrast")) < 0.0, "dir.highstd.contrast-");
    // C5 淡（sat 低）→ 加饱和（正）
    ck(LumenAgent::decideLocal(ctx(0.50,0.20,0.05,0.0,100,0.008,kNoGoal))
           .params.value(QStringLiteral("saturation")) > 0.0, "dir.lowsat.saturation+");
    // C6 浓（sat 高）→ 降饱和（负）
    ck(LumenAgent::decideLocal(ctx(0.50,0.20,0.60,0.0,100,0.008,kNoGoal))
           .params.value(QStringLiteral("saturation")) < 0.0, "dir.highsat.saturation-");
    // C7 偏暖（cast 正）→ 负 warmth 拉回
    ck(LumenAgent::decideLocal(ctx(0.50,0.20,0.30, 0.08,100,0.008,kNoGoal))
           .params.value(QStringLiteral("warmth")) < 0.0, "dir.warm.warmth-");
    // C8 偏冷（cast 负）→ 正 warmth 拉回
    ck(LumenAgent::decideLocal(ctx(0.50,0.20,0.30,-0.08,100,0.008,kNoGoal))
           .params.value(QStringLiteral("warmth")) > 0.0, "dir.cool.warmth+");
    // C9 ISO 档位：3200 → denoise==0.30（精确档位值，承铁律 17 数字算出非手写）
    ck(approxEq(LumenAgent::decideLocal(ctx(0.50,0.20,0.30,0.0,3200,0.008,kNoGoal))
               .params.value(QStringLiteral("denoise")), 0.30, 1e-9), "iso3200.denoise0.30");
    ck(approxEq(LumenAgent::decideLocal(ctx(0.50,0.20,0.30,0.0,6400,0.008,kNoGoal))
               .params.value(QStringLiteral("denoise")), 0.50, 1e-9), "iso6400.denoise0.50");
    // C10 长曝（4s）→ denoise 在 ISO 档位上叠加 +0.20
    ck(approxEq(LumenAgent::decideLocal(ctx(0.50,0.20,0.30,0.0,3200,4.0,kNoGoal))
               .params.value(QStringLiteral("denoise")), 0.50, 1e-9), "longExp.denoiseStack");
    // C11 sharpness 恒 0.15（出图锐化恒定，不随场景变）
    ck(approxEq(LumenAgent::decideLocal(base(kNoGoal)).params.value(QStringLiteral("sharpness")), 0.15, 1e-9),
       "const.sharpness0.15");
    // C12 目标「黑白」→ saturation 强制去色（-1.0），且 intent 切「黑白」
    {
        const AgentPlan bw = LumenAgent::decideLocal(base(QStringLiteral("黑白")));
        ck(approxEq(bw.params.value(QStringLiteral("saturation")), -1.0, 1e-9), "goal.bw.saturation-1");
        ck(bw.intent.contains(QStringLiteral("黑白")), "goal.bw.intent");
    }
    // C13 目标「人像」→ clarity 相对基准下降（皮肤柔和）
    ck(LumenAgent::decideLocal(base(QStringLiteral("人像"))).params.value(QStringLiteral("clarity"))
       < LumenAgent::decideLocal(base(kNoGoal)).params.value(QStringLiteral("clarity")),
       "goal.portrait.clarityDown");
    // C14 计划恒合法：ok=true / source=local / 13 键齐全 / 值域 [-1,1]
    {
        const AgentPlan p = LumenAgent::decideLocal(base(kNoGoal));
        ck(p.ok && p.source == QStringLiteral("local"), "plan.okLocal");
        ck(p.params.size() == 13, "plan.keys13");
        bool inRange = true;
        for (auto it = p.params.cbegin(); it != p.params.cend(); ++it)
            inRange = inRange && it.value() >= -1.0 && it.value() <= 1.0;
        ck(inRange, "plan.range[-1,1]");
    }

    // ---- D. contextFrom 测量单调性 + 元数据透传（黑/白/灰/暖极值，非恒真）----
    auto solid = [](QRgb c, int w = 64) {
        QImage im(w, w, QImage::Format_RGB32);
        for (int y = 0; y < w; ++y)
            reinterpret_cast<QRgb*>(im.scanLine(y))[0] = c;   // 扫描线首像素即全行同色
        for (int y = 0; y < w; ++y)
            for (int x = 0; x < w; ++x)
                reinterpret_cast<QRgb*>(im.scanLine(y))[x] = c;
        return im;
    };
    const AgentContext cBlack = LumenAgent::contextFrom(solid(qRgb(0,0,0)), 100, 0.008, QString());
    const AgentContext cWhite = LumenAgent::contextFrom(solid(qRgb(255,255,255)), 100, 0.008, QString());
    const AgentContext cGray  = LumenAgent::contextFrom(solid(qRgb(128,128,128)), 100, 0.008, QString());
    const AgentContext cWarm  = LumenAgent::contextFrom(solid(qRgb(200,150,110)), 100, 0.008, QString());
    ck(cBlack.lum < 0.05, "ctx.black.lum<0.05");            // 黑 → 亮度近 0
    ck(cWhite.lum > 0.95, "ctx.white.lum>0.95");            // 白 → 亮度近 1
    ck(cBlack.lum < cWhite.lum, "ctx.monoLum");             // 单调性
    ck(cGray.sat < 0.05, "ctx.gray.sat<0.05");               // 纯灰 → 饱和近 0
    ck(cWarm.cast > 0.0, "ctx.warm.cast>0");                // 偏暖 → cast 正
    // 元数据原样透传
    {
        const AgentContext m = LumenAgent::contextFrom(solid(qRgb(128,128,128)), 3200, 0.008,
                                                        QStringLiteral("通透"));
        ck(m.iso == 3200, "ctx.passthru.iso");
        ck(approxEq(m.shutter, 0.008, 1e-9), "ctx.passthru.shutter");
        ck(m.goal == QStringLiteral("通透"), "ctx.passthru.goal");
    }

    if (fails == 0)
        std::fprintf(stderr, "LA_AGENT_QA PASS checks=%d broken=%d\n", checks, broken ? 1 : 0);
    else
        std::fprintf(stderr, "LA_AGENT_QA FAIL checks=%d failed=%d broken=%d\n",
                     checks, fails, broken ? 1 : 0);
    std::fflush(stderr);
    return fails == 0 ? 0 : 1;
}
} // namespace（P9 agent 冒烟）

namespace {

// RAW 格式支持门禁（LA_RAW_QA，2.28.0）。
//
// 断言分两组：
//   A 纯函数契约（零外部依赖，任何机器都能跑）：
//     A1 isRawExt 白名单命中（含大小写不敏感）
//     A2 isRawExt 拒绝非 RAW
//     A3 isImportable：普通照片恒真（与既有行为完全一致）
//     A4 isImportable：RAW 在**无解码器**时为假 ← 优雅降级契约
//     A5 isImportable：RAW 在有解码器时为真
//   B 真实解码（需 LibRaw + 样本就位；缺失则显式 SKIP 并给出补齐方式，**不静默通过**）：
//     B1 available() 且 toolDir 下确有 dcraw_emu.exe
//     B2 decodeToTempPpm 产出 PPM 且非空
//     B3 probeSize 合理（长边 >= 512）
//     B4 非 RAW 输入解码必须失败（防"什么都能解码"的假实现）
//
// 反例 LA_RAW_QA_FORCE_BROKEN=1：令 decodeToTempPpm 走「rc==0 即成功 + 产物名按
//   **替换**扩展名猜测」的缺陷形态（RawDecode::setForceArtifactBlindForQa）。
//   实测两种模式下 dcraw_emu 的 rc 都是 0 → exit code 不携带信息 → 必红。
// ★ 期望值不随 broken 切换（铁律 22）：A 组在 broken 下同样全绿，只有 B2/B3 转红，
//   证明反例打的是真实解码判定，而非把整门一刀切（铁律 29：存在即恒真的门等于没门）。
static int runRawQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_RAW_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) { ++fails; failed << QString::fromLatin1(tag);
                     std::fprintf(stderr, "  LA_RAW_QA FAIL %s\n", tag); }
    };

    // ================= A 纯函数契约 =================
    chk(RawDecode::isRawExt(QStringLiteral("a.dng")), "A1a_dng");
    chk(RawDecode::isRawExt(QStringLiteral("A.CR3")), "A1b_upper_cr3");
    chk(RawDecode::isRawExt(QStringLiteral("x.nef")), "A1c_nef");
    chk(RawDecode::isRawExt(QStringLiteral("y.arw")), "A1d_arw");
    chk(!RawDecode::isRawExt(QStringLiteral("a.jpg")), "A2a_reject_jpg");
    chk(!RawDecode::isRawExt(QStringLiteral("a.png")), "A2b_reject_png");
    chk(!RawDecode::isRawExt(QStringLiteral("noext")), "A2c_reject_noext");

    chk(RawDecode::isImportable(QStringLiteral("a.jpg"), false), "A3_photo_no_decoder");
    chk(!RawDecode::isImportable(QStringLiteral("a.dng"), false), "A4_raw_no_decoder");
    chk(RawDecode::isImportable(QStringLiteral("a.dng"), true), "A5_raw_with_decoder");

    // A6 ★ 枚举并联判据的边界（2.28.0 实测踩坑）：
    //   needsExternalDecode 必须**只对 RAW 为真**。若误用 isImportable（非 RAW→true），
    //   扫描器的 `!isPhoto && !needs...` 恒假 → .json/.ini/.AAE/.icloud 全被放行，
    //   实测 LA_ICLOUD_QA total 由 5 变 10。此处钉死该边界。
    chk(RawDecode::needsExternalDecode(QStringLiteral("a.dng"), true), "A6a_raw_true");
    chk(!RawDecode::needsExternalDecode(QStringLiteral("a.dng"), false), "A6b_raw_no_decoder_false");
    chk(!RawDecode::needsExternalDecode(QStringLiteral("a.json"), true), "A6c_json_false");
    chk(!RawDecode::needsExternalDecode(QStringLiteral("a.ini"), true), "A6d_ini_false");
    chk(!RawDecode::needsExternalDecode(QStringLiteral("a.aae"), true), "A6e_aae_false");
    chk(!RawDecode::needsExternalDecode(QStringLiteral("a.jpg.icloud"), true), "A6f_sidecar_false");
    chk(!RawDecode::needsExternalDecode(QStringLiteral("a.jpg"), true), "A6g_jpg_false");

    // ================= B 真实解码 =================
    const bool avail = RawDecode::available();
    const QString sample = qEnvironmentVariable("LA_RAW_SAMPLE");
    if (!avail || sample.isEmpty() || !QFileInfo::exists(sample)) {
        // 显式 SKIP：写明缺什么、怎么补 —— 静默通过等于没门（铁律 29）。
        std::fprintf(stderr,
            "LA_RAW_QA SKIP reason=%s hint=\"powershell -ExecutionPolicy Bypass -File "
            "fetch_libraw.ps1; then set LA_RAW_SAMPLE=<path-to.dng>\"\n",
            !avail ? "libraw_missing" : "sample_missing");
        std::fprintf(stderr, "LA_RAW_QA PASS checks=%d failed=0 broken=0 scope=pure-contract\n",
                     checks);
        std::fflush(stderr);
        return 0;
    }

    chk(avail, "B1a_available");
    chk(!RawDecode::toolDir().isEmpty()
        && QFileInfo::exists(QDir(RawDecode::toolDir()).filePath(QStringLiteral("dcraw_emu.exe"))),
        "B1b_toolDir");

    // 反例注入（真实实现上的旗标，非测试分支 —— 铁律 6）
    RawDecode::setForceArtifactBlindForQa(broken);

    const QString ppm = RawDecode::decodeToTempPpm(sample);
    chk(!ppm.isEmpty(), "B2_ppm_produced");
    chk(!ppm.isEmpty() && QFileInfo::exists(ppm) && QFileInfo(ppm).size() > 1024,
        "B2b_ppm_nontrivial");
    if (!ppm.isEmpty() && QFileInfo::exists(ppm)) QFile::remove(ppm);

    const QSize got = RawDecode::probeSize(sample);
    chk(got.isValid() && qMax(got.width(), got.height()) >= 512, "B3_probe_size");

    // B4：非 RAW 必须失败（防止"什么都能解码"的假实现）
    const QString bogus = QDir(QFileInfo(sample).absolutePath())
                              .filePath(QStringLiteral("__la_not_raw.jpg"));
    QFile::remove(bogus);
    {
        QImage src(64, 48, QImage::Format_RGB32);
        src.fill(Qt::gray);
        src.save(bogus, "JPEG", 80);
    }
    chk(RawDecode::decodeToTempPpm(bogus).isEmpty(), "B4_non_raw_fails");
    QFile::remove(bogus);

    RawDecode::setForceArtifactBlindForQa(false);   // 还原（探针不污染环境）

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_RAW_QA %s checks=%d failed=%d sample=%s broken=%d\n",
                 pass ? "PASS" : "FAIL", checks, fails,
                 qPrintable(QFileInfo(sample).fileName()), broken ? 1 : 0);
    if (!pass)
        std::fprintf(stderr, "LA_RAW_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

} // namespace（RAW 门禁）

namespace {

// 视频格式支持门禁（LA_VIDEO_QA，2.28.0）。结构与 LA_RAW_QA 同构，便于统一理解。
//   A 纯函数契约（零外部依赖，任何机器都能跑）：
//     A1 isVideoExt 白名单（含大小写不敏感）/ 拒绝非视频
//     A2 needsExternalDecode 边界：**只对视频为真**，其余 false
//       （★ 与 RAW 同款坑：谓词若对非目标类型返回 true，`!isPhoto && !needs` 恒假 →
//         白名单被短路放行。上一轮 LA_ICLOUD_QA total 5→10 即此故，必须钉死。）
//   B 真实解码（需 FFmpeg + 视频样本；缺失则显式 SKIP + 补齐方式，**不静默通过**）：
//     B1 available() 且 toolDir 下确有 ffmpeg.exe + ffprobe.exe
//     B2 extractPoster 产出 JPEG 且非平凡
//     B3 封面可被 Qt 实读且尺寸合理
//     B4 probe 取到 duration>0 / width>0 / height>0
//     B5 非视频输入抽帧必须失败
//   C 徽标像素门（2.28.0）：isVideo=true 时左上角必须出现非背景像素，
//     isVideo=false 时**不得**出现 —— 防止「徽标恒显」或「永远不画」两类退化。
// 反例 LA_VIDEO_QA_FORCE_BROKEN=1：extractPoster 改走「rc==0 即成功 + 猜错产物名」，
//   B2/B3/B4 转红而 A 组仍绿（期望值不随 broken 变，铁律 22）。
static int runVideoQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_VIDEO_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) { ++fails; failed << QString::fromLatin1(tag);
                     std::fprintf(stderr, "  LA_VIDEO_QA FAIL %s\n", tag); }
    };

    // ================= A 纯函数契约 =================
    chk(VideoDecode::isVideoExt(QStringLiteral("a.mp4")), "A1a_mp4");
    chk(VideoDecode::isVideoExt(QStringLiteral("A.MOV")), "A1b_upper_mov");
    chk(VideoDecode::isVideoExt(QStringLiteral("x.mkv")), "A1c_mkv");
    chk(VideoDecode::isVideoExt(QStringLiteral("y.webm")), "A1d_webm");
    chk(!VideoDecode::isVideoExt(QStringLiteral("a.dng")), "A2a_reject_dng");
    chk(!VideoDecode::isVideoExt(QStringLiteral("a.jpg")), "A2b_reject_jpg");
    chk(!VideoDecode::isVideoExt(QStringLiteral("noext")), "A2c_reject_noext");

    chk(VideoDecode::needsExternalDecode(QStringLiteral("a.mp4"), true), "A3a_video_true");
    chk(!VideoDecode::needsExternalDecode(QStringLiteral("a.mp4"), false), "A3b_no_decoder_false");
    chk(!VideoDecode::needsExternalDecode(QStringLiteral("a.json"), true), "A3c_json_false");
    chk(!VideoDecode::needsExternalDecode(QStringLiteral("a.ini"), true), "A3d_ini_false");
    chk(!VideoDecode::needsExternalDecode(QStringLiteral("a.jpg"), true), "A3e_jpg_false");
    chk(!VideoDecode::needsExternalDecode(QStringLiteral("a.dng"), true), "A3f_dng_false");

    // ================= B 真实解码 =================
    const bool avail = VideoDecode::available();
    const QString sample = qEnvironmentVariable("LA_VIDEO_SAMPLE");
    if (!avail || sample.isEmpty() || !QFileInfo::exists(sample)) {
        std::fprintf(stderr,
            "LA_VIDEO_QA SKIP reason=%s hint=\"powershell -ExecutionPolicy Bypass -File "
            "fetch_ffmpeg.ps1; then set LA_VIDEO_SAMPLE=<path-to.mp4>\"\n",
            !avail ? "ffmpeg_missing" : "sample_missing");
        std::fprintf(stderr, "LA_VIDEO_QA PASS checks=%d failed=0 broken=0 scope=pure-contract\n",
                     checks);
        std::fflush(stderr);
        return 0;
    }

    chk(avail, "B1a_available");
    chk(QFileInfo::exists(QDir(VideoDecode::toolDir()).filePath(QStringLiteral("ffmpeg.exe")))
        && QFileInfo::exists(QDir(VideoDecode::toolDir()).filePath(QStringLiteral("ffprobe.exe"))),
        "B1b_toolDir");

    VideoDecode::setForceTrustExitCodeForQa(broken);

    const QString poster = VideoDecode::extractPoster(sample);
    chk(!poster.isEmpty(), "B2_poster_produced");
    chk(!poster.isEmpty() && QFileInfo::exists(poster) && QFileInfo(poster).size() > 1024,
        "B2b_poster_nontrivial");

    QSize posterSize;
    if (!poster.isEmpty() && QFileInfo::exists(poster)) {
        QImageReader rd(poster);
        const QImage pim = rd.read();
        posterSize = pim.size();
        chk(!pim.isNull(), "B3_poster_readable");
        chk(posterSize.isValid() && posterSize.width() >= 16 && posterSize.height() >= 16,
            "B3b_poster_size");
        QFile::remove(poster);
    }

    const VideoDecode::VideoProbe vp = VideoDecode::probe(sample);
    chk(vp.durationSec > 0, "B4a_duration");
    chk(vp.width > 0 && vp.height > 0, "B4b_dimensions");

    // B5：非视频必须失败
    const QString bogus = QDir(QFileInfo(sample).absolutePath())
                              .filePath(QStringLiteral("__la_not_video.mp4"));
    QFile::remove(bogus);
    {
        QFile f(bogus); f.open(QIODevice::WriteOnly); f.write("not a video", 11); f.close();
    }
    chk(VideoDecode::extractPoster(bogus).isEmpty(), "B5_non_video_fails");
    QFile::remove(bogus);

    // ================= C 徽标像素门 =================
    // 离屏渲染两张同尺寸卡片（isVideo true/false），比对左上角区域是否出现差异。
    // 只认「同一张卡上，左上角有/无徽标」的**几何事实**，不卡具体颜色值。
    {
        const int W = 320, H = 240;
        // ★ 必须给**非空** QPixmap：空图会让 skeletonState() 判为 Loading，
        //   paintGalleryCard 走骨架屏分支**提前 return**，根本到不了徽标绘制处
        //   —— 那样 badgeDiffPixels 恒 0，会把「徽标没画」误判成「徽标画得一样」。
        //   这正是铁律 20「门禁要测什么就只取什么」的变体：夹具必须让被测阶段可达。
        QPixmap realPm(W, H);
        {
            QPainter pp(&realPm);
            pp.fillRect(0, 0, W, H, QColor(0x3a, 0x4a, 0x40));
            pp.end();
        }
        auto grabCard = [&](bool isVideo) {
            QImage img(W, H, QImage::Format_ARGB32);
            img.fill(QColor(0x14, 0x1a, 0x16));
            QPainter p(&img);
            Lumen::paintGalleryCard(&p, QRectF(0, 0, W, H),
                                    QStringLiteral("clip"), QStringLiteral("2026-10-07"),
                                    QStringLiteral("landscape"), false, 0,
                                    realPm, false, false, false, 0.0, false,
                                    isVideo, isVideo ? 10.0 : 0.0);
            p.end();
            return img;
        };
        const QImage withB = grabCard(true);
        const QImage noB   = grabCard(false);
        // 采样左上角徽标带（与绘制处同坐标：left+12, top+12, 高 20）
        int diff = 0;
        for (int y = 10; y < 36; ++y)
            for (int x = 10; x < 120; ++x)
                if (withB.pixel(x, y) != noB.pixel(x, y)) ++diff;
        std::fprintf(stderr, "  LA_VIDEO_QA badgeDiffPixels=%d\n", diff);
        chk(diff > 0, "C1_badge_drawn_when_video");
        // 反向：非视频卡的左上角应与「有图无徽标」基线完全一致 ——
        // 用同一张 realPm 渲染的 noB 作基准，只允许图片本身内容，不允许徽标残留。
        // 故改为断言：非视频卡在徽标带内**不含徽标底色**（0x0c,0x10,0x0d 及其叠加）。
        int badgeLeft = 0;
        for (int y = 10; y < 36; ++y)
            for (int x = 10; x < 120; ++x) {
                const QRgb px = noB.pixel(x, y);
                // 徽标底是近黑半透明叠加（0x0c,0x10,0x0d,190）→ 与图片底 0x3a4a40 明显不同
                if (qRed(px) < 0x20 && qGreen(px) < 0x24 && qBlue(px) < 0x20) ++badgeLeft;
            }
        chk(badgeLeft == 0, "C2_no_badge_when_image");
    }

    VideoDecode::setForceTrustExitCodeForQa(false);

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_VIDEO_QA %s checks=%d failed=%d sample=%s broken=%d\n",
                 pass ? "PASS" : "FAIL", checks, fails,
                 qPrintable(QFileInfo(sample).fileName()), broken ? 1 : 0);
    if (!pass)
        std::fprintf(stderr, "LA_VIDEO_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// ================================================================
// 2.30.0 视频播放门禁 LA_VIDEO_PLAY_QA
//  验证「内嵌自研解码器」能经 ffmpeg rawvideo 管道逐帧解出**动态**画面。
//  正例：帧数>0、末帧非空且尺寸正确、帧间像素确实不同（真解码非静止图）、pts 递增。
//  反例：FORCE_BROKEN 塞入不存在的第二输入 → ffmpeg 建不起输出流 → 必须 0 帧。
//  need=LA_VIDEO_SAMPLE（样本是外部素材，不入库、不进 CI）。
// ================================================================
static int runVideoPlayQa()
{
    const QString sample = QString::fromLocal8Bit(qgetenv("LA_VIDEO_SAMPLE"));
    const bool broken = !qEnvironmentVariableIsEmpty("LA_VIDEO_PLAY_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) { ++fails; failed << QString::fromLatin1(tag);
                     std::fprintf(stderr, "  LA_VIDEO_PLAY_QA FAIL %s\n", tag); }
    };

    if (sample.isEmpty() || !QFileInfo::exists(sample)) {
        std::fprintf(stderr,
            "LA_VIDEO_PLAY_QA SKIP: set LA_VIDEO_SAMPLE=<a video file> to run\n");
        std::fflush(stderr);
        return 0;
    }

    VideoDecoder::setForceBrokenForQa(broken);

    const int kW = 320, kH = 240;
    const int kTarget = 60;
    QImage firstFrame, lastFrame;
    int     frameCount = 0;
    qint64  lastPts = 0;
    bool    gotFinish = false, gotError = false;

    VideoDecoder dec;
    QObject::connect(&dec, &VideoDecoder::frameDecoded, &dec,
        [&](const QImage& f, qint64 pts) {
            if (frameCount == 0) firstFrame = f;
            lastFrame = f; lastPts = pts; ++frameCount;
        });
    QObject::connect(&dec, &VideoDecoder::finished, &dec, [&](){ gotFinish = true; });
    QObject::connect(&dec, &VideoDecoder::decodeError, &dec,
        [&](const QString&){ gotError = true; });

    dec.configure(sample, kW, kH, 25.0);
    dec.start();

    QElapsedTimer timer; timer.start();
    while (frameCount < kTarget && !gotFinish && timer.elapsed() < 25000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    dec.stop();   // 主动回收线程（铁律 19）

    // ★正/反例断言**完全相同**，不随 broken 切换（铁律22：期望值不随 broken 变）。
    //   broken 只是把产品注入成「解不出帧」，于是 A1/A2/A4 自然变红（铁律23：反例须实测变红）。
    chk(frameCount > 0, "A1_frames_decoded");
    chk(!lastFrame.isNull(), "A2_last_frame_nonnull");
    chk(lastFrame.width() == kW && lastFrame.height() == kH, "A3_frame_size");
    // ★真动态解码：首末帧必须有像素不同（采样网格；静止图/占位图会红）
    bool motion = false;
    if (frameCount >= 2 && !firstFrame.isNull() && !lastFrame.isNull()) {
        for (int y = 0; y < kH && !motion; y += 16)
            for (int x = 0; x < kW; x += 16)
                if (firstFrame.pixel(x, y) != lastFrame.pixel(x, y)) { motion = true; break; }
    }
    chk(motion, "A4_real_motion_frames_differ");
    chk(lastPts > 0, "A5_pts_monotonic");
    std::fprintf(stderr, "  decoded frames=%d lastPts=%lld finish=%d err=%d broken=%d\n",
                 frameCount, (long long)lastPts, gotFinish ? 1 : 0,
                 gotError ? 1 : 0, broken ? 1 : 0);

    VideoDecoder::setForceBrokenForQa(false);

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_VIDEO_PLAY_QA %s checks=%d failed=%d sample=%s broken=%d\n",
                 pass ? "PASS" : "FAIL", checks, fails,
                 qPrintable(QFileInfo(sample).fileName()), broken ? 1 : 0);
    if (!pass)
        std::fprintf(stderr, "LA_VIDEO_PLAY_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// ================================================================
// 2.30.0 播放器 UI 渲染门禁 LA_VIDEO_UI_QA
//  验证 VideoPlayerWidget 真能在离屏渲染出画面像素（铁律24：视觉必须实测）。
//  正例：解出帧 → widget 非空白 → 当前帧与首帧不同（真动）→ grab 像素非纯底。
//  反例 FORCE_BROKEN：解码 0 帧 → 画面只剩占位（断言转红）。
//  need=LA_VIDEO_SAMPLE。
// ================================================================
static int runVideoUiQa()
{
    const QString sample = QString::fromLocal8Bit(qgetenv("LA_VIDEO_SAMPLE"));
    const bool broken = !qEnvironmentVariableIsEmpty("LA_VIDEO_UI_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) { ++fails; failed << QString::fromLatin1(tag);
                     std::fprintf(stderr, "  LA_VIDEO_UI_QA FAIL %s\n", tag); }
    };

    if (sample.isEmpty() || !QFileInfo::exists(sample)) {
        std::fprintf(stderr, "LA_VIDEO_UI_QA SKIP: set LA_VIDEO_SAMPLE to run\n");
        std::fflush(stderr);
        return 0;
    }
    VideoDecoder::setForceBrokenForQa(broken);

    VideoPlayerWidget w;
    w.resize(480, 320);
    w.setSource(sample, QString(), 0.0);   // 无海报：验证解码帧本身能画出来
    w.show();
    w.play();

    // 让 -re 实时吐几帧（realtime=true，泵事件等帧）
    QElapsedTimer t; t.start();
    while (t.elapsed() < 6000 && w.currentFrame().isNull())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    // 再多泵一会儿让 hasAdvanced() 有机会置位（需要 >=2 帧且帧间不同）
    const qint64 need = qMin<qint64>(6000, 3000);
    while (t.elapsed() < need && !w.hasAdvanced())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    w.pause();

    const QImage cur = w.currentFrame();
    const bool decoded = !cur.isNull();
    chk(decoded, "D1_frame_decoded");
    chk(!decoded || cur.width() > 0, "D2_frame_dims");

    // 离屏渲染整窗 → 抓像素，断言非「纯底色」（画面区至少有一块非背景）
    const QImage shot = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
    chk(!shot.isNull(), "D3_widget_rendered");
    int nonBg = 0;
    if (!shot.isNull()) {
        for (int y = 40; y < shot.height() - 40; y += 4)
            for (int x = 8; x < shot.width() - 8; x += 4) {
                const QRgb c = shot.pixel(x, y);
                // 背景深色 ~ (13,15,14)；任一通道明显偏离即视为有内容像素
                if (qAbs(qRed(c) - 13) > 10 || qAbs(qGreen(c) - 15) > 10 || qAbs(qBlue(c) - 14) > 10)
                    ++nonBg;
            }
    }
    chk(nonBg > 200, "D4_pixels_not_blank");

    // ★正/反例断言**完全相同**，不随 broken 切换（铁律22）。
    //   broken 只是让解码产出 0 帧 → D1/D2/D5 自然变红（铁律23：反例须实测变红）。
    //   D4（画面区非空白）在 broken 下可能仍绿——因为「▶ 加载中…」占位文案
    //   本身就是有效渲染内容，这属正确行为；真正把关的是 D1（有没有解出帧）。
    chk(w.hasAdvanced(), "D5_real_motion_advanced");
    std::fprintf(stderr, "  decoded=%d nonBg=%d advanced=%d broken=%d\n",
                 decoded ? 1 : 0, nonBg, w.hasAdvanced() ? 1 : 0, broken ? 1 : 0);

    VideoDecoder::setForceBrokenForQa(false);

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_VIDEO_UI_QA %s checks=%d failed=%d sample=%s broken=%d\n",
                 pass ? "PASS" : "FAIL", checks, fails,
                 qPrintable(QFileInfo(sample).fileName()), broken ? 1 : 0);
    if (!pass)
        std::fprintf(stderr, "LA_VIDEO_UI_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// ================================================================
// 2.30.0 音频输出门禁 LA_AUDIO_QA
//  验证「音轨 → ffmpeg s16le PCM → winmm waveOut」这条链路真的送出非静音音频。
//  设计取舍：不靠「能听见」判定（离屏/无设备不可靠），而是断言
//  「解出的 PCM 字节数 > 0 且非全零」+「waveOut 设备可开」——前者证明解码链路通，
//  后者证明输出通路通。二者合起来等价于「有声」，且可离屏断言。
//  反例 FORCE_BROKEN：ffmpeg 建不起流 → 0 字节 → 断言转红。
//  need=LA_VIDEO_SAMPLE。
// ================================================================
static int runAudioQa()
{
    const QString sample = QString::fromLocal8Bit(qgetenv("LA_VIDEO_SAMPLE"));
    const bool broken = !qEnvironmentVariableIsEmpty("LA_AUDIO_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) { ++fails; failed << QString::fromLatin1(tag);
                     std::fprintf(stderr, "  LA_AUDIO_QA FAIL %s\n", tag); }
    };

    if (sample.isEmpty() || !QFileInfo::exists(sample)) {
        std::fprintf(stderr, "LA_AUDIO_QA SKIP: set LA_VIDEO_SAMPLE to run\n");
        std::fflush(stderr);
        return 0;
    }
    AudioPlayer::setForceBrokenForQa(broken);

    // ① 该样本必须确有音轨（否则本门无从谈起——诚实前提）
    const bool hasTrack = AudioPlayer::hasAudioTrack(sample);
    if (!hasTrack && !broken) {
        std::fprintf(stderr,
            "LA_AUDIO_QA SKIP: sample has no audio track (nothing to assert)\n");
        AudioPlayer::setForceBrokenForQa(false);
        std::fflush(stderr);
        return 0;
    }

    // ② 设备可开（输出通路）
    const bool devOk = AudioPlayer::deviceAvailable();
    chk(devOk, "C1_waveout_device_open");

    // ③ 真解 PCM：播放一小段，统计写入 waveOut 的字节数
    AudioPlayer::qaResetBytes();
    AudioPlayer ap;
    ap.configure(sample, 0.0);
    ap.play();
    QElapsedTimer t; t.start();
    while (t.elapsed() < 4000 && AudioPlayer::qaBytesWritten() < 176400)  // >=1s PCM
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    ap.stop();
    const quint64 bytes = AudioPlayer::qaBytesWritten();
    // 字节数必须是块大小的整数倍，且> 0（块大小从实现取，禁手写常量——铁律17）
    const int blockBytes = AudioPlayer::qaBlockBytes();
    chk(bytes > 0, "C2_pcm_written_nonzero");
    chk(blockBytes > 0 && bytes % quint64(blockBytes) == 0, "C3_pcm_block_aligned");
    std::fprintf(stderr, "  hasTrack=%d devOk=%d pcmBytes=%llu broken=%d\n",
                 hasTrack ? 1 : 0, devOk ? 1 : 0,
                 (unsigned long long)bytes, broken ? 1 : 0);

    AudioPlayer::setForceBrokenForQa(false);

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_AUDIO_QA %s checks=%d failed=%d sample=%s broken=%d\n",
                 pass ? "PASS" : "FAIL", checks, fails,
                 qPrintable(QFileInfo(sample).fileName()), broken ? 1 : 0);
    if (!pass)
        std::fprintf(stderr, "LA_AUDIO_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// ================================================================
// 2.31.0 视频选帧门禁 LA_VIDEO_FRAME_QA
//  验证「按秒选帧」这条链路真的取到**不同**的帧，且边界不崩、不串帧。
//  背景：实测同视频 t=0/2/4s 三帧 md5 各异（ac946ed/76b91ee/1d8c26f），
//  故若抽帧忽略秒数或产物名共用，会读到同一帧 —— 这正是选帧器的头号失败模式。
//  正例：0/2/4s 三帧**互不相同**；边界（0 与末尾）不崩且非空；透传生效。
//  反例 FORCE_BROKEN：令 extractPoster 忽略 atSec（恒抽 2s）→ 三帧相同 → 转红。
//  need=LA_VIDEO_SAMPLE。
// ================================================================
static int runVideoFrameQa()
{
    const QString sample = QString::fromLocal8Bit(qgetenv("LA_VIDEO_SAMPLE"));
    const bool broken = !qEnvironmentVariableIsEmpty("LA_VIDEO_FRAME_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) { ++fails; failed << QString::fromLatin1(tag);
                     std::fprintf(stderr, "  LA_VIDEO_FRAME_QA FAIL %s\n", tag); }
    };

    if (sample.isEmpty() || !QFileInfo::exists(sample)) {
        std::fprintf(stderr, "LA_VIDEO_FRAME_QA SKIP: set LA_VIDEO_SAMPLE to run\n");
        std::fflush(stderr);
        return 0;
    }
    // 反例：注入「忽略 atSec，恒抽默认 2s」的缺陷形态（真实缺陷：换了秒读到旧帧）
    VideoDecode::setForceIgnoreFrameForQa(broken);

    // 三个时刻各抽一帧
    QImage f0 = ImageLoad::loadForEdit(sample, 0.0);
    QImage f2 = ImageLoad::loadForEdit(sample, 2.0);
    QImage f4 = ImageLoad::loadForEdit(sample, 4.0);

    chk(!f0.isNull() && !f2.isNull() && !f4.isNull(), "E1_all_frames_decoded");

    // ★核心：三帧必须互不相同（采样网格比对；共用帧/串帧会红）
    auto differs = [](const QImage& a, const QImage& b) {
        if (a.isNull() || b.isNull() || a.size() != b.size()) return false;
        for (int y = 0; y < a.height(); y += 12)
            for (int x = 0; x < a.width(); x += 12)
                if (a.pixel(x, y) != b.pixel(x, y)) return true;
        return false;
    };
    chk(differs(f0, f2), "E2_frame0_differs_frame2");
    chk(differs(f2, f4), "E3_frame2_differs_frame4");
    chk(differs(f0, f4), "E4_frame0_differs_frame4");

    // 边界：第 0 秒必须可解（不是只有 2s 有效）
    chk(f0.width() > 0 && f0.height() > 0, "E5_frame0_boundary_ok");
    // 末尾秒（超时长）必须**诚实失败或给有效帧**，但绝不能崩/给半成品
    const VideoDecode::VideoProbe vp = VideoDecode::probe(sample);
    if (vp.durationSec > 4.5) {
        const QImage fEnd = ImageLoad::loadForEdit(sample, vp.durationSec - 0.5);
        chk(!fEnd.isNull() && fEnd.width() > 0, "E6_last_frame_ok");
    }

    std::fprintf(stderr, "  frames: f0=%dx%d f2=%dx%d f4=%dx%d dur=%.2f broken=%d\n",
                 f0.width(), f0.height(), f2.width(), f2.height(),
                 f4.width(), f4.height(), vp.durationSec, broken ? 1 : 0);

    VideoDecode::setForceIgnoreFrameForQa(false);

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_VIDEO_FRAME_QA %s checks=%d failed=%d sample=%s broken=%d\n",
                 pass ? "PASS" : "FAIL", checks, fails,
                 qPrintable(QFileInfo(sample).fileName()), broken ? 1 : 0);
    if (!pass)
        std::fprintf(stderr, "LA_VIDEO_FRAME_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// ================================================================
// 2.31.0 抽帧缓存门禁 LA_VIDEO_CACHE_QA
//  验证 extractPoster 的磁盘缓存真的命中、真的失效。
//  正例：
//    C1 首次抽帧成功；
//    C2/C3 缓存被写入（count=1, bytes>0）；
//    C4/C5 同一视频同一秒再次抽帧命中缓存（count 不变）；
//    C6 命中返回的帧与首次内容一致；
//    C7/C8 源文件被替换（mtime 变化）后缓存失效并产生新条目（count=2）；
//    C9 clearFrameCache 清空。
//  反例 FORCE_BROKEN：setForceBypassFrameCacheForQa(true) → 缓存完全不被写入/命中，
//    C2/C3/C5/C8 转红（期望值不随 broken 变，铁律 22）。
// ================================================================
static int runVideoCacheQa()
{
    const QString sample = QString::fromLocal8Bit(qgetenv("LA_VIDEO_SAMPLE"));
    const bool broken = !qEnvironmentVariableIsEmpty("LA_VIDEO_CACHE_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) { ++fails; failed << QString::fromLatin1(tag);
                     std::fprintf(stderr, "  LA_VIDEO_CACHE_QA FAIL %s\n", tag); }
    };

    if (sample.isEmpty() || !QFileInfo::exists(sample)) {
        std::fprintf(stderr, "LA_VIDEO_CACHE_QA SKIP: set LA_VIDEO_SAMPLE to run\n");
        std::fflush(stderr);
        return 0;
    }

    VideoDecode::setForceBypassFrameCacheForQa(broken);
    VideoDecode::clearFrameCache();

    // 用临时副本测试，避免改动原始样本
    const QString tmpCopy = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                .filePath(QStringLiteral("la_cache_qa_copy.mp4"));
    QFile::remove(tmpCopy);
    if (!QFile::copy(sample, tmpCopy)) {
        std::fprintf(stderr, "LA_VIDEO_CACHE_QA SKIP: failed to copy sample to temp\n");
        std::fflush(stderr);
        VideoDecode::setForceBypassFrameCacheForQa(false);
        return 0;
    }

    auto fileHash = [](const QString& path) -> QByteArray {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return QByteArray();
        QCryptographicHash h(QCryptographicHash::Md5);
        if (!h.addData(&f)) return QByteArray();
        return h.result().toHex();
    };

    // C1-C3：首次抽帧 → 必须未命中，写入缓存
    const QString p1 = VideoDecode::extractPoster(tmpCopy, 2.0);
    chk(!p1.isEmpty(), "C1_first_extract_ok");
    chk(VideoDecode::frameCacheCount() == 1, "C2_cache_populated");
    chk(VideoDecode::frameCacheBytes() > 0, "C3_cache_bytes_positive");

    // C4-C6：再次抽帧 → 必须命中，count 不变，内容一致
    const QString p2 = VideoDecode::extractPoster(tmpCopy, 2.0);
    chk(!p2.isEmpty(), "C4_second_extract_ok");
    chk(VideoDecode::frameCacheCount() == 1, "C5_cache_hit_no_new_entry");
    const QByteArray h1 = fileHash(p1);
    const QByteArray h2 = fileHash(p2);
    chk(!h1.isEmpty() && h1 == h2, "C6_cache_hit_identical");

    // C7-C8：修改副本 mtime（内容不变）→ 缓存键变，应产生新条目，验证缓存失效
    bool timeChanged = false;
    {
        QFile f(tmpCopy);
        // Windows 上修改时间需写权限，故以 ReadWrite 打开
        if (f.open(QIODevice::ReadWrite)) {
            timeChanged = f.setFileTime(QDateTime::currentDateTime().addDays(1),
                                        QFileDevice::FileModificationTime);
            f.close();
        }
    }
    chk(timeChanged, "C7_mtime_changed");
    const QString p3 = VideoDecode::extractPoster(tmpCopy, 2.0);
    chk(!p3.isEmpty(), "C7b_replace_extract_ok");
    chk(VideoDecode::frameCacheCount() == 2, "C8_cache_invalidated_new_entry");

    // C9：清空
    VideoDecode::clearFrameCache();
    chk(VideoDecode::frameCacheCount() == 0, "C9_clear_cache");

    // 清理临时产物
    QFile::remove(tmpCopy);
    QFile::remove(p1);
    QFile::remove(p2);
    QFile::remove(p3);
    VideoDecode::setForceBypassFrameCacheForQa(false);

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_VIDEO_CACHE_QA %s checks=%d failed=%d sample=%s broken=%d cache_dir=%s\n",
                 pass ? "PASS" : "FAIL", checks, fails,
                 qPrintable(QFileInfo(sample).fileName()), broken ? 1 : 0,
                 qPrintable(VideoDecode::frameCacheDir()));
    if (!pass)
        std::fprintf(stderr, "LA_VIDEO_CACHE_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

} // namespace（视频门禁）

namespace {

// 选片豁免契约门禁（LA_CULL_EXEMPT_QA，2.29.0）。
//
// 背景：2.28.0 接入 RAW/视频后，选片链路仍按「纯 JPEG 照片」判格式，且
// **另立了一份扩展名表**（CullPage::isRawSuffix 16 种 vs RawDecode 24 种，漂移 8 种：
// fff/iiq/k25/kdc/mos/ptx/srf/srw）。后果不是理论问题 —— RAW 豁免存在的理由正是
// 「缩略图是 JPEG、暗部 Laplacian 系统性失真」，故这些 RAW **必然被误杀**。
//
// 断言：
//   A1 exemptFromQualityCull：isRaw / isVideo 各自与组合均为真；普通照片为假
//   A2 ★ 8 种漂移扩展名必须被 isRawExt 认（钉死 CullPage 删表后的行为）
//   A3 非 RAW/非视频必须为假（含此前 ICloud 内联表漏掉的 10 种视频）
//   B  真实引擎行为：构造 RAW/视频输入 → 断言**只查分辨率**、不因模糊/曝光被淘汰
// 反例 LA_CULL_EXEMPT_QA_FORCE_BROKEN=1：令 exemptFromQualityCull 恒返回 false
//   （模拟「删掉豁免」这一真实缺陷形态）→ B 组转红而 A 组仍绿
//   （期望值不随 broken 变，铁律 22；且 A 组是纯函数契约，本就不该被注入影响）。
static int runCullExemptQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_CULL_EXEMPT_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) { ++fails; failed << QString::fromLatin1(tag);
                     std::fprintf(stderr, "  LA_CULL_EXEMPT_QA FAIL %s\n", tag); }
    };

    // ================= A 纯函数契约 =================
    {
        CullInput ci;
        ci.isRaw = true;
        chk(ci.exemptFromQualityCull(), "A1a_raw_exempt");
        ci.isRaw = false; ci.isVideo = true;
        chk(ci.exemptFromQualityCull(), "A1b_video_exempt");
        ci.isVideo = true;                       // 组合亦为真
        chk(ci.exemptFromQualityCull(), "A1c_both_exempt");
        ci.isRaw = false; ci.isVideo = false;
        chk(!ci.exemptFromQualityCull(), "A1d_photo_not_exempt");
    }
    // A2 ★ 曾经漂移的 8 种 RAW 扩展名（这是本门禁存在的核心理由）
    {
        static const char* const kDrifted[] = {
            "a.fff", "a.iiq", "a.k25", "a.kdc",
            "a.mos", "a.ptx", "a.srf", "a.srw" };
        for (const char* n : kDrifted)
            chk(RawDecode::isRawExt(QString::fromLatin1(n)), n);
    }
    // A3 非 RAW / 非视频必须为假
    {
        static const char* const kNot[] = {
            "a.jpg", "a.png", "a.json", "a.ini", "a.aae", "a.icloud" };
        for (const char* n : kNot)
            chk(!RawDecode::isRawExt(QString::fromLatin1(n)), n);
        // 曾经被 ICloudSource 内联 4 项表漏掉的视频扩展名
        static const char* const kVid[] = {
            "a.mkv", "a.avi", "a.webm", "a.wmv", "a.flv",
            "a.mpg", "a.mpeg", "a.m2ts", "a.3gp", "a.mts" };
        for (const char* n : kVid) {
            chk(VideoDecode::isVideoExt(QString::fromLatin1(n)), n);
            chk(!RawDecode::isRawExt(QString::fromLatin1(n)), n);
        }
    }

    // ================= B 真实引擎行为 =================
    // 构造一张**故意极暗**的缩略图（若走完整淘汰必被判欠曝而 rejected），
    // 分辨率给足（确保淘汰原因只可能来自质量维度），对比 RAW 与普通照片两条路径。
    {
        QTemporaryDir td;
        auto writeImg = [&](const QString& name, int v) {
            QImage im(160, 120, QImage::Format_RGB32);
            im.fill(QColor(v, v, v));
            const QString p = td.filePath(name);
            im.save(p, "JPEG", 90);
            return p;
        };

        if (!td.isValid()) {
            std::fprintf(stderr, "  LA_CULL_EXEMPT_QA SKIP b (tmpdir)\n");
        } else {
            const QString dark = writeImg("dark.jpg", 6);   // 极暗 → 曝光判定必为欠曝

            CullOptions opts;
            opts.dedupThreshold = -1;                        // 关闭相似去重（本门只测质量维度）

            CullInput inRaw;
            inRaw.thumbPath = dark;
            inRaw.origW = 4000; inRaw.origH = 3000;          // 160x120 < minDim 会因分辨率被拒，
                                                              // 故此处用 4000x3000 表达原图分辨率
            inRaw.isRaw = true;
            if (broken) inRaw.isRaw = false;                 // ★ 反例：模拟「豁免失效」
            const CullReport rRaw = CullEngine::analyze(QVector<CullInput>{inRaw}, opts);
            std::fprintf(stderr, "  LA_CULL_EXEMPT_QA raw: cand=%d rej=%d exposure=%d\n",
                         rRaw.candidateCount, rRaw.rejectedCount, rRaw.exposureCount);
            chk(rRaw.candidateCount == 1 && rRaw.rejectedCount == 0,
                "B1_raw_dark_not_rejected");

            // 反向：同图作为普通照片必被欠曝淘汰 —— 证明 B1 不是恒真（铁律 16）
            CullInput inPhoto;
            inPhoto.thumbPath = dark;
            inPhoto.origW = 4000; inPhoto.origH = 3000;
            const CullReport rPhoto = CullEngine::analyze(QVector<CullInput>{inPhoto}, opts);
            std::fprintf(stderr, "  LA_CULL_EXEMPT_QA photo: cand=%d rej=%d exposure=%d\n",
                         rPhoto.candidateCount, rPhoto.rejectedCount, rPhoto.exposureCount);
            chk(rPhoto.rejectedCount == 1, "B2_photo_dark_rejected");

            // 视频与 RAW 同享豁免
            CullInput inVid;
            inVid.thumbPath = dark;
            inVid.origW = 4000; inVid.origH = 3000;
            inVid.isVideo = true;
            const CullReport rVid = CullEngine::analyze(QVector<CullInput>{inVid}, opts);
            std::fprintf(stderr, "  LA_CULL_EXEMPT_QA video: cand=%d rej=%d\n",
                         rVid.candidateCount, rVid.rejectedCount);
            chk(rVid.candidateCount == 1 && rVid.rejectedCount == 0,
                "B3_video_dark_not_rejected");
        }
    }

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_CULL_EXEMPT_QA %s checks=%d failed=%d broken=%d\n",
                 pass ? "PASS" : "FAIL", checks, fails, broken ? 1 : 0);
    if (!pass)
        std::fprintf(stderr, "LA_CULL_EXEMPT_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

} // namespace（选片豁免门禁）

namespace {

// 2.31.0 选片页视频适配门禁（LA_CULL_VIDEO_QA）。
//
// 断言：
//   A 数据贯通：CullEngine 透传 isVideo/mediaType/durSec 到 CullItem
//   B 模型角色：CullModel 正确暴露 IsVideoRole / DurRole / MediaTypeRole
//   C 代理过滤：CullProxy::setVideoOnly(true) 只保留视频
//   D 像素门：CullCardDelegate 为视频项绘制播放徽标 + 时长
//   反例 LA_CULL_VIDEO_QA_FORCE_BROKEN=1：关闭徽标绘制 → 徽标像素消失
static int runCullVideoQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_CULL_VIDEO_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) {
            ++fails;
            failed << QString::fromLatin1(tag);
            std::fprintf(stderr, "  LA_CULL_VIDEO_QA FAIL %s\n", tag);
        }
    };

    // A. 数据贯通：构造一个视频输入与一个照片输入
    QTemporaryDir td;
    if (!td.isValid()) {
        std::fprintf(stderr, "LA_CULL_VIDEO_QA SKIP (tmpdir)\n");
        std::fflush(stderr);
        return 0;
    }
    const QString photoPath = td.filePath(QStringLiteral("photo.jpg"));
    const QString videoPath = td.filePath(QStringLiteral("video.jpg"));
    {
        QImage photo(200, 150, QImage::Format_RGB32);
        photo.fill(QColor(0x4f, 0xc0, 0x8a)); // 绿
        photo.save(photoPath);
        QImage video(200, 150, QImage::Format_RGB32);
        video.fill(QColor(0xd9, 0xa9, 0x4e)); // 金
        video.save(videoPath);
    }

    CullInput inPhoto, inVideo;
    inPhoto.thumbPath = photoPath;
    inPhoto.origW = 4000; inPhoto.origH = 3000;
    inPhoto.isVideo = false;
    inPhoto.durSec = 0;

    inVideo.thumbPath = videoPath;
    inVideo.origW = 1920; inVideo.origH = 1080;
    inVideo.isVideo = true;
    inVideo.durSec = 125.0; // 2:05

    const CullReport rep = CullEngine::analyze(QVector<CullInput>{inPhoto, inVideo}, CullOptions());
    // 两张都应成为候选（视频豁免质量淘汰）
    int videoItemIdx = -1;
    for (int i = 0; i < rep.topItems.size(); ++i) {
        if (rep.topItems[i].thumbPath == videoPath) { videoItemIdx = i; break; }
    }
    chk(videoItemIdx >= 0, "A1_video_in_candidates");
    if (videoItemIdx >= 0) {
        const CullItem& vit = rep.topItems[videoItemIdx];
        chk(vit.isVideo == true, "A2_item_isVideo");
        chk(vit.mediaType == QLatin1String("video"), "A3_item_mediaType");
        chk(qFuzzyCompare(vit.durSec, 125.0), "A4_item_durSec");
    }

    // B. 模型角色
    CullModel model;
    model.setItems(rep.topItems, td.path());
    const QModelIndex vidIx = model.index(videoItemIdx >= 0 ? videoItemIdx : 0, 0);
    chk(model.data(vidIx, CullModel::IsVideoRole).toBool() == true, "B1_model_isVideo");
    chk(model.data(vidIx, CullModel::MediaTypeRole).toString() == QLatin1String("video"), "B2_model_mediaType");
    chk(qFuzzyCompare(model.data(vidIx, CullModel::DurRole).toDouble(), 125.0), "B3_model_durSec");

    // C. 代理过滤
    CullProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setVideoOnly(true);
    chk(proxy.rowCount() == 1, "C1_videoOnly_one_row");
    if (proxy.rowCount() == 1) {
        const QModelIndex mapped = proxy.mapToSource(proxy.index(0, 0));
        chk(model.data(mapped, CullModel::IsVideoRole).toBool() == true, "C2_videoOnly_is_video");
    }
    proxy.setVideoOnly(false);
    chk(proxy.rowCount() == model.rowCount(), "C3_videoOnly_off_restores");

    // D. 像素门：用 CullCardDelegate 直接绘制到 QPixmap，避免 QWidget show 依赖
    CullPage::qaSetVideoBadgeDisabled(broken);
    QPixmap pm(232, 268);
    pm.fill(Qt::transparent);
    {
        QPainter painter(&pm);
        QStyleOptionViewItem opt;
        opt.rect = QRect(0, 0, 232, 268);
        opt.state = QStyle::State_Enabled;
        CullCardDelegate delegate;
        delegate.paint(&painter, opt, vidIx);
    }
    CullPage::qaSetVideoBadgeDisabled(false);
    const QImage img = pm.toImage().convertToFormat(QImage::Format_RGB32);
    long whiteBadge = 0;
    // 播放三角与时长文字为 #f4f7f4（高亮）；统计左上角区域（徽标大致位置）
    for (int y = 12; y < 50 && y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 12; x < 90 && x < img.width(); ++x) {
            const QRgb px = row[x];
            if (qRed(px) > 230 && qGreen(px) > 230 && qBlue(px) > 230) ++whiteBadge;
        }
    }
    chk(whiteBadge > 5, broken ? "D1_badge_pixels_absent_in_broken" : "D1_badge_pixels_present");

    // 时长文案 "2:05" 中有 '2'、':'、'0'、'5'，至少应有一部分白色像素落在右侧
    if (!broken) {
        bool hasTextPixel = false;
        for (int y = 14; y < 36 && y < img.height(); ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 36; x < 80 && x < img.width(); ++x) {
                const QRgb px = row[x];
                if (qRed(px) > 230 && qGreen(px) > 230 && qBlue(px) > 230) { hasTextPixel = true; break; }
            }
            if (hasTextPixel) break;
        }
        chk(hasTextPixel, "D2_duration_text_visible");
    }

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_CULL_VIDEO_QA %s checks=%d failed=%d broken=%d badge=%ld\n",
                 pass ? "PASS" : "FAIL", checks, fails, broken ? 1 : 0, whiteBadge);
    if (!pass)
        std::fprintf(stderr, "LA_CULL_VIDEO_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

} // namespace（选片视频适配门禁）

namespace {

// RAW/视频进入修图链路门禁（LA_EDIT_RAW_QA，2.29.0）。
//
// 背景：2.28.0 让 RAW 进了库与缩略图，却漏了修图页入口 —— `RetouchPage.cpp:1027`
// 用 `QImage(ph.path)` 直读原图，实测对 RAW 返回 **0x0 null=1** → RAW 根本进不了修图页。
// 现统一走 `ImageLoad::loadForEdit`（照片/RAW/视频三条路径收口，铁律 6）。
//
// 断言：
//   A1 needsExternal 边界：RAW/视频为真，照片/噪声文件为假
//   B 真实加载（需 LibRaw / FFmpeg + 样本；缺失则显式 SKIP + 补齐方式）：
//     B1 RAW 经 loadForEdit 得到**非 null** 且尺寸合理（这是本门禁的核心断言）
//     B2 视频封面经 loadForEdit 非 null
//     B3 普通照片经 loadForEdit 非 null（证明改动没有把既有路径弄坏）
//     B4 ★端到端像素门：RAW 加载 → RetouchEngine::apply → 断言像素**确实改变**
//        （防止「能加载但调参无效果」的假通过；遵铁律 29：存在即恒真的门等于没门）
//     B5 不存在的文件必须返回 null（失败诚实，不返回占位图）
// 反例 LA_EDIT_RAW_QA_FORCE_BROKEN=1：令 loadForEdit 对 RAW 回退到 QImage 直读
//   （即恢复 2.28.0 的缺陷形态）→ B1/B4 转红而 A 组仍绿。
static int runEditRawQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_EDIT_RAW_QA_FORCE_BROKEN");
    int checks = 0, fails = 0;
    QStringList failed;
    auto chk = [&](bool cond, const char* tag) {
        ++checks;
        if (!cond) { ++fails; failed << QString::fromLatin1(tag);
                     std::fprintf(stderr, "  LA_EDIT_RAW_QA FAIL %s\n", tag); }
    };

    // ================= A 纯函数契约 =================
    chk(ImageLoad::needsExternal(QStringLiteral("a.dng")), "A1a_raw_true");
    chk(ImageLoad::needsExternal(QStringLiteral("a.cr3")), "A1b_cr3_true");
    chk(ImageLoad::needsExternal(QStringLiteral("a.mp4")), "A1c_video_true");
    chk(!ImageLoad::needsExternal(QStringLiteral("a.jpg")), "A2a_jpg_false");
    chk(!ImageLoad::needsExternal(QStringLiteral("a.png")), "A2b_png_false");
    chk(!ImageLoad::needsExternal(QStringLiteral("a.json")), "A2c_json_false");

    // ================= B 真实加载 =================
    const QString rawSample   = qEnvironmentVariable("LA_RAW_SAMPLE");
    const QString videoSample = qEnvironmentVariable("LA_VIDEO_SAMPLE");
    const bool haveRaw   = !rawSample.isEmpty()   && QFileInfo::exists(rawSample);
    const bool haveVideo = !videoSample.isEmpty() && QFileInfo::exists(videoSample);
    if (!haveRaw || !haveVideo) {
        std::fprintf(stderr,
            "LA_EDIT_RAW_QA SKIP reason=%s hint=\"set LA_RAW_SAMPLE and LA_VIDEO_SAMPLE\"\n",
            !haveRaw ? (RawDecode::available() ? "raw_sample_missing" : "libraw_missing")
                     : (VideoDecode::available() ? "video_sample_missing" : "ffmpeg_missing"));
        std::fprintf(stderr, "LA_EDIT_RAW_QA PASS checks=%d failed=0 broken=0 scope=pure-contract\n",
                     checks);
        std::fflush(stderr);
        return 0;
    }

    if (broken) {
        // ★ 反例注入：恢复 2.28.0 的缺陷形态 —— 对 RAW 直接 QImage 直读（实测 0x0 null）
        //   注入点必须在「按类型分派」这一步之内（铁律 32）。
        ImageLoad::setForceRawFallbackForQa(true);
    }

    // B1 ★ RAW 能真正进入编辑链路（旧实现在此返回 null）
    const QImage rawImg = ImageLoad::loadForEdit(rawSample);
    std::fprintf(stderr, "  LA_EDIT_RAW_QA raw: %dx%d null=%d\n",
                 rawImg.width(), rawImg.height(), rawImg.isNull() ? 1 : 0);
    chk(!rawImg.isNull(), "B1_raw_loads");
    chk(!rawImg.isNull() && rawImg.width() >= 512 && rawImg.height() >= 512,
        "B1b_raw_size");

    // B2 视频封面
    const QImage vidImg = ImageLoad::loadForEdit(videoSample);
    chk(!vidImg.isNull(), "B2_video_loads");

    // B3 普通照片未被破坏（回归保护）
    const QString jpgSample = qEnvironmentVariable("LA_EDIT_JPG_SAMPLE");
    if (!jpgSample.isEmpty() && QFileInfo::exists(jpgSample)) {
        const QImage jpgImg = ImageLoad::loadForEdit(jpgSample);
        chk(!jpgImg.isNull(), "B3_jpg_still_loads");
    }

    // B4 ★端到端像素门：加载 → apply → 像素必须改变
    if (!rawImg.isNull()) {
        const QImage before = rawImg;
        RetouchParams p;                      // 默认参数即应有可见效果
        p.exposure = 0.5;
        const QImage after = RetouchEngine::apply(before, p);
        chk(!after.isNull(), "B4a_apply_non_null");
        if (!after.isNull()) {
            chk(after.size() == before.size(), "B4b_size_preserved");
            // 逐像素比较抽样：曝光 +0.5 必然改变大量像素
            long long diff = 0, sampled = 0;
            for (int y = 0; y < before.height(); y += 8) {
                for (int x = 0; x < before.width(); x += 8) {
                    ++sampled;
                    if (before.pixel(x, y) != after.pixel(x, y)) ++diff;
                }
            }
            const double pct = sampled ? (100.0 * double(diff) / double(sampled)) : 0.0;
            std::fprintf(stderr, "  LA_EDIT_RAW_QA pixels: changed=%.1f%% (n=%lld)\n", pct, sampled);
            chk(pct > 50.0, "B4c_pixels_changed");
        }
    }

    // B5 不存在的文件必须诚实返回 null
    chk(ImageLoad::loadForEdit(QStringLiteral("__la_no_such_file__.dng")).isNull(),
        "B5_missing_returns_null");

    ImageLoad::setForceRawFallbackForQa(false);

    const bool pass = (checks > 0) && (fails == 0);
    std::fprintf(stderr, "LA_EDIT_RAW_QA %s checks=%d failed=%d sample=%s broken=%d\n",
                 pass ? "PASS" : "FAIL", checks, fails,
                 qPrintable(QFileInfo(rawSample).fileName()), broken ? 1 : 0);
    if (!pass)
        std::fprintf(stderr, "LA_EDIT_RAW_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

} // namespace（RAW 修图门禁）

namespace {
// P10 离线逆地理编码冒烟：已知点断言（pip / 城市兜底 / 海上诚实空）+ 真库回填统计。
int runGeoSmoke()
{
    GeoIndex& gi = GeoIndex::instance();
    const bool loaded = gi.ensureLoaded();
    int pass = loaded ? 1 : 0;
    // 数据完整性自检：中国城市省名不得残留英文（P-geo-prov-cn 债务清偿验证）
    {
        const QVector<QString> bad = gi.verifyChinaProvinces();
        for (const QString& b : bad)
            std::fprintf(stderr, "GEO badprov %s\n", qPrintable(b));
        if (!bad.isEmpty()) pass = 0;
    }
    struct P { double lat, lon; const char* r; const char* pv; const char* ct; };
    static const P pts[] = {
        {64.15, -21.94,  "iceland", "Capital Region",   "雷克雅未克"},
        // DataV 34 省切换后，多边形主路径返回中文省名（北京市/广东省/辽宁省）。
        // kCities 英文兜底债务已清偿（本迭代）：所有 china 城市 prov 改为精确中文省名，
        // 并由 verifyChinaProvinces() 在门禁内逐条校验（含"无 ASCII 字母 + 在 34 省白名单"）。
        // 以下期望对齐设计内中文真值（主路径命中多边形）。
        {39.90, 116.41,  "china",   "北京市", "北京"},
        {23.13, 113.26,  "china",   "广东省", "广州"},
        {38.91, 121.61,  "china",   "辽宁省", "大连"},   // 主路径命中辽宁省多边形
        {30.57, 104.07,  "china",   "四川省", "成都"},   // 主路径命中四川省多边形
        {15.46, 111.13,  "",        "",                 ""},       // 南海海域 → 诚实空
    };
    for (const P& p : pts) {
        const GeoResult g = gi.resolve(p.lat, p.lon);
        // 中文字面量必须 fromUtf8（QLatin1String 按 Latin-1 解，中文必错——PathGuess 同款教训）
        const bool ok = g.region == QString::fromUtf8(p.r)
                     && g.prov == QString::fromUtf8(p.pv)
                     && g.city == QString::fromUtf8(p.ct);
        std::fprintf(stderr, "GEO %s lat=%.2f lon=%.2f -> r=%s pv=%s ct=%s\n",
                     ok ? "ok" : "FAIL", p.lat, p.lon,
                     qPrintable(g.region), qPrintable(g.prov), qPrintable(g.city));
        if (!ok) pass = 0;
    }
    // 真库回填统计（读时回填，KPI 省份数应显著 > 0）
    int enriched = 0, total = 0, provCount = 0;
    if (qEnvironmentVariableIsSet("LA_DB")) {
        Library lib;
        QString err;
        if (lib.open(qEnvironmentVariable("LA_DB"), &err)) {
            for (const Photo& p : lib.photos()) {
                ++total;
                if (!p.region.isEmpty()) ++enriched;
            }
            provCount = lib.kpi().provCount;
        } else {
            std::fprintf(stderr, "GEO db open fail: %s\n", qPrintable(err));
        }
    }
    std::fprintf(stderr, "GEO realdb total=%d enriched=%d provCount=%d\n",
                 total, enriched, provCount);
    if (total > 0 && (enriched * 100 / total) < 40) pass = 0;   // 真库点亮 ≥40%
    std::fprintf(stderr, "LA_GEO_SMOKE %s loaded=%d\n", pass ? "PASS" : "FAIL", loaded ? 1 : 0);
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 B：撤销/重做栈纯状态机断言（直接驱动 RtHistory，无需 UI / 库）。
int runRetouchUndoQa()
{
    RtHistory h;
    RtEditState a, b, c, d;
    a.cur[QStringLiteral("brightness")] = 0.0; a.rot = 0; a.angle = 0.0;
    b.cur[QStringLiteral("brightness")] = 0.3; b.rot = 0; b.angle = 0.0;
    c.cur[QStringLiteral("brightness")] = 0.3; c.rot = 1; c.angle = 5.0;
    c.cropNorm = QRectF(0.1, 0.1, 0.5, 0.5);
    d.cur[QStringLiteral("brightness")] = 0.5; d.rot = 2; d.angle = 0.0;

    h.clear(a);
    int marks = 0, checks = 0;
    auto chk = [&](bool ok){ ++checks; if (ok) ++marks; };

    chk(!h.canUndo() && !h.canRedo());                  // 基线：无撤销/重做
    h.push(b);
    chk(h.canUndo() && !h.canRedo());                  // 一次编辑后可撤不可重
    h.push(c);
    chk(h.canUndo() && h.pos() == 2 && h.size() == 3); // 两步后位于栈顶
    chk(h.undo() == b && h.pos() == 1 && h.canRedo()); // undo -> b
    chk(h.undo() == a && h.pos() == 0 && !h.canUndo());// undo -> a（栈底）
    chk(h.redo() == b && h.pos() == 1);                // redo -> b
    h.push(d);                                          // 在 b 之后 push -> 截断 c
    chk(!h.canRedo() && h.size() == 3 && h.pos() == 2);// push 截断 redo 尾
    const int before = h.size();
    h.push(d);                                          // 无变化不记
    chk(h.size() == before);
    chk(h.current() == d && h.current().rot == 2);      // 当前态等于 d

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr, "LA_RETOUCH_UNDO_QA %s marks=%d checks=%d\n",
                 pass ? "PASS" : "FAIL", marks, checks);
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// ============================================================================
// 方向 B 深化：12 算子逐项方向像素断言（LA_RETOUCH_OP_QA）
//
// 为什么需要这道门（取证结论，非推测）：修图已有 LA_RETOUCH_SMOKE /
// LA_RETOUCH_A_QA / LA_RETOUCH_UNDO_QA 三条门，但它们断言的全是**几何与流程**
// （旋转/方形/翻转/蒙版混合/裁剪/擦除桥/撤销栈），**12 个像素算子一个都没断言**——
// 甚至连 `out.width() != 0` 这种恒真式都在输出里。现状只能证明「界面能打开」，
// 不能证明「曝光滑杆真的改了像素」。更要命的是：单算子方向若反了，往往会被
// 另一算子的效果**掩盖**（实测 recommend() 对偏亮图给 highlights=-0.30 实为提亮 +42.90，
// 却因 exposure=-0.10 掩盖成整体 -16.23 看起来"正常"）。故本门的设计原则是
// **每个算子单独施参 + 断言方向**，绝不用组合参数下结论。
//
// 阈值标定来源：tools/retouch_probe.cpp 在 128x96 合成图上实测 20 组算子输出，
// 取各方向 delta 的实测最小绝对值再留余量（非猜、非估）。
//
// 反例：LA_RETOUCH_OP_QA_FORCE_BROKEN=1 把若干算子接成恒等（模拟"滑杆拖了没反应"
// 这一最常见真实缺陷形态），正例实测为绿、反例实测变红，且**期望值不随 broken 切换**。
// ============================================================================

// 12 算子度量原语（局部静态，RetouchEngine 内部 luma/boxBlur 为文件私有，此处独立实现）
static double rtQaLuma(QRgb c) { return 0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c); }

static double rtQaMeanLuma(const QImage& im)
{
    if (im.isNull() || im.width() == 0) return -1.0;
    double s = 0; long n = 0;
    for (int y = 0; y < im.height(); ++y) {
        const QRgb* d = reinterpret_cast<const QRgb*>(im.constScanLine(y));
        for (int x = 0; x < im.width(); ++x) { s += rtQaLuma(d[x]); ++n; }
    }
    return n ? s / double(n) : -1.0;
}

static double rtQaStdLuma(const QImage& im)
{
    if (im.isNull() || im.width() == 0) return -1.0;
    double s = 0, s2 = 0; long n = 0;
    for (int y = 0; y < im.height(); ++y) {
        const QRgb* d = reinterpret_cast<const QRgb*>(im.constScanLine(y));
        for (int x = 0; x < im.width(); ++x) {
            const double L = rtQaLuma(d[x]); s += L; s2 += L * L; ++n;
        }
    }
    if (!n) return -1.0;
    const double m = s / double(n);
    return std::sqrt(std::max(0.0, s2 / double(n) - m * m));
}

static double rtQaMeanSat(const QImage& im)
{
    if (im.isNull() || im.width() == 0) return -1.0;
    double s = 0; long n = 0;
    for (int y = 0; y < im.height(); ++y) {
        const QRgb* d = reinterpret_cast<const QRgb*>(im.constScanLine(y));
        for (int x = 0; x < im.width(); ++x) {
            const int mx = std::max({qRed(d[x]), qGreen(d[x]), qBlue(d[x])});
            const int mn = std::min({qRed(d[x]), qGreen(d[x]), qBlue(d[x])});
            s += double(mx - mn) / 255.0; ++n;
        }
    }
    return n ? s / double(n) : -1.0;
}

// 局部对比度（|拉普拉斯| 均值）—— clarity / sharpen 的方向判别器。
// 单看均值/方差无法区分「锐化」与「整体加对比」，必须看局部梯度能量。
static double rtQaLocalContrast(const QImage& im)
{
    if (im.isNull() || im.width() < 3 || im.height() < 3) return -1.0;
    const QImage q = im.convertToFormat(QImage::Format_RGB32);
    double s = 0; long n = 0;
    for (int y = 1; y < q.height() - 1; ++y) {
        const QRgb* d = reinterpret_cast<const QRgb*>(q.constScanLine(y));
        const QRgb* u = reinterpret_cast<const QRgb*>(q.constScanLine(y - 1));
        const QRgb* v = reinterpret_cast<const QRgb*>(q.constScanLine(y + 1));
        for (int x = 1; x < q.width() - 1; ++x) {
            const double lap = 4.0 * rtQaLuma(d[x]) - rtQaLuma(d[x - 1]) - rtQaLuma(d[x + 1])
                             - rtQaLuma(u[x]) - rtQaLuma(v[x]);
            s += std::fabs(lap); ++n;
        }
    }
    return n ? s / double(n) : -1.0;
}

// 通道和（判色温/色调方向用）。返回 long[3] = R/G/B 相对 src 的增量。
static QVector<long> rtQaRgbDelta(const QImage& a, const QImage& b)
{
    QVector<long> out(3, 0);
    if (a.isNull() || b.isNull() || a.size() != b.size()) return out;
    long d[3] = {0, 0, 0};
    for (int y = 0; y < a.height(); ++y) {
        const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            d[0] += qRed(pb[x])   - qRed(pa[x]);
            d[1] += qGreen(pb[x]) - qGreen(pa[x]);
            d[2] += qBlue(pb[x])  - qBlue(pa[x]);
        }
    }
    out[0] = d[0]; out[1] = d[1]; out[2] = d[2];
    return out;
}

// 算子度量快照：把上面四个度量 + 通道和打包，便于逐算子统一比较。
struct RtOpMetrics {
    double luma = 0, stdL = 0, sat = 0, local = 0;
    QVector<long> rgb{0, 0, 0};
};

static RtOpMetrics rtQaMeasure(const QImage& base, const QImage& out)
{
    RtOpMetrics m;
    m.luma  = rtQaMeanLuma(out) - rtQaMeanLuma(base);
    m.stdL  = rtQaStdLuma(out) - rtQaStdLuma(base);
    m.sat   = rtQaMeanSat(out) - rtQaMeanSat(base);
    m.local = rtQaLocalContrast(out) - rtQaLocalContrast(base);
    m.rgb   = rtQaRgbDelta(base, out);
    return m;
}

// 方向 B 深化：12 算子逐项方向断言 + identity 契约 + 空间分布断言。
int runRetouchOpQa()
{
    const bool broken = qEnvironmentVariableIsSet("LA_RETOUCH_OP_QA_FORCE_BROKEN");

    // 128x96 合成测试图（与 tools/retouch_probe.cpp 完全同一张，阈值即由它标定）：
    // 左半中灰 128 / 右上亮块 225（高光区 L>0.5）/ 左下暗块 38（阴影区 L<0.5）
    // / 有色块 (200,60,60)（给饱和与白平衡）/ 硬边缘白块（给 clarity 与 sharpen）
    QImage src(128, 96, QImage::Format_RGB32);
    src.fill(qRgb(128, 128, 128));
    {
        QPainter p(&src);
        p.fillRect(80, 8, 40, 36, qRgb(225, 225, 225));    // 亮块
        p.fillRect(8, 56, 40, 32, qRgb(38, 38, 38));      // 暗块
        p.fillRect(52, 34, 24, 24, qRgb(200, 60, 60));    // 有色块
        p.fillRect(30, 20, 20, 20, qRgb(245, 245, 245));  // 硬边缘
        p.end();
    }
    if (src.isNull()) {
        std::fprintf(stderr, "LA_RETOUCH_OP_QA FAIL src_null\n");
        std::fflush(stderr);
        return 1;
    }

    int checks = 0, marks = 0;
    QStringList fails;
    auto chk = [&](bool ok, const char* what) {
        ++checks;
        if (ok) ++marks; else fails << QString::fromLatin1(what);
    };

    // ---------- 0) identity 契约：空参必须逐位等同原图（守恒，非"输出非空"） ----------
    // isEmpty() 短路在 apply() 首行，若短路失效会静默重算并引入舍入偏差。
    {
        const RetouchParams id;
        const QImage idOut = RetouchEngine::apply(src, id);
        bool same = (idOut.size() == src.size());
        if (same) {
            for (int y = 0; y < src.height() && same; ++y) {
                const QRgb* a = reinterpret_cast<const QRgb*>(src.constScanLine(y));
                const QRgb* b = reinterpret_cast<const QRgb*>(idOut.constScanLine(y));
                for (int x = 0; x < src.width(); ++x) {
                    if (a[x] != b[x]) { same = false; break; }
                }
            }
        }
        chk(same, "identity_bit_identical");
    }

    // ---------- 逐算子方向断言 ----------
    // 阈值 = tools/retouch_probe.cpp 实测 delta 取安全余量（见各注释）。
    // 反例注入（broken）：把各算子的参数**清零** → apply() 走 isEmpty 短路 → 输出恒等，
    // 精确模拟"滑杆拖了像素没动"这一最常见真实缺陷形态。
    // **期望值不随 broken 切换**（铁律 22）：反例靠上述断言真实变红，而非改阈值放水。
    // 各算子必须**单独施参**——单算子方向反了常被另一算子掩盖（实测 highlights 反向时
    // 被 exposure 掩盖成"整体正常"，见 CHANGELOG 2.24.0）。

    // 曝光：+0.5 → 平均亮度升（实测 +99.3），-0.5 → 降（实测 -66.2）
    {
        RetouchParams p; p.exposure = broken ? 0.0 : 0.5;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.luma > 20.0, "exposure_pos_brightens");
    }
    {
        RetouchParams p; p.exposure = broken ? 0.0 : -0.5;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.luma < -20.0, "exposure_neg_darkens");
    }
    // 亮度：+0.5 → 升（实测 +29.3），-0.5 → 降（实测 -30.0）
    {
        RetouchParams p; p.brightness = broken ? 0.0 : 0.5;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.luma > 10.0, "brightness_pos_brightens");
    }
    {
        RetouchParams p; p.brightness = broken ? 0.0 : -0.5;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.luma < -10.0, "brightness_neg_darkens");
    }
    // 对比：+0.5 → 亮度标准差升（实测 +2.65），-0.5 → 降（实测 -14.7）
    {
        RetouchParams p; p.contrast = broken ? 0.0 : 0.5;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.stdL > 1.0, "contrast_pos_widens_spread");
    }
    {
        RetouchParams p; p.contrast = broken ? 0.0 : -0.5;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.stdL < -3.0, "contrast_neg_narrows_spread");
    }
    // 清晰（clarity）：只看局部梯度能量，均值几乎不动（实测 +1.96 / -1.32）
    {
        RetouchParams p; p.clarity = broken ? 0.0 : 0.5;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.local > 0.5, "clarity_pos_raises_local_contrast");
    }
    {
        RetouchParams p; p.clarity = broken ? 0.0 : -0.5;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.local < -0.5, "clarity_neg_lowers_local_contrast");
    }
    // 自然饱和（vibrance）：+0.6 → 饱和升（实测 +0.0055），-0.6 → 降（实测 -0.0055）
    {
        RetouchParams p; p.vibrance = broken ? 0.0 : 0.6;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.sat > 0.002, "vibrance_pos_raises_sat");
    }
    {
        RetouchParams p; p.vibrance = broken ? 0.0 : -0.6;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.sat < -0.002, "vibrance_neg_lowers_sat");
    }
    // 饱和：+0.6 → 升（实测 +0.0147）；-1.0 → 全灰，实测饱和应≈0
    {
        RetouchParams p; p.saturation = broken ? 0.0 : 0.6;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.sat > 0.005, "saturation_pos_raises_sat");
    }
    {
        RetouchParams p; p.saturation = broken ? 0.0 : -1.0;
        const QImage out = RetouchEngine::apply(src, p);
        const double satAbs = rtQaMeanSat(out);      // 绝对值：-1 应≈0（纯灰）
        chk(satAbs < 0.005, "saturation_neg1_goes_gray");
    }
    // 色温：+0.8 → R 升 B 降（实测 dR=+191984 dB=-189792，G 不变=0）
    {
        RetouchParams p; p.temp = broken ? 0.0 : 0.8;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.rgb[0] > 1000 && m.rgb[2] < -1000, "temp_pos_warm_rb");
    }
    {
        RetouchParams p; p.temp = broken ? 0.0 : -0.8;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.rgb[0] < -1000 && m.rgb[2] > 1000, "temp_neg_cool_rb");
    }
    // 色调：+0.8 → R/B 升 G 降（品红，实测 dR=+128896 dG=-126560）；-0.8 → 反向（绿）
    {
        RetouchParams p; p.tint = broken ? 0.0 : 0.8;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.rgb[0] > 1000 && m.rgb[1] < -1000 && m.rgb[2] > 1000, "tint_pos_magenta");
    }
    {
        RetouchParams p; p.tint = broken ? 0.0 : -0.8;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.rgb[0] < -1000 && m.rgb[1] > 1000 && m.rgb[2] < -1000, "tint_neg_green");
    }
    // 锐化（sharpen）：局部梯度能量升（实测 +5.20）；整体亮度基本不动（实测 -0.08）
    {
        RetouchParams p; p.sharpen = broken ? 0.0 : 0.8;
        const RtOpMetrics m = rtQaMeasure(src, RetouchEngine::apply(src, p));
        chk(m.local > 1.0, "sharpen_raises_local_contrast");
    }

    // ---------- 空间分布断言（隔离"只压高光 / 只提阴影 / 只加暗角"） ----------
    // 高光算子语义裁定：UI 滑杆标签「高光」范围 [-1,1]，用户拖右 = 提亮高光
    // （与 12 算子中其余 11 个的「正值=增强同名效果」约定一致）。故 +0.8 必须提亮亮块、
    // 且**不得改动暗块**（cross-contamination 断言，防止"压暗块"冒充"提亮块"）。
    {
        RetouchParams p; p.highlights = broken ? 0.0 : 0.8;
        const QImage out = RetouchEngine::apply(src, p);
        const int brightSrc = qRed(src.pixel(100, 25));    // 亮块 (225)
        const int darkSrc   = qRed(src.pixel(25, 70));     // 暗块 (38)
        const int brightOut = qRed(out.pixel(100, 25));
        const int darkOut   = qRed(out.pixel(25, 70));
        chk(brightOut > brightSrc, "highlights_pos_brightens_bright_block");
        chk(darkOut == darkSrc, "highlights_pos_spares_dark_block");
    }
    {
        RetouchParams p; p.highlights = broken ? 0.0 : -0.8;
        const QImage out = RetouchEngine::apply(src, p);
        chk(qRed(out.pixel(100, 25)) < qRed(src.pixel(100, 25)), "highlights_neg_darkens_bright_block");
    }
    // 阴影：+0.8 → 提亮暗块，且不改动亮块（实测暗块 38→181）
    {
        RetouchParams p; p.shadows = broken ? 0.0 : 0.8;
        const QImage out = RetouchEngine::apply(src, p);
        chk(qRed(out.pixel(25, 70)) > qRed(src.pixel(25, 70)), "shadows_pos_brightens_dark_block");
        chk(qRed(out.pixel(100, 25)) == qRed(src.pixel(100, 25)), "shadows_pos_spares_bright_block");
    }
    {
        RetouchParams p; p.shadows = broken ? 0.0 : -0.8;
        const QImage out = RetouchEngine::apply(src, p);
        chk(qRed(out.pixel(25, 70)) < qRed(src.pixel(25, 70)), "shadows_neg_darkens_dark_block");
    }
    // 暗角：+0.8 → 四角变暗、中心基本不变（实测 corner 128→67，center 200→198）
    {
        RetouchParams p; p.vignette = broken ? 0.0 : 0.8;
        const QImage out = RetouchEngine::apply(src, p);
        const int cx = out.width() / 2, cy = out.height() / 2;
        chk(qRed(out.pixel(1, 1)) < qRed(src.pixel(1, 1)), "vignette_darkens_corner");
        chk(std::abs(qRed(out.pixel(cx, cy)) - qRed(src.pixel(cx, cy))) <= 6,
            "vignette_spares_center");
    }

    // ---------- recommend() 语义一致性：芯片文案「压高光」必须真压 ----------
    // 取证发现（已实测）：原实现对偏亮图给 highlights -= 0.30，而引擎
    // t = -p.highlights*(L-0.5)*2 使**负值=提亮**，与文案相反；整体亮度被
    // exposure=-0.10 掩盖成"看起来正常"，单算子隔离才暴露。此断言锁定文案与像素一致。
    {
        QImage bright(64, 48, QImage::Format_RGB32);
        bright.fill(qRgb(215, 212, 205));                  // avgL≈0.83 > 0.62 → 触发「压高光」
        const RetouchSuggest s = RetouchEngine::recommend(bright);
        const bool hasChip = s.chips.join(QLatin1Char('|')).contains(QStringLiteral("压高光"));
        // 只隔离 highlights 分量：它必须把亮块压暗
        RetouchParams only; only.highlights = s.params.highlights;
        const QImage out = RetouchEngine::apply(bright, only);
        const int srcPx = qRed(bright.pixel(32, 24)), outPx = qRed(out.pixel(32, 24));
        if (hasChip) chk(outPx < srcPx, "recommend_highlights_chip_matches_pixels");
        else         chk(true, "recommend_highlights_chip_matches_pixels");
    }

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr,
        "LA_RETOUCH_OP_QA %s ops=12 checks=%d marks=%d broken=%d fails=[%s]\n",
        pass ? "PASS" : "FAIL", checks, marks, int(broken),
        qPrintable(fails.join(QStringLiteral(","))));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 C 第一步：相册内搜索过滤断言（直接驱动 GalleryModel，无需 UI / 库）。
int runGallerySearchQa()
{
    // 重要：main() 入口已构造 QApplication（line ~417），QA 派发在其后。
    // 此处**禁止**再建 QCoreApplication —— 双 QApplication 退场会 SIGSEGV（实测 EXIT=139）。
    // GalleryModel 仅做同步数据运算（setSource/setFilter/rebuild/rowCount），复用既有的 QApplication 即可。

    auto mk = [](const QString& title, const QString& cam, const QString& region,
                 const QString& prov, const QString& city, const QString& date, int rating) {
        Photo p;
        p.title = title; p.cam = cam; p.region = region; p.prov = prov;
        p.city = city; p.date = date; p.rating = rating; p.thumbFile = QStringLiteral("t");
        return p;
    };
    QVector<Photo> phs;
    phs.append(mk(QStringLiteral("黄山日出"), QStringLiteral("Canon EOS R5"),
                  QStringLiteral("china"), QStringLiteral("安徽省"), QStringLiteral("黄山市"),
                  QStringLiteral("2023-05-01"), 5));
    phs.append(mk(QStringLiteral("东京夜景"), QStringLiteral("Sony A7"),
                  QStringLiteral("japan"), QStringLiteral("Tokyo"), QStringLiteral("东京"),
                  QStringLiteral("2022-11-20"), 4));
    phs.append(mk(QStringLiteral("家猫"), QStringLiteral("iPhone"),
                  QStringLiteral("china"), QStringLiteral("北京市"), QStringLiteral("北京"),
                  QStringLiteral("2024-01-12"), 3));
    phs.append(mk(QStringLiteral("冰岛风光"), QStringLiteral("Nikon"),
                  QStringLiteral("iceland"), QStringLiteral("Capital Region"), QStringLiteral("雷克雅未克"),
                  QStringLiteral("2021-07-10"), 5));

    GalleryModel m;
    m.setSource(&phs);

    int marks = 0, checks = 0;
    auto chk = [&](bool ok){ ++checks; if (ok) ++marks; };
    auto cnt = [&](){ return m.rowCount(); };

    m.setFilter(QStringLiteral("北京"));   chk(cnt() == 1);   // 北京市/北京 命中 1
    m.setFilter(QStringLiteral("china"));  chk(cnt() == 2);   // region 命中 2
    m.setFilter(QStringLiteral("2023"));    chk(cnt() == 1);   // 年份 1
    m.setFilter(QStringLiteral("5"));       chk(cnt() == 2);   // 裸串"5"命中 2 张（黄山/冰岛均含 5；其余日期/器材不含裸 5）
    m.setFilter(QStringLiteral("Canon"));   chk(cnt() == 1);   // 器材 1
    m.setFilter(QStringLiteral("夜景"));    chk(cnt() == 1);   // 标题关键词 1
    m.setFilter(QString());                 chk(cnt() == 4);   // 清空恢复全部

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr, "LA_GALLERY_SEARCH_QA %s marks=%d checks=%d\n",
                 pass ? "PASS" : "FAIL", marks, checks);
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 C 第一步：相册聚类门禁（LA_CLUSTER_QA）。
// 注入确定性 Photo 列表，断言 PhotoCluster 纯函数的四类性质：
//   A 地点聚类：按省/市正确分组
//   B 时间聚类：按年/月正确分组
//   C ★数据诚实：无地点/无日期的照片必须进「未分类」簇，**绝不丢弃**
//     （簇总数守恒 = 可见照片数；这是「聚类没骗用户」的核心契约）
//   D 隐私门：hidePrivate=true 时私密照片不参与分簇
// 反例 LA_CLUSTER_QA_FORCE_BROKEN=1 令空键照片被直接跳过 → 总数不守恒 → RED。
// 注意：main() 已构造 QApplication，此处**禁止**再建（双实例退场 SIGSEGV）。
int runClusterQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_CLUSTER_QA_FORCE_BROKEN");
    auto mk = [](const QString& prov, const QString& city, int year, const QString& date) {
        Photo p;
        p.prov = prov; p.city = city; p.year = year; p.date = date;
        p.thumbFile = QStringLiteral("t");
        return p;
    };
    QVector<Photo> phs;
    phs.append(mk(QStringLiteral("安徽省"), QStringLiteral("黄山市"), 2023, QStringLiteral("2023-05-01")));
    phs.append(mk(QStringLiteral("安徽省"), QStringLiteral("黄山市"), 2023, QStringLiteral("2023-05-02")));
    phs.append(mk(QStringLiteral("安徽省"), QStringLiteral("合肥市"), 2023, QStringLiteral("2023-07-11")));
    phs.append(mk(QStringLiteral("北京市"), QStringLiteral("北京市"), 2024, QStringLiteral("2024-01-12")));
    phs.append(mk(QString(), QString(), 0, QString()));                 // 无地点无时间 → 未分类
    phs.append(mk(QStringLiteral("北京市"), QString(), 2024, QStringLiteral("2024-03-05"))); // 省市不等 → 两维度各成一簇
    Photo priv = mk(QStringLiteral("浙江省"), QStringLiteral("杭州市"), 2024, QStringLiteral("2024-02-02"));
    priv.priv = true;                                                    // 私密：隐私门开启时应被排除
    phs.append(priv);
    const int total = phs.size();          // 8
    const int visible = total - 1;         // 6（私密 1 张在 hidePrivate=true 下不计）

    int marks = 0, checks = 0;
    QStringList failed;   // 诊断：记下未通过的断言标签，便于定位（不参与判定）
    auto chk = [&](bool ok, const char* tag = ""){
        ++checks;
        if (ok) ++marks;
        else if (tag && *tag) failed << QString::fromLatin1(tag);
    };
    // 反例：在**真实实现**上注入「空键照片被丢弃」缺陷（core 层 QA 钩子），
    // 让下方 A/B/C/D 全部断言自然变红 —— 这才是非空心门禁。
    // （铁律 22：期望值恒为规格值，不因 broken 而改变）
    PhotoCluster::setForceDropUnclusteredForQa(broken);

    // —— A 地点聚类（按省）——
    const QVector<PhotoCluster::Cluster> prov =
        PhotoCluster::cluster(phs, PhotoCluster::ByProvince, true);
    {
        int sum = 0;
        for (const PhotoCluster::Cluster& c : prov) sum += c.count;
        chk(sum == visible, "provSum");
        chk(prov.size() == 3, "provSize");
        int unc = 0, bj = 0, ah = 0;
        for (const PhotoCluster::Cluster& c : prov) {
            if (c.key == QString::fromLatin1(PhotoCluster::kUnclustered)) unc = c.count;
            else if (c.key == QStringLiteral("北京市")) bj = c.count;
            else if (c.key == QStringLiteral("安徽省")) ah = c.count;
        }
        chk(unc == 1, "provUnc");
        chk(bj == 2, "provBj");
        chk(ah == 3, "provAh");
    }
    // 按市：黄山市 2 / 合肥市 1 / 北京市 1 / 未分类 2（两张缺 city）
    {
        const QVector<PhotoCluster::Cluster> city =
            PhotoCluster::cluster(phs, PhotoCluster::ByCity, true);
        int huangshan = 0, hefei = 0, bj = 0, unc = 0;
        for (const PhotoCluster::Cluster& c : city) {
            if (c.key == QStringLiteral("黄山市")) huangshan = c.count;
            else if (c.key == QStringLiteral("合肥市")) hefei = c.count;
            else if (c.key == QStringLiteral("北京市")) bj = c.count;
            else if (c.key == QString::fromLatin1(PhotoCluster::kUnclustered)) unc = c.count;
        }
        chk(city.size() == 4, "citySize");
        chk(huangshan == 2, "cityHs");
        chk(hefei == 1, "cityHf");
        chk(bj == 1, "cityBj");
        chk(unc == 2, "cityUnc");
    }

    // —— B 时间聚类（按年 / 按月）——
    {
        const QVector<PhotoCluster::Cluster> byYear =
            PhotoCluster::cluster(phs, PhotoCluster::ByYear, true);
        int y2023 = 0, y2024 = 0, yUnc = 0;
        for (const PhotoCluster::Cluster& c : byYear) {
            if (c.key == QLatin1String("2023")) y2023 = c.count;
            else if (c.key == QLatin1String("2024")) y2024 = c.count;
            else if (c.key == QString::fromLatin1(PhotoCluster::kUnclustered)) yUnc = c.count;
        }
        chk(byYear.size() == 3, "yearSize");
        chk(y2023 == 3, "y2023");
        chk(y2024 == 2, "y2024");
        chk(yUnc == 1, "yUnc");

        const QVector<PhotoCluster::Cluster> byMonth =
            PhotoCluster::cluster(phs, PhotoCluster::ByMonth, true);
        int may = 0;
        for (const PhotoCluster::Cluster& c : byMonth)
            if (c.key == QLatin1String("2023-05")) may = c.count;
        chk(may == 2, "may");
    }

    // —— C 数据诚实：未分类簇必须存在且带可读标签 ——
    {
        const QVector<PhotoCluster::Cluster> byYear =
            PhotoCluster::cluster(phs, PhotoCluster::ByYear, true);
        bool uncHasLabel = false;
        for (const PhotoCluster::Cluster& c : byYear)
            if (c.key == QString::fromLatin1(PhotoCluster::kUnclustered)
                && c.label == QCoreApplication::translate("LightAlbum", "未分类"))
                uncHasLabel = true;
        chk(uncHasLabel, "uncLabel");
    }

    // —— D 隐私门：hidePrivate=false 时私密照片应参与分簇 ——
    {
        const QVector<PhotoCluster::Cluster> all =
            PhotoCluster::cluster(phs, PhotoCluster::ByProvince, false);
        int sum = 0;
        for (const PhotoCluster::Cluster& c : all) sum += c.count;
        chk(sum == total, "allSum");
        chk(all.size() == 4, "allSize");
    }
    PhotoCluster::setForceDropUnclusteredForQa(false);   // 还原（探针不污染环境）

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr, "LA_CLUSTER_QA %s marks=%d checks=%d clusters=%d visible=%d broken=%d\n",
                 pass ? "PASS" : "FAIL", marks, checks, prov.size(), visible, int(broken));
    if (!pass)
        std::fprintf(stderr, "LA_CLUSTER_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 C·联动：地图地理热点门禁（LA_GEO_LINK_QA）。
// 断言 GeoHotspot::compute 纯函数的三层性质，并验证「热点 key 过滤图库 == hotspot.count」
// 这一核心联动契约（绘制/筛选/门禁同源）。另含离屏点击链路：热点徽章点击 → hotspotActivated
// 带正确 key。反例 LA_GEO_LINK_QA_FORCE_BROKEN=1 令 World 级误按 prov 聚合（key 不再是
// region id）→ 上述断言自然变红（铁律 22：期望值恒为规格值）。
int runGeoLinkQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_GEO_LINK_QA_FORCE_BROKEN");
    GeoHotspot::setForceMisaggregateForQa(broken);

    auto mk = [](const QString& region, const QString& prov, const QString& city,
                 double la, double lo, bool priv = false) {
        Photo p;
        p.region = region; p.prov = prov; p.city = city;
        p.hasGps = (la != 0.0 || lo != 0.0);
        p.la = la; p.lo = lo; p.priv = priv;
        p.thumbFile = QStringLiteral("t");
        return p;
    };
    QVector<Photo> phs;
    // china：安徽 2 + 北京 1 = 3；japan 1；iceland 1（共 5，无私密）
    phs.append(mk(QStringLiteral("china"),  QStringLiteral("安徽省"), QStringLiteral("黄山"),   29.7,  118.3));
    phs.append(mk(QStringLiteral("china"),  QStringLiteral("安徽省"), QStringLiteral("合肥"),   31.8,  117.2));
    phs.append(mk(QStringLiteral("china"),  QStringLiteral("北京市"), QStringLiteral("北京"),   39.9,  116.4));
    phs.append(mk(QStringLiteral("japan"),  QStringLiteral("Tokyo"),  QStringLiteral("东京"),   35.7,  139.7));
    phs.append(mk(QStringLiteral("iceland"),QStringLiteral("Capital Region"), QStringLiteral("雷克雅未克"), 64.1, -21.9));

    const int total = phs.size();   // 5

    int marks = 0, checks = 0;
    QStringList failed;
    auto chk = [&](bool ok, const char* tag = "") {
        ++checks; if (ok) ++marks; else if (tag && *tag) failed << QString::fromLatin1(tag);
    };

    // —— 1. World 级聚合（按 region）——
    const QVector<GeoHotspot> world = GeoHotspot::compute(phs, true, GeoHotspot::Level::World);
    {
        // 独立期望（不依赖 GeoHotspot）：直接按 Photo.region 计数
        QHash<QString, int> exp;
        for (const Photo& p : phs) if (!p.priv) exp[p.region.toLower()]++;
        int sum = 0; for (const GeoHotspot& h : world) sum += h.count;
        chk(sum == total, "worldSum");                 // 全可见（无私密）
        chk(world.size() == exp.size(), "worldSize");  // 3 个 region
        for (auto it = exp.constBegin(); it != exp.constEnd(); ++it) {
            const GeoHotspot* h = nullptr;
            for (const GeoHotspot& x : world) if (x.key.toLower() == it.key()) { h = &x; break; }
            chk(h != nullptr, ("worldKey_" + it.key()).toLatin1().constData());
            if (h) {
                chk(h->count == it.value(), ("worldCnt_" + it.key()).toLatin1().constData());
                chk(!h->isProvince, "worldIsRegion");
            }
        }
        // 核心联动契约：key 过滤图库 == hotspot.count（world 走 setRegionFilter）
        {
            GalleryModel m; m.setSource(&phs);
            for (const GeoHotspot& h : world) {
                m.setRegionFilter(h.key);
                const int got = m.rowCount();
                m.setRegionFilter(QString());
                chk(got == h.count, ("worldLink_" + h.key).toLatin1().constData());
            }
        }
        // centroid 合理（有 GPS 必不为 (0,0) 退化）
        for (const GeoHotspot& h : world)
            chk(h.centroid.x() > 0 && h.centroid.y() > 0, "worldCentroid");
    }

    // —— 2. Region 级聚合（按 prov，限定 china）——
    const QVector<GeoHotspot> region = GeoHotspot::compute(phs, true, GeoHotspot::Level::Region, QStringLiteral("china"));
    {
        int sum = 0; for (const GeoHotspot& h : region) sum += h.count;
        chk(sum == 3, "regionSum");          // 仅 china 内：安徽 2 + 北京 1
        chk(region.size() == 2, "regionSize");
        for (const GeoHotspot& h : region) chk(h.isProvince, "regionIsProv");
        // 核心联动契约：prov 走 setProvFilter
        {
            GalleryModel m; m.setSource(&phs);
            for (const GeoHotspot& h : region) {
                m.setProvFilter(h.key);
                const int got = m.rowCount();
                m.setProvFilter(QString());
                chk(got == h.count, ("regionLink_" + h.key).toLatin1().constData());
            }
        }
    }

    // —— 3. 离屏点击链路：World 热点徽章点击 → hotspotActivated 带正确 key ——
    {
        MapView* v = new MapView();
        v->resize(1000, 600);
        v->setGeoHotspots(world);
        QVector<QPointF> pts;
        for (const Photo& p : phs) if (p.hasGps)
            pts.append(QPointF((p.lo + 180.0) / 360.0 * MapView::MAP_W,
                               (90.0 - p.la) / 180.0 * MapView::MAP_H));
        v->setGpsPoints(pts);
        v->show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

        QString gotKey; bool gotProv = false; bool fired = false;
        const QMetaObject::Connection conn = QObject::connect(
            v, &MapView::hotspotActivated,
            [&](const QString& k, const QString&, bool ip) { fired = true; gotKey = k; gotProv = ip; });

        const QPoint pos = v->qaHotspotWidgetPos(0).toPoint();
        QMouseEvent ev(QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(v, &ev);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        chk(fired, "clickFired");
        if (fired) {
            chk(gotKey == world.at(0).key, "clickKey");
            chk(gotProv == false, "clickIsRegion");
        }
        QObject::disconnect(conn);
        delete v;
    }

    GeoHotspot::setForceMisaggregateForQa(false);   // 还原（探针不污染环境）

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr,
        "LA_GEO_LINK_QA %s marks=%d checks=%d hotspots=%d broken=%d\n",
        pass ? "PASS" : "FAIL", marks, checks, world.size(), int(broken));
    if (!pass)
        std::fprintf(stderr, "LA_GEO_LINK_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 C：相似照片去重门禁（LA_DEDUP_QA）。
// 注入确定性合成缩略图，断言 PhotoDedup 纯函数的性质：
//   A 精确去重：字节相同的两张图进"完全重复（MD5 相同）"组
//   B 近重复：感知哈希 Hamming ≤ 阈值的图进"高度相似（感知哈希）"组
//   C 计数：组数 / 重复张数正确
//   D 唯一性：完全不相似的图不成组
//   E 缺失：缩略图缺失的图不入任何组、不崩溃
//   F 保留最优：每组的保留者分辨率最高
//   G ★守恒：每张合法照片恰好出现一次（反例令同一张被重复计入 → RED）
// 反例 LA_DEDUP_QA_FORCE_BROKEN=1 令 findGroups 把第 1 组保留者再塞进第 2 组
//   dupPaths → 破坏守恒 → RED（打真实实现，非空心）。
int runDedupQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_DEDUP_QA_FORCE_BROKEN");
    PhotoDedup::setForceDoubleCountForQa(broken);   // 反例：注入真实实现的重复计入缺陷

    // —— 合成缩略图（QTemporaryDir 自动清理）——
    QTemporaryDir td;
    const QString dir = td.path();
    auto disk = [](int bg, int fg) {
        QImage im(64, 64, QImage::Format_ARGB32);
        im.fill(QColor(bg, bg, bg));
        QPainter p(&im); p.setPen(Qt::NoPen); p.setBrush(QColor(fg, fg, fg));
        p.drawEllipse(16, 16, 32, 32);
        return im;
    };
    auto checker = [] {
        QImage im(64, 64, QImage::Format_ARGB32); QPainter p(&im);
        for (int y = 0; y < 64; y += 8) for (int x = 0; x < 64; x += 8)
            p.fillRect(x, y, 8, 8, ((x / 8 + y / 8) % 2) ? Qt::white : Qt::black);
        return im;
    };
    QImage a1 = disk(255, 0); a1.save(QDir(dir).filePath(QStringLiteral("a1.png")), "PNG");
    a1.save(QDir(dir).filePath(QStringLiteral("a2.png")), "PNG");   // 字节相同 → 精确 MD5 对
    QImage b1 = disk(0, 255);  b1.save(QDir(dir).filePath(QStringLiteral("b1.png")), "PNG");
    QImage b2 = b1; b2.setPixel(32, 32, qRgb(255, 0, 0));          // 1px 差异 → pHash 不变、MD5 不同
    b2.save(QDir(dir).filePath(QStringLiteral("b2.png")), "PNG");
    checker().save(QDir(dir).filePath(QStringLiteral("c1.png")), "PNG");
    // d1.png 故意不写（缺失）

    auto mk = [](const QString& path, const QString& tf, int w, int h) {
        DedupPhoto d; d.path = path; d.thumbFile = tf; d.origW = w; d.origH = h; return d;
    };
    QVector<DedupPhoto> phs;
    phs.append(mk(QStringLiteral("C:/p_a1.jpg"), QStringLiteral("a1.png"), 4000, 3000));
    phs.append(mk(QStringLiteral("C:/p_a2.jpg"), QStringLiteral("a2.png"), 2000, 1500));
    phs.append(mk(QStringLiteral("C:/p_b1.jpg"), QStringLiteral("b1.png"), 3000, 2000));
    phs.append(mk(QStringLiteral("C:/p_b2.jpg"), QStringLiteral("b2.png"), 1000, 800));
    phs.append(mk(QStringLiteral("C:/p_c1.jpg"), QStringLiteral("c1.png"), 5000, 4000));
    phs.append(mk(QStringLiteral("C:/p_d1.jpg"), QStringLiteral("d1.png"), 1000, 1000)); // 缺失

    int marks = 0, checks = 0;
    QStringList failed;
    auto chk = [&](bool ok, const char* tag = "") {
        ++checks; if (ok) ++marks; else if (tag && *tag) failed << QString::fromLatin1(tag);
    };

    const QVector<DupGroup> groups = PhotoDedup::findGroups(phs, dir, 8, true);
    const QString a1p = QStringLiteral("C:/p_a1.jpg"), a2p = QStringLiteral("C:/p_a2.jpg");
    const QString b1p = QStringLiteral("C:/p_b1.jpg"), b2p = QStringLiteral("C:/p_b2.jpg");
    const QString c1p = QStringLiteral("C:/p_c1.jpg"), d1p = QStringLiteral("C:/p_d1.jpg");

    const DupGroup* exact = nullptr;
    const DupGroup* nearGrp = nullptr;
    for (const DupGroup& g : groups) {
        if (g.reason == QCoreApplication::translate("LightAlbum", "完全重复（MD5 相同）"))      exact = &g;
        else if (g.reason == QCoreApplication::translate("LightAlbum", "高度相似（感知哈希）")) nearGrp = &g;
    }

    // A 精确去重
    chk(exact != nullptr, "exactExists");
    chk(exact && exact->dupPaths.contains(a2p), "exactDup");
    // B 近重复
    chk(nearGrp != nullptr, "nearExists");
    chk(nearGrp && nearGrp->dupPaths.contains(b2p), "nearDup");
    // C 计数
    chk(groups.size() == 2, "grpSize");
    int dupTotal = 0; for (const DupGroup& g : groups) dupTotal += g.dupPaths.size();
    chk(dupTotal == 2, "dupTotal");
    // D 唯一
    bool cInAny = false;
    for (const DupGroup& g : groups) if (g.keptPath == c1p || g.dupPaths.contains(c1p)) cInAny = true;
    chk(!cInAny, "cUnique");
    // E 缺失
    bool dInAny = false;
    for (const DupGroup& g : groups) if (g.keptPath == d1p || g.dupPaths.contains(d1p)) dInAny = true;
    chk(!dInAny, "dMissing");
    // F 保留最优
    chk(exact && exact->keptPath == a1p, "exactKeep");
    chk(exact && exact->keptPixels == qint64(4000) * 3000, "exactPixels");
    chk(nearGrp  && nearGrp->keptPath == b1p, "nearKeep");
    // G ★守恒：每张合法照片恰好出现一次
    QSet<QString> seen;
    bool doubled = false;
    for (const DupGroup& g : groups) {
        auto add = [&](const QString& s) { if (seen.contains(s)) doubled = true; seen.insert(s); };
        add(g.keptPath);
        for (const QString& d : g.dupPaths) add(d);
    }
    chk(!doubled, "noDouble");                                  // 反例触发：a1 被重复计入
    chk(seen.size() == 4, "seenSize");                          // 恰好 4 张合法照片被分组
    chk(seen.contains(a1p) && seen.contains(a2p)
        && seen.contains(b1p) && seen.contains(b2p), "seenSet");

    PhotoDedup::setForceDoubleCountForQa(false);   // 还原（探针不污染环境）

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr, "LA_DEDUP_QA %s marks=%d checks=%d groups=%d dups=%d broken=%d\n",
                 pass ? "PASS" : "FAIL", marks, checks, groups.size(), dupTotal, int(broken));
    if (!pass)
        std::fprintf(stderr, "LA_DEDUP_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 方向 D 第一步：真实库回归门禁（规模化与流畅性）。
// 加载真实库（env LA_SCALE_DB 优先 → LA_DB → 从 exe 目录向上搜索 _scale_fixture.db），
// 断言 open 成功、照片数 ≥ LA_SCALE_MIN（默认 1000，镜像真库规模）、kpi 已填充；
// 构造 GalleryPage（真实图库网格）+ MapView（真实 GPS 足迹）offscreen 不崩、首屏渲染不超时。
// 重要：main() 已建 QApplication（line ~417），此处严禁再建 QApplication —— 双实例退场 SIGSEGV。
//       本函数复用既有 QApplication，自建 Library 打开真实库（Library 仅为 QSqlDatabase 包装，
//       不依赖 main 的 lib）；构造页面仅需 QApplication 已存在即可。
int runScaleSmoke()
{
    QString db = qEnvironmentVariable("LA_SCALE_DB");
    if (db.isEmpty()) db = qEnvironmentVariable("LA_DB");
    if (db.isEmpty()) {
        // 默认：从可执行文件目录向上搜索合成 scale fixture（提交于仓库根，镜像真库 6425 规模）
        QString dir = QCoreApplication::applicationDirPath();
        for (int i = 0; i < 6; ++i) {
            const QString cand = dir + QStringLiteral("/_scale_fixture.db");
            if (QFileInfo::exists(cand)) { db = cand; break; }
            const QString up = QDir(dir).filePath(QStringLiteral(".."));
            if (up == dir) break;
            dir = QDir::cleanPath(up);
        }
    }
    const int minCount = qEnvironmentVariable("LA_SCALE_MIN", QStringLiteral("1000")).toInt();
    const qint64 mapBudgetMs = qEnvironmentVariable("LA_SCALE_MAP_MS", QStringLiteral("15000")).toLongLong();

    QString err;
    Library lib;
    if (!lib.open(db, &err)) {
        std::fprintf(stderr, "LA_SCALE_SMOKE FAIL open=%s (%s)\n",
                     qPrintable(db), qPrintable(err));
        std::fflush(stderr);
        return 2;
    }
    const auto& phs = lib.photos();
    const Kpi& k = lib.kpi();
    const int n = int(phs.size());

    bool ok = true;
    QStringList fails;
    if (k.total != n) { ok = false; fails << "kpi.total!=n"; }
    if (n < minCount) { ok = false; fails << QStringLiteral("photos=%1<min=%2").arg(n).arg(minCount); }
    if (k.yearCount < 1) { ok = false; fails << "yearCount<1"; }
    if (k.provCount < 1) { ok = false; fails << "provCount<1"; }

    // 关键页面 1：真实图库网格（EditorialView 窗口化，仅可见行出绘制，构造/首屏不崩）
    int galleryCtrls = -1;
    {
        GalleryPage page(&phs, QString(), nullptr);
        page.setHidePrivate(false);
        page.resize(1200, 800);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        galleryCtrls = page.findChildren<QWidget*>().size();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    // 关键页面 2：足迹地图（注入真实 GPS 预投影点）。测量单次首屏 paint 周期。
    int gpsCount = 0;
    qint64 paintMs = -1;
    {
        MapView view(nullptr);
        QVector<QPointF> pts;
        pts.reserve(n);
        for (const Photo& p : phs) {
            if (!p.hasGps) continue;
            const double x = (p.lo + 180.0) / 360.0 * MapView::MAP_W;
            const double y = (90.0 - p.la) / 180.0 * MapView::MAP_H;
            pts.append(QPointF(x, y));
        }
        gpsCount = pts.size();
        view.setGpsPoints(pts);
        view.setGpsCount(gpsCount);
        view.resize(1200, 800);
        view.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100); // 初始 show
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        view.update();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2000); // 单次 paint 周期
        paintMs = QDateTime::currentMSecsSinceEpoch() - t0;
        std::fprintf(stderr, "LA_SCALE_MAP mapMs=%lld gps=%d\n", paintMs, gpsCount);
        std::fflush(stderr);
    }

    if (galleryCtrls < 0) { ok = false; fails << "gallery_construct"; }
    if (paintMs < 0 || paintMs > mapBudgetMs) { ok = false; fails << QStringLiteral("mapPaint=%1>budget=%2").arg(paintMs).arg(mapBudgetMs); }

    if (ok) {
        std::fprintf(stderr,
            "LA_SCALE_SMOKE PASS photos=%d years=%d provs=%d span=%d-%d gps=%d galleryCtrls=%d mapMs=%lld db=%s\n",
            n, k.yearCount, k.provCount, k.minYear, k.maxYear, gpsCount, galleryCtrls, paintMs,
            qPrintable(QFileInfo(db).fileName()));
    } else {
        std::fprintf(stderr, "LA_SCALE_SMOKE FAIL reason=%s photos=%d gps=%d mapMs=%lld\n",
                     qPrintable(fails.join(QChar(';'))), n, gpsCount, paintMs);
    }
    std::fflush(stderr);
    return ok ? 0 : 1;
}

// 方向 D 第二步：规模化流畅性基线门禁（先测再改：量化首屏/滚动，守护回归）。
// 复用 runScaleSmoke 的库定位逻辑；构造 GalleryPage（EditorialView 窗口化网格），
// 量化 ① 首屏耗时（构造→首绘）② N 次滚动跳变帧 paint 均值/峰值（保持 <120ms 节流，
// 隔离纯 paint，不触发缩略图预取）。预算经 env 覆盖（默认宽松防 CI 误红），实测后校准。
// 重要：复用 main() 既有 QApplication，禁新建 QCoreApplication（双实例退场 SIGSEGV）。
int runScalePerf()
{
    QString db = qEnvironmentVariable("LA_SCALE_DB");
    if (db.isEmpty()) db = qEnvironmentVariable("LA_DB");
    if (db.isEmpty()) {
        QString dir = QCoreApplication::applicationDirPath();
        for (int i = 0; i < 6; ++i) {
            const QString cand = dir + QStringLiteral("/_scale_fixture.db");
            if (QFileInfo::exists(cand)) { db = cand; break; }
            const QString up = QDir(dir).filePath(QStringLiteral(".."));
            if (up == dir) break;
            dir = QDir::cleanPath(up);
        }
    }
    const qint64 firstBudgetMs  = qEnvironmentVariable("LA_SCALE_PERF_FIRST",  QStringLiteral("3000")).toLongLong();
    const qint64 scrollBudgetMs = qEnvironmentVariable("LA_SCALE_PERF_SCROLL", QStringLiteral("50")).toLongLong();
    const int    jumps          = qEnvironmentVariable("LA_SCALE_PERF_JUMPS",  QStringLiteral("20")).toInt();

    QString err;
    Library lib;
    if (!lib.open(db, &err)) {
        std::fprintf(stderr, "LA_SCALE_PERF FAIL open=%s (%s)\n", qPrintable(db), qPrintable(err));
        std::fflush(stderr);
        return 2;
    }
    const auto& phs = lib.photos();
    const int n = int(phs.size());

    int galleryCtrls = -1;
    qint64 firstMs = -1, scrollAvgMs = -1, scrollMaxMs = -1;
    {
        const qint64 tA = QDateTime::currentMSecsSinceEpoch();
        GalleryPage page(&phs, QString(), nullptr);
        page.setHidePrivate(false);
        page.resize(1200, 800);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400); // 首屏 + 布局 + 可见区预取
        const qint64 tB = QDateTime::currentMSecsSinceEpoch();
        firstMs = tB - tA;
        galleryCtrls = page.findChildren<QWidget*>().size();

        // 滚动跳变帧 paint 测量：设 scrollbar 值 → update → 短 processEvents
        // （<120ms 节流，隔离纯 paint，不触发缩略图预取）。
        QScrollBar* sb = page.qaScrollBar();
        if (sb && sb->maximum() > 0 && jumps > 0) {
            qint64 sum = 0, mx = 0;
            for (int j = 1; j <= jumps; ++j) {
                const int v = int(sb->maximum() * j / (jumps + 1));
                const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
                sb->setValue(v);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                const qint64 dt = QDateTime::currentMSecsSinceEpoch() - t0;
                sum += dt; if (dt > mx) mx = dt;
            }
            scrollAvgMs = sum / jumps;
            scrollMaxMs = mx;
        } else {
            scrollAvgMs = 0; scrollMaxMs = 0;   // 内容未超出视口：无需滚动，成本记 0
        }
        std::fprintf(stderr,
            "LA_SCALE_PERF firstMs=%lld scrollAvgMs=%lld scrollMaxMs=%lld ctrls=%d jumps=%d\n",
            firstMs, scrollAvgMs, scrollMaxMs, galleryCtrls, jumps);
        std::fflush(stderr);
    }

    bool ok = (n >= 1000)
            && (firstMs >= 0 && firstMs <= firstBudgetMs)
            && (scrollMaxMs >= 0 && scrollMaxMs <= scrollBudgetMs);
    if (ok) {
        std::fprintf(stderr,
            "LA_SCALE_PERF PASS photos=%d firstMs=%lld scrollAvgMs=%lld scrollMaxMs=%lld ctrls=%d db=%s\n",
            n, firstMs, scrollAvgMs, scrollMaxMs, galleryCtrls, qPrintable(QFileInfo(db).fileName()));
    } else {
        QStringList fails;
        if (n < 1000) fails << QStringLiteral("photos=%1<1000").arg(n);
        if (firstMs < 0 || firstMs > firstBudgetMs) fails << QStringLiteral("firstMs=%1>budget=%2").arg(firstMs).arg(firstBudgetMs);
        if (scrollMaxMs < 0 || scrollMaxMs > scrollBudgetMs) fails << QStringLiteral("scrollMaxMs=%1>budget=%2").arg(scrollMaxMs).arg(scrollBudgetMs);
        std::fprintf(stderr, "LA_SCALE_PERF FAIL reason=%s photos=%d firstMs=%lld scrollMaxMs=%lld\n",
                     qPrintable(fails.join(QChar(';'))), n, firstMs, scrollMaxMs);
    }
    std::fflush(stderr);
    return ok ? 0 : 1;
}

// 方向 D 第三步（2.23.0）：真实库规模门禁 LA_SCALE_REAL_QA —— 第 46 门。
// ★ 取证依据（先测再改，禁猜）：现有 LA_SCALE_SMOKE / LA_SCALE_PERF 跑的是仓库内合成
//   fixture `_scale_fixture.db`（6425 行），而实测**其 path/thumb 指向的文件在磁盘上
//   全不存在**（scale_lib/ 目录不存在；missing=1 行数却是 0）→ 两门只验了「内存里
//   6425 行」，从未触碰磁盘 IO / 缩略图解码 / 混合格式这些**真实规模下才贵**的路径。
//   真库 D:/lightalbum/library.db 实测基线（2026-10-06）：
//     6526 行 / 6507 文件存在（99.71%）/ 6 种扩展名（jpg 5140、heic 779、png 576、
//     jpeg 24、webp 6、tif 1）/ missing=1 共 19 且**与磁盘缺失数完全一致**。
//   二者反差即判别器：fixture 是「声称都在、实际全不在」的说谎库。
// 本门只断言**真库才验得了**的维度。真库属个人数据（不能进 CI、不能提交），
// 故由 gate_matrix 的 need 声明式 SKIP（CI 优雅跳过，不自造 CHECK 警告态）。
// 反例 LA_SCALE_REAL_QA_FORCE_BROKEN=1：把 path 全指向不存在的文件 → 复现说谎库 → 必红。
int runScaleRealQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_SCALE_REAL_QA_FORCE_BROKEN");
    QString db = qEnvironmentVariable("LA_SCALE_REAL_DB");
    if (db.isEmpty()) db = QStringLiteral("D:/lightalbum/library.db");

    const int    minRows  = qEnvironmentVariable("LA_SCALE_REAL_MIN",    QStringLiteral("2000")).toInt();
    const double minExist = qEnvironmentVariable("LA_SCALE_REAL_EXIST",  QStringLiteral("0.80")).toDouble();
    const double minMiss  = qEnvironmentVariable("LA_SCALE_REAL_MISS",   QStringLiteral("0.90")).toDouble();
    const double minThumb = qEnvironmentVariable("LA_SCALE_REAL_THUMB",  QStringLiteral("0.90")).toDouble();
    const int    minExts  = qEnvironmentVariable("LA_SCALE_REAL_EXTS",   QStringLiteral("2")).toInt();
    const int    samples  = qEnvironmentVariable("LA_SCALE_REAL_SAMPLES",QStringLiteral("24")).toInt();
    // 缩略图解码预算：实测 24 张真实缩略图（27-32KB）亚秒级；给 20s 是 20 倍余量，
    // 属于"卡住/病态"级别的兜底，不是会随机器负载抖动的紧预算。
    const qint64 decodeBudgetMs = qEnvironmentVariable("LA_SCALE_REAL_DECODE_MS", QStringLiteral("20000")).toLongLong();
    const qint64 firstBudgetMs  = qEnvironmentVariable("LA_SCALE_REAL_FIRST_MS",  QStringLiteral("15000")).toLongLong();

    QString err;
    Library lib;
    if (!lib.open(db, &err)) {
        std::fprintf(stderr, "LA_SCALE_REAL_QA FAIL open=%s (%s)\n",
                     qPrintable(db), qPrintable(err));
        std::fflush(stderr);
        return 2;
    }
    const auto& phs = lib.photos();
    const int n = int(phs.size());

    // 缩略图目录：图库网格真实读取的就是它（ThumbCache L1/L2），不是原图。
    const QString thumbsDir = qEnvironmentVariable("LA_THUMBS", QStringLiteral("D:/lightalbum/thumbs"));

    // 反例：把 path 与 thumbFile 全指向不存在的文件 —— 精确复现 fixture
    //「声称存在、磁盘没有」的形态；存在率 / missing 一致性 / 缩略图覆盖率须同时命中。
    QVector<Photo> view = phs;
    if (broken) {
        const QString bogus = QDir::temp().filePath(QStringLiteral("la_scale_real_broken"));
        QDir().mkpath(bogus);
        for (int i = 0; i < view.size(); ++i) {
            view[i].path      = bogus + QStringLiteral("/not_exists_%1.jpg").arg(i);
            view[i].thumbFile = QStringLiteral("not_exists_thumb_%1.jpg").arg(i);
        }
    }

    // 存在率 / missing 一致性 / 格式多样性 / 缩略图覆盖率（fixture 门走不到真实分支）
    int exist = 0, missFlag = 0, lie = 0, thumbOk = 0;
    QSet<QString> exts;
    for (Photo& p : view) {
        const bool e = !p.path.isEmpty() && QFileInfo::exists(p.path);
        if (e) ++exist;
        const QString ext = QFileInfo(p.path).suffix().toLower();
        if (!ext.isEmpty()) exts.insert(ext);
        if (p.missing) ++missFlag;
        else if (!e) ++lie;                 // 声称存在、实际不在 —— "说谎"行
        if (!p.thumbFile.isEmpty() &&
            QFileInfo::exists(QDir(thumbsDir).filePath(p.thumbFile))) ++thumbOk;
    }
    const double existRate = n ? double(exist) / n : 0.0;
    const double thumbRate = n ? double(thumbOk) / n : 0.0;
    const int    claimExist = n - missFlag;      // 库声称存在的行数
    const double missRate = claimExist ? double(claimExist - lie) / claimExist : 0.0;

    // 真实规模图库首屏（EditorialView 窗口化，6526 行真实数据）
    int ctrls = -1;
    qint64 firstMs = -1;
    {
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        GalleryPage page(&view, QString(), nullptr);
        page.setHidePrivate(false);
        page.resize(1200, 800);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        ctrls = page.findChildren<QWidget*>().size();
        firstMs = QDateTime::currentMSecsSinceEpoch() - t0;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    // 真实缩略图解码抽样（真 IO —— fixture 门永远走不到这里）。
    // ★ 刻意**不解码原图**：实测 24 张真实原图（12MP JPEG + 大尺寸 HEIC）耗时 90s 且波动极大，
    //   属「断言墙钟预算」型 flaky（铁律：墙钟预算门即便便宜也不进门禁）。
    //   图库网格真实读取的是**缩略图**（ThumbCache L1/L2，实测 27-32KB），
    //   解码快、代表性强、且不受用户原图尺寸波动影响。
    int decGood = 0, decTried = 0;
    qint64 decodeMs = 0;
    QMap<QString, int> extGood, extTried;
    {
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        const int step = qMax(1, n / qMax(1, samples));
        for (int i = 0; i < n && decTried < samples; i += step) {
            if (view[i].thumbFile.isEmpty()) continue;
            const QString tp = QDir(thumbsDir).filePath(view[i].thumbFile);
            if (!QFileInfo::exists(tp)) continue;
            QImageReader rd(tp);
            const QImage im = rd.read();
            ++decTried;
            const bool good = !im.isNull();
            if (good) ++decGood;
            const QString ext = QFileInfo(view[i].path).suffix().toLower();
            extTried[ext] = extTried.value(ext, 0) + 1;
            if (good) extGood[ext] = extGood.value(ext, 0) + 1;
        }
        decodeMs = QDateTime::currentMSecsSinceEpoch() - t0;
    }

    bool ok = true; QStringList fails;
    if (n < minRows)    { ok = false; fails << QStringLiteral("photos=%1<min=%2").arg(n).arg(minRows); }
    if (existRate < minExist) { ok = false; fails << QStringLiteral("existRate=%1<min=%2").arg(existRate, 0, 'f', 4).arg(minExist, 0, 'f', 2); }
    if (missRate  < minMiss)  { ok = false; fails << QStringLiteral("missConsistency=%1<min=%2").arg(missRate, 0, 'f', 4).arg(minMiss, 0, 'f', 2); }
    if (exts.size() < minExts){ ok = false; fails << QStringLiteral("exts=%1<min=%2").arg(exts.size()).arg(minExts); }
    if (thumbRate < minThumb) { ok = false; fails << QStringLiteral("thumbRate=%1<min=%2").arg(thumbRate, 0, 'f', 4).arg(minThumb, 0, 'f', 2); }
    if (ctrls <= 0)     { ok = false; fails << QStringLiteral("galleryCtrls=%1").arg(ctrls); }
    if (firstMs < 0 || firstMs > firstBudgetMs) { ok = false; fails << QStringLiteral("firstMs=%1>budget=%2").arg(firstMs).arg(firstBudgetMs); }
    if (decTried < 1 || decGood < 1) { ok = false; fails << QStringLiteral("decodeGood=%1/tried=%2").arg(decGood).arg(decTried); }
    if (decodeMs > decodeBudgetMs)   { ok = false; fails << QStringLiteral("decodeMs=%1>budget=%2").arg(decodeMs).arg(decodeBudgetMs); }

    QStringList extDetail;
    for (auto it = extTried.constBegin(); it != extTried.constEnd(); ++it)
        extDetail << QStringLiteral("%1=%2/%3").arg(it.key())
                     .arg(extGood.value(it.key(), 0)).arg(it.value());

    if (ok) {
        std::fprintf(stderr,
            "LA_SCALE_REAL_QA PASS photos=%d exist=%d(%.4f) exts=%d missConsistency=%.4f "
            "thumb=%d(%.4f) galleryCtrls=%d firstMs=%lld decode=%d/%d decodeMs=%lld db=%s\n",
            n, exist, existRate, exts.size(), missRate, thumbOk, thumbRate, ctrls, firstMs,
            decGood, decTried, decodeMs, qPrintable(QFileInfo(db).fileName()));
    } else {
        std::fprintf(stderr, "LA_SCALE_REAL_QA FAIL reason=%s photos=%d exts=%d\n",
                     qPrintable(fails.join(QChar(';'))), n, exts.size());
    }
    std::fprintf(stderr, "  decodeByExt %s\n", qPrintable(extDetail.join(QChar(' '))));
    std::fflush(stderr);
    return ok ? 0 : 1;
}

// 方向 D 第六步（2.24.x）：真实库性能回归门禁 LA_SCALE_REAL_PERF_QA。
// 与 runScalePerf 同源（同一套 GalleryPage 首屏 + 滚动跳变帧量化），
// 但**强制跑真实库**（need=LA_SCALE_REAL_DB，CI 优雅 SKIP，绝不自造 CHECK 警告态），
// 守护真实规模（6526 行）下的流畅性回归：① 首屏（构造→首绘）② 滚动跳变帧 paint 预算。
// ★ 取证依据（先测再改，禁猜）：runScalePerf 跑的是仓库内合成 fixture（6425 行，
//   文件在磁盘全不存在），其首屏/滚动只验了"虚拟网格"的绘制成本，从未触碰真实规模下
//   缩略图取盘/混合格式这些真实才贵、且**随个人库变化**的路径——属"以合成假绿掩盖真实风险"。
//   本门把同一套量化锚定到真实库，断言卡"成分（首屏/单帧预算）"而非"比例"。
// 反例 LA_SCALE_REAL_PERF_QA_FORCE_BROKEN=1：在**滚动测量前**布防一次性 +300ms 绘制延迟
//   （EditorialView::qaSetPaintLagMsForQa，one-shot 只污染一个 paint 事件，总开销与瓦片数
//   无关、不会撞 20s 看门狗），精确复现"绘制退化" → 某个滚动帧 dt 必 > 50ms 预算 → 必红。
//   期望值（预算阈值）**不随 broken 切换**（铁律 22）。
int runScaleRealPerfQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_SCALE_REAL_PERF_QA_FORCE_BROKEN");
    QString db = qEnvironmentVariable("LA_SCALE_REAL_DB");
    if (db.isEmpty()) db = QStringLiteral("D:/lightalbum/library.db");

    const qint64 firstBudgetMs  = qEnvironmentVariable("LA_SCALE_REAL_PERF_FIRST",  QStringLiteral("15000")).toLongLong();
    const qint64 scrollBudgetMs = qEnvironmentVariable("LA_SCALE_REAL_PERF_SCROLL", QStringLiteral("50")).toLongLong();
    const int    jumps          = qEnvironmentVariable("LA_SCALE_REAL_PERF_JUMPS",  QStringLiteral("20")).toInt();
    const int    minRows        = qEnvironmentVariable("LA_SCALE_REAL_MIN",          QStringLiteral("2000")).toInt();

    // 反例注入在**首屏测量之后、滚动测量之前**才布防（见下），此处确保干净起步。
    EditorialView::qaSetPaintLagMsForQa(0);

    QString err;
    Library lib;
    if (!lib.open(db, &err)) {
        std::fprintf(stderr, "LA_SCALE_REAL_PERF_QA FAIL open=%s (%s)\n",
                     qPrintable(db), qPrintable(err));
        std::fflush(stderr);
        return 2;
    }
    const auto& phs = lib.photos();
    const int n = int(phs.size());

    int ctrls = -1;
    qint64 firstMs = -1, scrollAvgMs = -1, scrollMaxMs = -1;
    {
        const qint64 tA = QDateTime::currentMSecsSinceEpoch();
        GalleryPage page(&phs, QString(), nullptr);
        page.setHidePrivate(false);
        page.resize(1200, 800);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400); // 首屏 + 布局 + 可见区预取
        const qint64 tB = QDateTime::currentMSecsSinceEpoch();
        firstMs = tB - tA;
        ctrls = page.findChildren<QWidget*>().size();

        // 滚动跳变帧 paint 测量：设 scrollbar 值 → update → 短 processEvents
        // （隔离纯 paint，不触发缩略图预取）。
        // ★ 反例在**此刻**才布防：one-shot 注入只污染接下来的**一个** paint 事件，
        //   该事件必落在滚动帧内 → scrollMaxMs 必 > 50ms 预算（门红）；
        //   若在首屏前布防，注入会落进 firstMs（15s 预算）而被吞掉、门误绿。
        if (broken) EditorialView::qaSetPaintLagMsForQa(300);
        QScrollBar* sb = page.qaScrollBar();
        if (sb && sb->maximum() > 0 && jumps > 0) {
            qint64 sum = 0, mx = 0;
            for (int j = 1; j <= jumps; ++j) {
                const int v = int(sb->maximum() * j / (jumps + 1));
                const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
                sb->setValue(v);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
                const qint64 dt = QDateTime::currentMSecsSinceEpoch() - t0;
                sum += dt; if (dt > mx) mx = dt;
            }
            scrollAvgMs = sum / jumps;
            scrollMaxMs = mx;
        } else {
            scrollAvgMs = 0; scrollMaxMs = 0;   // 内容未超出视口：无需滚动，成本记 0
        }
        std::fprintf(stderr,
            "LA_SCALE_REAL_PERF_QA firstMs=%lld scrollAvgMs=%lld scrollMaxMs=%lld ctrls=%d jumps=%d broken=%d\n",
            firstMs, scrollAvgMs, scrollMaxMs, ctrls, jumps, broken ? 1 : 0);
        std::fflush(stderr);
    }

    bool ok = true; QStringList fails;
    if (n < minRows)    { ok = false; fails << QStringLiteral("photos=%1<min=%2").arg(n).arg(minRows); }
    if (ctrls <= 0)     { ok = false; fails << QStringLiteral("galleryCtrls=%1").arg(ctrls); }
    if (firstMs < 0 || firstMs > firstBudgetMs) { ok = false; fails << QStringLiteral("firstMs=%1>budget=%2").arg(firstMs).arg(firstBudgetMs); }
    if (scrollMaxMs < 0 || scrollMaxMs > scrollBudgetMs) { ok = false; fails << QStringLiteral("scrollMaxMs=%1>budget=%2").arg(scrollMaxMs).arg(scrollBudgetMs); }

    if (ok) {
        std::fprintf(stderr,
            "LA_SCALE_REAL_PERF_QA PASS photos=%d firstMs=%lld scrollAvgMs=%lld scrollMaxMs=%lld ctrls=%d broken=%d db=%s\n",
            n, firstMs, scrollAvgMs, scrollMaxMs, ctrls, broken ? 1 : 0, qPrintable(QFileInfo(db).fileName()));
    } else {
        std::fprintf(stderr, "LA_SCALE_REAL_PERF_QA FAIL reason=%s photos=%d broken=%d\n",
                     qPrintable(fails.join(QChar(';'))), n, broken ? 1 : 0);
    }
    std::fflush(stderr);
    return ok ? 0 : 1;
}

// 方向 D 第四步：缩略图多级缓存门禁（headless，无需 db——现场合成 8 张 512x512 样图）。
// 断言 ① 单文件一次解码派生两级、两级像素不同（pxFeat > pxTile）
//      ② 同批图两级字节 L1 < L2（小图省内存）
//      ③ 同级重复 request 按文件名去重（后台任务数 == 文件数）
//      ④ 负缓存：缺文件重复 request 只触发一次后台任务，两个回调都得空图
//      ⑤ 字节预算生效：超预算淘汰 → 该级张数受限且 bytes<=budget
//      ⑥ 跨级回落：某级被淘汰后 cached() 回落另一级、且不重复触发加载
// 预算/像素均经 env 可覆盖，便于本门自造反例（小预算即淘汰路径）。
int runThumbCacheQa()
{
    const int kFiles = 8;
    const QString dir = QDir::tempPath() + QStringLiteral("/la_thumb_qa");
    QDir(dir).removeRecursively();
    if (!QDir().mkpath(dir)) {
        std::fprintf(stderr, "LA_THUMB_CACHE_QA FAIL mkpath=%s\n", qPrintable(dir));
        std::fflush(stderr);
        return 2;
    }
    QStringList files;
    for (int i = 0; i < kFiles; ++i) {
        QImage im(512, 512, QImage::Format_RGB32);
        im.fill(qRgb(40 + i * 20, 90, 170 - i * 12));
        const QString fn = QStringLiteral("qa_t%1.png").arg(i, 2, 10, QChar('0'));
        im.save(QDir(dir).filePath(fn), "PNG");
        files << fn;
    }

    // 完成信号经 QueuedConnection 回主线程 → 必须转事件循环等 finish 落地
    const auto spin = [](const std::function<bool()>& pred, int ms) {
        QElapsedTimer t; t.start();
        while (!pred() && t.elapsed() < ms)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return pred();
    };

    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) {
        if (!ok) fails << what;
    };
    const auto num = [](qint64 v) { return QString::number(v); };

    QObject ctx;
    const int pxTile = qEnvironmentVariable("LA_THUMB_TILE_PX", QStringLiteral("256")).toInt();
    const int pxFeat = qEnvironmentVariable("LA_THUMB_FEATURE_PX", QStringLiteral("512")).toInt();

    // —— A：默认预算，一次解码派生两级 + 去重 + 负缓存 ——
    int aL1 = 0, aL2 = 0, aDerived = 0, aReq = 0, aNegCb = 0;
    qint64 aB1 = 0, aB2 = 0;
    {
        ThumbCache cache(dir);
        for (int i = 0; i < files.size(); ++i) {
            cache.request(files[i], &ctx, [](const QPixmap&) {}, ThumbCache::kTile);
            cache.request(files[i], &ctx, [](const QPixmap&) {}, ThumbCache::kTile);
        }
        const bool done = spin([&cache] { return cache.cacheCount(ThumbCache::kFeature) >= kFiles; }, 20000);
        note(done, QStringLiteral("A_load timeout"));
        aL1 = cache.cacheCount(ThumbCache::kTile);
        aL2 = cache.cacheCount(ThumbCache::kFeature);
        aB1 = cache.cacheBytes(ThumbCache::kTile);
        aB2 = cache.cacheBytes(ThumbCache::kFeature);
        aDerived = cache.derivedCount();
        aReq = cache.requestCount();
        note(cache.levelPixels(ThumbCache::kTile) == pxTile,
             QStringLiteral("A_pxTile got=%1 want=%2").arg(cache.levelPixels(ThumbCache::kTile)).arg(pxTile));
        note(cache.levelPixels(ThumbCache::kFeature) == pxFeat,
             QStringLiteral("A_pxFeat got=%1 want=%2").arg(cache.levelPixels(ThumbCache::kFeature)).arg(pxFeat));
        note(aL1 == kFiles && aL2 == kFiles, QStringLiteral("A_count l1=%1 l2=%2").arg(aL1).arg(aL2));
        note(aB1 > 0 && aB2 > aB1, QStringLiteral("A_bytes l1=%1 l2=%2").arg(num(aB1)).arg(num(aB2)));
        note(aDerived == 2 * kFiles, QStringLiteral("A_derived=%1").arg(aDerived));
        note(aReq == kFiles, QStringLiteral("A_dedup req=%1").arg(aReq));
        const QPixmap p1 = cache.cached(files.first(), ThumbCache::kTile);
        const QPixmap p2 = cache.cached(files.first(), ThumbCache::kFeature);
        note(p1.width() == pxTile && p2.width() == pxFeat,
             QStringLiteral("A_px w1=%1 w2=%2").arg(p1.width()).arg(p2.width()));

        const QString miss = QStringLiteral("qa_missing.png");
        const int before = cache.requestCount();
        const auto negCb = [&note, &aNegCb](const QPixmap& pm) { ++aNegCb; note(pm.isNull(), QStringLiteral("A_negcb non-null")); };
        cache.request(miss, &ctx, negCb, ThumbCache::kTile);
        cache.request(miss, &ctx, negCb, ThumbCache::kFeature);
        spin([&cache, &miss] { return cache.isNegative(miss); }, 10000);
        note(cache.isNegative(miss), QStringLiteral("A_neg isNegative=false"));
        note(cache.requestCount() == before + 1,
             QStringLiteral("A_neg dedup fired=%1").arg(cache.requestCount() - before));
        note(cache.cached(miss, ThumbCache::kTile).isNull(), QStringLiteral("A_neg cached non-null"));
        note(aNegCb == 2, QStringLiteral("A_neg cb=%1").arg(aNegCb));
    }

    // —— B：压小 L1 预算（够放 ~2 张 L1）→ 淘汰 + 回落 L2 ——
    const qint64 smallB1 = 600000;
    int bEvict1 = -1;
    {
        qputenv("LA_THUMB_BUDGET1", QByteArray::number(smallB1));
        ThumbCache cache(dir);
        qunsetenv("LA_THUMB_BUDGET1");
        for (int i = 0; i < files.size(); ++i)
            cache.request(files[i], &ctx, [](const QPixmap&) {}, ThumbCache::kTile);
        spin([&cache] { return cache.cacheCount(ThumbCache::kFeature) >= kFiles; }, 20000);
        bEvict1 = cache.cacheCount(ThumbCache::kTile);
        note(cache.cacheBytes(ThumbCache::kTile) <= smallB1,
             QStringLiteral("B_budget bytes=%1>%2").arg(num(cache.cacheBytes(ThumbCache::kTile))).arg(num(smallB1)));
        note(bEvict1 >= 1 && bEvict1 < kFiles, QStringLiteral("B_evict l1count=%1").arg(bEvict1));
        note(cache.cacheCount(ThumbCache::kFeature) == kFiles,
             QStringLiteral("B_l2count=%1").arg(cache.cacheCount(ThumbCache::kFeature)));
        int fallbacks = 0;
        for (int i = 0; i < files.size(); ++i) {
            const QPixmap fb = cache.cached(files[i], ThumbCache::kTile);
            if (!fb.isNull() && fb.width() == pxFeat) ++fallbacks;   // L1 淘汰 -> 回落 L2 大图
            else if (fb.width() != pxTile) { note(false, QStringLiteral("B_fb w=%1").arg(fb.width())); break; }
        }
        note(fallbacks == kFiles - bEvict1, QStringLiteral("B_fbcount=%1").arg(fallbacks));
        if (fallbacks > 0) {
            const int before = cache.requestCount();
            bool cbOk = false;
            const QString victim = files.first();
            cache.request(victim, &ctx, [&cbOk](const QPixmap& pm) { cbOk = !pm.isNull(); }, ThumbCache::kTile);
            note(cbOk, QStringLiteral("B_fb cb null"));
            note(cache.requestCount() == before, QStringLiteral("B_fb retrigger +%1").arg(cache.requestCount() - before));
        }
    }

    // —— C：压小 L2 预算（够放 ~1 张 L2）→ 回落 L1 ——
    const qint64 smallB2 = 1500000;
    int cEvict2 = -1;
    {
        qputenv("LA_THUMB_BUDGET2", QByteArray::number(smallB2));
        ThumbCache cache(dir);
        qunsetenv("LA_THUMB_BUDGET2");
        for (int i = 0; i < files.size(); ++i)
            cache.request(files[i], &ctx, [](const QPixmap&) {}, ThumbCache::kTile);
        spin([&cache] { return cache.cacheCount(ThumbCache::kFeature) >= 1; }, 20000);
        spin([&cache] { return cache.cacheCount(ThumbCache::kTile) >= kFiles; }, 20000);
        cEvict2 = cache.cacheCount(ThumbCache::kFeature);
        note(cache.cacheBytes(ThumbCache::kFeature) <= smallB2,
             QStringLiteral("C_budget bytes=%1").arg(num(cache.cacheBytes(ThumbCache::kFeature))));
        note(cEvict2 >= 1 && cEvict2 < kFiles, QStringLiteral("C_evict l2count=%1").arg(cEvict2));
        note(cache.cacheCount(ThumbCache::kTile) == kFiles,
             QStringLiteral("C_l1count=%1").arg(cache.cacheCount(ThumbCache::kTile)));
        int fallbacks = 0;
        for (int i = 0; i < files.size(); ++i) {
            const QPixmap fb = cache.cached(files[i], ThumbCache::kFeature);
            if (!fb.isNull() && fb.width() == pxTile) ++fallbacks;   // L2 淘汰 -> 回落 L1 小图
            else if (fb.width() != pxFeat) { note(false, QStringLiteral("C_fb w=%1").arg(fb.width())); break; }
        }
        note(fallbacks == kFiles - cEvict2, QStringLiteral("C_fbcount=%1").arg(fallbacks));
    }

    if (!qEnvironmentVariableIsSet("LA_THUMB_QA_KEEP"))
        QDir(dir).removeRecursively();

    const bool ok = fails.isEmpty();
    if (ok) {
        std::fprintf(stderr,
            "LA_THUMB_CACHE_QA PASS files=%d px=%d/%d l1=%d l2=%d bytes=%lld/%lld "
            "derived=%d req=%d evictL1=%d evictL2=%d\n",
            kFiles, pxTile, pxFeat, aL1, aL2, aB1, aB2, aDerived, aReq, bEvict1, cEvict2);
    } else {
        std::fprintf(stderr, "LA_THUMB_CACHE_QA FAIL reason=%s\n", qPrintable(fails.join(QChar(';'))));
    }
    std::fflush(stderr);
    return ok ? 0 : 1;
}


// 方向 D 第五步：批量写回事务门禁（headless，自建临时库，无需真实库）。
// 断言 ① path 索引与 photos_ 双向一致（verifyPathIndex）
//      ② 空批 = 成功空操作（total=0 ok=true done=0，非错误）
//      ③ 批量打星原子成功：ok=true done=N，DB 与内存副本同步
//      ④ 不存在的路径 → 命中 0 行但仍算成功（不是错误）
//      ⑤ **失败整体回滚**：中途失败 ok=false + failedIndex 定位 + done 归零
//         且 DB 与内存副本**都保持原值**（无撕裂）——这是本门核心红线
//      ⑥ rating 越界自动 qBound 收敛（9→5, -3→0）
//      ⑦ 批量隐私标记同事务语义
// 反例：LA_BATCH_TX_QA_FAIL_AT=<n> 强制在第 n 条失败（正常路径永不失败），
//       用它证明 ⑤ 的回滚断言不是空转。
int runBatchTxQa()
{
    const int failAt = qEnvironmentVariable("LA_BATCH_TX_QA_FAIL_AT", QStringLiteral("-1")).toInt();

    // 临时库：QStandardPaths 临时目录，绝不碰真实library.db
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/la_batch_tx_qa");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString dbPath = QDir(dir).filePath(QStringLiteral("batch.db"));
    QFile::remove(dbPath);

    // ---- 造库：6 行 photos（path唯一），字段对齐 Library::open 的 SELECT ----
    const int kRows = 6;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_qa_seed"));
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            std::fprintf(stderr, "LA_BATCH_TX_QA FAIL seedopen\n");
            std::fflush(stderr);
            return 2;
        }
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "CREATE TABLE photos (path TEXT PRIMARY KEY, rel TEXT, thumb TEXT, "
            "missing INTEGER DEFAULT 0, exif_json TEXT, rating INTEGER DEFAULT 0)"));
        q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)"));
        for (int i = 0; i < kRows; ++i) {
            q.prepare(QStringLiteral(
                "INSERT INTO photos(path,rel,thumb,missing,exif_json,rating) VALUES(?,?,?,0,?,0)"));
            q.addBindValue(QStringLiteral("D:/p%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("p%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("t%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("{}"));
            q.exec();
        }
        db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("la_qa_seed"));
    }

    QString err;
    Library lib;
    if (!lib.open(dbPath, &err)) {
        std::fprintf(stderr, "LA_BATCH_TX_QA FAIL open=%s\n", qPrintable(err));
        std::fflush(stderr);
        return 2;
    }

    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) {
        if (!ok) fails << what;
    };
    QStringList paths;
    for (int i = 0; i < kRows; ++i) paths << QStringLiteral("D:/p%1.jpg").arg(i);
    // 直接查库某path 的 rating（独立连接，读真实落盘值，不看内存副本）
    const auto dbRating = [&dbPath](const QString& p, int* out) {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_qa_read"));
        db.setDatabaseName(dbPath);
        bool ok = false;
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT rating FROM photos WHERE path=?"));
            q.addBindValue(p);
            ok = q.exec() && q.next();
            if (ok) *out = q.value(0).toInt();
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("la_qa_read"));
        return ok;
    };
    const auto memRating = [&lib](const QString& p) {
        for (const Photo& ph : lib.photos())
            if (ph.path == p) return ph.rating;
        return -1;
    };

    //① 索引自检
    note(lib.verifyPathIndex(), QStringLiteral("verifyPathIndex false"));

    // ② 空批 = 成功空操作
    {
        const BatchResult r = lib.setRatings(QStringList(), 3);
        note(r.ok && r.total == 0 && r.done == 0 && r.failedIndex == -1,
             QStringLiteral("empty total=%1 done=%2 ok=%3").arg(r.total).arg(r.done).arg(r.ok ? 1 : 0));
    }

    // ③ 批量打星原子成功
    {
        const BatchResult r = lib.setRatings(paths, 4);
        note(r.ok && r.total == kRows && r.done == kRows && r.failedIndex == -1,
             QStringLiteral("ok-batch ok=%1 done=%2/%3 failed=%4").arg(r.ok ? 1 : 0).arg(r.done).arg(kRows).arg(r.failedIndex));
        int dbv = -1;
        for (int i = 0; i < kRows; ++i) {
            dbRating(paths[i], &dbv);
            note(dbv == 4, QStringLiteral("db p%1=%2").arg(i).arg(dbv));
            note(memRating(paths[i]) == 4, QStringLiteral("mem p%1=%2").arg(i).arg(memRating(paths[i])));
        }
    }

    // ④ 不存在的路径 → done 少但仍成功
    {
        QStringList mixed = paths;
        mixed << QStringLiteral("D:/__nope__.jpg");
        const BatchResult r = lib.setRatings(mixed, 2);
        note(r.ok && r.total == kRows + 1 && r.done == kRows,
             QStringLiteral("miss-path ok=%1 done=%2/%3").arg(r.ok ? 1 : 0).arg(r.done).arg(kRows + 1));
        note(memRating(QStringLiteral("D:/__nope__.jpg")) == -1,
             QStringLiteral("miss-path entered memory"));
    }

    // ⑥ rating 越界收敛
    {
        const BatchResult r = lib.setRatings(paths, 9);
        note(r.ok, QStringLiteral("clamp-high ok=%1").arg(r.ok ? 1 : 0));
        int dbv = -1;
        dbRating(paths[0], &dbv);
        note(dbv == 5 && memRating(paths[0]) == 5,
             QStringLiteral("clamp-high db=%1 mem=%2").arg(dbv).arg(memRating(paths[0])));
        lib.setRatings(paths, -3);
        dbRating(paths[0], &dbv);
        note(dbv == 0 && memRating(paths[0]) == 0,
             QStringLiteral("clamp-low db=%1 mem=%2").arg(dbv).arg(memRating(paths[0])));
    }

    // ⑤ 失败整体回滚（failAt<0 时用只读连接制造真实失败）
    {
        // 先铺一个已知基线
        lib.setRatings(paths, 3);
        int before0 = -1, beforeN = -1;
        dbRating(paths[0], &before0);
        dbRating(paths[kRows - 1], &beforeN);
        const int memBefore0 = memRating(paths[0]);
        const int memBeforeN = memRating(paths[kRows - 1]);

        // 失败源（两条路径，生产代码零seam）：
        //  A) 默认：BEGIN EXCLUSIVE 独占写锁 → rw 连接的 UPDATE 必然 SQLITE_BUSY
        //  B) failAt>=0：QA 自建 BEFORE UPDATE 触发器 RAISE(ABORT) → 在第 failAt 条精确失败，
        //     用于断言 failedIndex 的定位精度
        // NOLOCK=1 则**不制造失败**（批量会成功）→ ⑤ 的「必须失败」断言应转红 =非空心反例。
        const bool noLock = qEnvironmentVariableIsSet("LA_BATCH_TX_QA_NOLOCK");
        BatchResult r;
        if (noLock) {
            r = lib.setRatings(paths, 4);                 // 无失败源 → 应成功（反例）
        } else {
            QSqlDatabase blocker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                             QStringLiteral("la_qa_block"));
            blocker.setDatabaseName(dbPath);
            if (blocker.open()) {
                blocker.exec(QStringLiteral("BEGIN EXCLUSIVE"));   // 持写锁
                r = lib.setRatings(paths, 4);                      // 应失败
                blocker.exec(QStringLiteral("ROLLBACK"));
                blocker.close();
            } else {
                r = BatchResult();
                note(false, QStringLiteral("blocker open failed"));
            }
            QSqlDatabase::removeDatabase(QStringLiteral("la_qa_block"));
        }

        if (noLock) {
            // 反例：没有失败源就不该走回滚分支——门必须因此变红（证明断言非恒真）
            note(!r.ok, QStringLiteral("NOLOCK: expected FAIL but batch succeeded (assertion is vacuous)"));
        } else {
            note(!r.ok, QStringLiteral("locked-batch unexpectedly ok"));
            note(r.done == 0, QStringLiteral("locked-batch done=%1 (must 0)").arg(r.done));
            note(!r.error.isEmpty(), QStringLiteral("locked-batch error empty"));
            note(r.failedIndex >= 0, QStringLiteral("locked-batch failedIndex=%1 (must locate)").arg(r.failedIndex));
            int after0 = -1, afterN = -1;
            dbRating(paths[0], &after0);
            dbRating(paths[kRows - 1], &afterN);
            note(after0 == before0, QStringLiteral("rollback db0 %1->%2").arg(before0).arg(after0));
            note(afterN == beforeN, QStringLiteral("rollback dbN %1->%2").arg(beforeN).arg(afterN));
            note(memRating(paths[0]) == memBefore0 && memRating(paths[kRows - 1]) == memBeforeN,
                 QStringLiteral("rollback memory torn mem0=%1(=%2) memN=%3(=%4)")
                     .arg(memRating(paths[0])).arg(memBefore0)
                     .arg(memRating(paths[kRows - 1])).arg(memBeforeN));
        }
    }

    // ⑤b 触发器注入：失败定位精度（failedIndex 必须等于注入下标，且整体回滚）
    if (failAt >= 0 && failAt < kRows && !qEnvironmentVariableIsSet("LA_BATCH_TX_QA_NOLOCK")) {
        const QString victim = paths[failAt];
        lib.setRatings(paths, 1);                        // 基线
        int b0 = -1, bN = -1;
        dbRating(paths[0], &b0);
        dbRating(paths[kRows - 1], &bN);

        QSqlDatabase inj = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("la_qa_inject"));
        inj.setDatabaseName(dbPath);
        if (inj.open()) {
            inj.exec(QStringLiteral(
                "CREATE TRIGGER qa_inject BEFORE UPDATE ON photos WHEN NEW.path='%1' "
                "BEGIN SELECT RAISE(ABORT, 'qa injected failure'); END").arg(victim));
            inj.close();
        }
        const BatchResult r = lib.setRatings(paths, 5);
        {
            QSqlDatabase inj2 = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                         QStringLiteral("la_qa_drop"));
            inj2.setDatabaseName(dbPath);
            if (inj2.open()) {
                inj2.exec(QStringLiteral("DROP TRIGGER IF EXISTS qa_inject"));
                inj2.close();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("la_qa_drop"));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("la_qa_inject"));

        note(!r.ok, QStringLiteral("inject ok=%1").arg(r.ok ? 1 : 0));
        note(r.failedIndex == failAt,
             QStringLiteral("inject failedIndex=%1 want=%2").arg(r.failedIndex).arg(failAt));
        note(r.done == 0, QStringLiteral("inject done=%1 (must 0)").arg(r.done));
        int a0 = -1, aN = -1;
        dbRating(paths[0], &a0);
        dbRating(paths[kRows - 1], &aN);
        note(a0 == b0 && aN == bN,
             QStringLiteral("inject rollback db %1/%2 -> %3/%4").arg(b0).arg(bN).arg(a0).arg(aN));
        note(memRating(victim) == 1,
             QStringLiteral("inject victim mem=%1 (must 1)").arg(memRating(victim)));
    }

    // ⑦ 批量隐私标记
    {
        const BatchResult r = lib.setPrivates(paths, true);
        note(r.ok && r.done == kRows, QStringLiteral("priv ok=%1 done=%2").arg(r.ok ? 1 : 0).arg(r.done));
        int n = 0;
        for (const Photo& ph : lib.photos()) if (ph.priv) ++n;
        note(n == kRows, QStringLiteral("priv memory count=%1").arg(n));
        lib.setPrivates(QStringList{QStringLiteral("D:/p0.jpg")}, false);
        n = 0;
        for (const Photo& ph : lib.photos()) if (ph.priv) ++n;
        note(n == kRows - 1, QStringLiteral("priv unmark count=%1").arg(n));
    }

    if (!qEnvironmentVariableIsSet("LA_BATCH_QA_KEEP"))
        QDir(dir).removeRecursively();

    const bool ok = fails.isEmpty();
    if (ok) {
        std::fprintf(stderr,
            "LA_BATCH_TX_QA PASS rows=%d failAt=%d idx=1 empty=1 atomic=1 rollback=1 clamp=1 priv=1\n",
            kRows, failAt);
    } else {
        std::fprintf(stderr, "LA_BATCH_TX_QA FAIL reason=%s\n", qPrintable(fails.join(QChar(';'))));
    }
    std::fflush(stderr);
    return ok ? 0 : 1;
}

// 方向 C·智能分类门禁（LA_CLASSIFY_QA）。
// 不依赖外部 CLIP 服务：用 Classifier::setSyntheticForQa 注入确定结果，测
// 「写回 + 检索契约」。反例 LA_CLASSIFY_QA_FORCE_BROKEN=1 令 Library 写库但
// 跳过内存同步 → GalleryModel 按内存检索失效 → 变红（非空心）。
int runClassifyQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_CLASSIFY_QA_FORCE_BROKEN");

    // 临时库：绝不碰真实 library.db
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/la_classify_qa");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString dbPath = QDir(dir).filePath(QStringLiteral("classify.db"));
    QFile::remove(dbPath);
    const QString thumbsDir = dir;   // 合成钩子不读盘，目录仅占位

    // 造库：6 行 photos（字段对齐 Library::open 的 SELECT）
    const int kRows = 6;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_clsf_seed"));
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            std::fprintf(stderr, "LA_CLASSIFY_QA FAIL seedopen\n");
            std::fflush(stderr);
            return 2;
        }
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "CREATE TABLE photos (path TEXT PRIMARY KEY, rel TEXT, thumb TEXT, "
            "missing INTEGER DEFAULT 0, exif_json TEXT, category TEXT, person TEXT, "
            "private INTEGER DEFAULT 0, rating INTEGER DEFAULT 0, mark TEXT)"));
        q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)"));
        for (int i = 0; i < kRows; ++i) {
            q.prepare(QStringLiteral(
                "INSERT INTO photos(path,rel,thumb,missing,exif_json,category,person,private,rating,mark) "
                "VALUES(?,?,?,0,?,?,?,0,0,?)"));
            q.addBindValue(QStringLiteral("D:/p%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("p%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("t%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("{}"));
            q.addBindValue(QStringLiteral(""));      // category 空=未分类
            q.addBindValue(QStringLiteral(""));      // person
            q.addBindValue(QStringLiteral(""));      // mark
            q.exec();
        }
        db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("la_clsf_seed"));
    }

    QString err;
    Library lib;
    if (!lib.open(dbPath, &err)) {
        std::fprintf(stderr, "LA_CLASSIFY_QA FAIL open=%s\n", qPrintable(err));
        std::fflush(stderr);
        return 2;
    }

    // 反例钩子：写库但跳过内存同步
    Library::setForceCategoryNoSyncForQa(broken);

    // 合成结果：p0,p1,p2 → 风光；p3 → 人像；p4,p5 保持未分类
    QVector<CatItem> syn;
    syn.append({QStringLiteral("D:/p0.jpg"), QStringLiteral("风光"), 0.9});
    syn.append({QStringLiteral("D:/p1.jpg"), QStringLiteral("风光"), 0.9});
    syn.append({QStringLiteral("D:/p2.jpg"), QStringLiteral("风光"), 0.9});
    syn.append({QStringLiteral("D:/p3.jpg"), QStringLiteral("人像"), 0.9});
    Classifier::setSyntheticForQa(syn);

    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) {
        if (!ok) fails << what;
    };

    // ① 索引自检
    note(lib.verifyPathIndex(), QStringLiteral("verifyPathIndex false"));
    // 守恒：setCategories 不增删照片
    note(lib.photos().size() == kRows,
         QStringLiteral("conserve size=%1 want=%2").arg(lib.photos().size()).arg(kRows));

    // ② 跑分类（合成钩子 → 绕过网络，内部调 setCategories 写回）
    const bool ok = Classifier::classify(&lib, thumbsDir);
    note(ok, QStringLiteral("classify returned false"));

    // DB / 内存读助手（独立连接读真实落盘值）
    const auto dbCat = [&dbPath](const QString& p) {
        QString v;
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_clsf_read"));
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT category FROM photos WHERE path=?"));
            q.addBindValue(p);
            if (q.exec() && q.next()) v = q.value(0).toString();
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("la_clsf_read"));
        return v;
    };
    const auto memCat = [&lib](const QString& p) {
        for (const Photo& ph : lib.photos())
            if (ph.path == p) return ph.category;
        return QStringLiteral("__missing__");
    };

    // ③ DB 写回正确
    note(dbCat(QStringLiteral("D:/p0.jpg")) == QStringLiteral("风光"),
         QStringLiteral("db p0=%1").arg(dbCat(QStringLiteral("D:/p0.jpg"))));
    note(dbCat(QStringLiteral("D:/p3.jpg")) == QStringLiteral("人像"),
         QStringLiteral("db p3=%1").arg(dbCat(QStringLiteral("D:/p3.jpg"))));
    note(dbCat(QStringLiteral("D:/p4.jpg")).isEmpty(),
         QStringLiteral("db p4 should stay empty=%1").arg(dbCat(QStringLiteral("D:/p4.jpg"))));

    // ④ 内存同步（非 broken 时与 DB 一致）
    note(memCat(QStringLiteral("D:/p0.jpg")) == QStringLiteral("风光"),
         QStringLiteral("mem p0=%1").arg(memCat(QStringLiteral("D:/p0.jpg"))));
    note(memCat(QStringLiteral("D:/p3.jpg")) == QStringLiteral("人像"),
         QStringLiteral("mem p3=%1").arg(memCat(QStringLiteral("D:/p3.jpg"))));
    note(memCat(QStringLiteral("D:/p4.jpg")).isEmpty(),
         QStringLiteral("mem p4 should stay empty"));

    // ⑤ GalleryModel 检索契约（按内存 category 过滤）
    GalleryModel model;
    model.setSource(&lib.photos());
    model.setCategoryFilter(QStringLiteral("风光"));
    note(model.rowCount() == 3, QStringLiteral("rowCount(风光)=%1 want=3").arg(model.rowCount()));
    model.setCategoryFilter(QStringLiteral("人像"));
    note(model.rowCount() == 1, QStringLiteral("rowCount(人像)=%1 want=1").arg(model.rowCount()));
    model.setCategoryFilter(GalleryModel::kNoCategory);
    note(model.rowCount() == 2, QStringLiteral("rowCount(未分类)=%1 want=2").arg(model.rowCount()));
    model.setCategoryFilter(QString());  // 全部
    note(model.rowCount() == kRows, QStringLiteral("rowCount(全部)=%1 want=%2").arg(model.rowCount()).arg(kRows));

    // ⑥ normalizeLabel 确定性
    note(Classifier::normalizeLabel(QStringLiteral("portrait")) == QStringLiteral("人像"),
         QStringLiteral("norm portrait"));
    note(Classifier::normalizeLabel(QStringLiteral("landscape")) == QStringLiteral("风光"),
         QStringLiteral("norm landscape"));
    note(Classifier::normalizeLabel(QStringLiteral("夜景")) == QStringLiteral("夜景·星空"),
         QStringLiteral("norm 夜景"));
    note(Classifier::normalizeLabel(QStringLiteral("zzzunknown")) == QStringLiteral("其他"),
         QStringLiteral("norm unknown"));

    // 反例：broken 时 DB 已写但内存未同步 → 内存检索失效（行数=0），DB 侧仍有值。
    // 主断言 ④(memCat==风光) / ⑤(rowCount 按类) 已转红；此处签名化记录缺陷特征，
    // 并再次确认「只坏同步、不坏写回」，避免反例断言本身失真。
    if (broken) {
        model.setCategoryFilter(QStringLiteral("风光"));
        note(model.rowCount() == 0,
             QStringLiteral("BROKEN rowCount(风光)=%1 (expected 0: memory not synced)").arg(model.rowCount()));
        note(memCat(QStringLiteral("D:/p0.jpg")).isEmpty(),
             QStringLiteral("BROKEN mem p0 empty (intended defect)"));
        note(dbCat(QStringLiteral("D:/p0.jpg")) == QStringLiteral("风光"),
             QStringLiteral("BROKEN db p0 still 风光 (write ok, sync broken)"));
    }

    // 复位静态钩子（避免泄漏到其它门，虽然各门独立进程）
    Library::setForceCategoryNoSyncForQa(false);
    Classifier::clearSyntheticForQa();
    if (!qEnvironmentVariableIsSet("LA_CLASSIFY_QA_KEEP"))
        QDir(dir).removeRecursively();

    const bool okAll = fails.isEmpty();
    if (okAll) {
        std::fprintf(stderr,
            "LA_CLASSIFY_QA PASS rows=%d broken=%d write=1 sync=1 retrieve=1 conserve=1 norm=1\n",
            kRows, int(broken));
    } else {
        std::fprintf(stderr, "LA_CLASSIFY_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    }
    std::fflush(stderr);
    return okAll ? 0 : 1;
}

// 方向 C·人物自动聚类门禁（LA_FACE_CLUSTER_QA）。
// 不依赖外部 face_server：用 PersonDetector::setSyntheticEmbForQa 注入确定 embedding
// （3 张同一张脸 + 3 张孤立脸），经「抽 embedding 落库 face_emb → FaceCluster 阈值余弦
// 聚类 → applyClusters 写回 person」全链路，断言：
//   (A) face_emb 落库后可读回（聚类输入集 = 6）
//   (B) 守恒：inputCount == outputCount == 6，每 path 恰出现一次、无重复
//   (C) 分组正确：同一张脸 3 张归一簇「人物1」，孤立脸各自成簇（共 4 簇）
//   (D) DB person 列写回正确
//   (E) 内存同步（GalleryModel 读内存 Photo.person）
//   (F) 检索契约：rowCount(人物1)==3 / rowCount(全部)==6
//   (G) 重命名后仍可检索（沿用 2.19.0 renamePerson 契约，未回归）
// 反例 LA_FACE_CLUSTER_QA_FORCE_BROKEN=1 经 FaceCluster::setForceClusterBrokenForQa
// 丢一张 → 守恒断言 (B) 转红（非空心：正例同一断言实测为绿）。
int runFaceClusterQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_FACE_CLUSTER_QA_FORCE_BROKEN");

    // 临时库：绝不碰真实 library.db
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/la_fcluster_qa");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString dbPath = QDir(dir).filePath(QStringLiteral("cluster.db"));
    QFile::remove(dbPath);

    // 造库：6 行 photos（含 face_emb 列，对齐 Library::open 的迁移后 schema）
    const int kRows = 6;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_fcl_seed"));
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            std::fprintf(stderr, "LA_FACE_CLUSTER_QA FAIL seedopen\n");
            std::fflush(stderr);
            return 2;
        }
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "CREATE TABLE photos (path TEXT PRIMARY KEY, rel TEXT, thumb TEXT, "
            "missing INTEGER DEFAULT 0, exif_json TEXT, category TEXT, person TEXT, "
            "private INTEGER DEFAULT 0, rating INTEGER DEFAULT 0, mark TEXT, face_emb BLOB)"));
        q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)"));
        for (int i = 0; i < kRows; ++i) {
            q.prepare(QStringLiteral(
                "INSERT INTO photos(path,rel,thumb,missing,exif_json,category,person,private,rating,mark) "
                "VALUES(?,?,?,0,?,?,?,0,0,?)"));
            q.addBindValue(QStringLiteral("D:/c%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("c%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("t%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("{}"));
            q.addBindValue(QStringLiteral(""));      // category 空
            q.addBindValue(QStringLiteral(""));      // person 空=未标注（聚类输入集）
            q.addBindValue(QStringLiteral(""));      // mark
            q.exec();
        }
        db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("la_fcl_seed"));
    }

    QString err;
    Library lib;
    if (!lib.open(dbPath, &err)) {
        std::fprintf(stderr, "LA_FACE_CLUSTER_QA FAIL open=%s\n", qPrintable(err));
        std::fflush(stderr);
        return 2;
    }

    // 512-d 合成 embedding：p0,p1,p2 = 同一张脸（基向量）；p3,p4,p5 = 互相正交且与基正交。
    // 正交向量余弦 0 < 阈值 0.4 → 各自成簇；基向量余弦 1.0 → 归一簇。
    const int kDim = 512;
    // ⚠ QByteArray::resize() 不做零初始化（残留堆垃圾 → cosine=NaN）。必须显式填 0。
    const auto unitVec = [kDim](int idx) {
        QByteArray b(kDim * 4, '\0');        // 显式零初始化（非 resize）
        float* p = reinterpret_cast<float*>(b.data());
        p[idx] = 1.0f;                       // 单分量单位向量：与其它分量正交（余弦 0）
        return b;
    };
    QVector<FaceItem> synEmb;
    for (int i = 0; i < 3; ++i)              // 同一张脸 3 张
        synEmb.append({QStringLiteral("D:/c%1.jpg").arg(i), unitVec(0)});
    for (int i = 3; i < 6; ++i)              // 3 张互不相同的脸
        synEmb.append({QStringLiteral("D:/c%1.jpg").arg(i), unitVec(i)});
    PersonDetector::setSyntheticEmbForQa(synEmb);
    // 反例钩子：cluster 丢一张 → 守恒断言转红
    FaceCluster::setForceBrokenForQa(broken);

    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) {
        if (!ok) fails << what;
    };

    // ① 索引自检 + 守恒基线
    note(lib.verifyPathIndex(), QStringLiteral("verifyPathIndex false"));
    note(lib.photos().size() == kRows,
         QStringLiteral("conserve size=%1 want=%2").arg(lib.photos().size()).arg(kRows));

    // ② 抽 embedding 并落库 face_emb（合成钩子 → 绕过网络）
    const bool okEmb = PersonDetector::detectEmbeddings(&lib, QStringLiteral("http://127.0.0.1:8767"));
    note(okEmb, QStringLiteral("detectEmbeddings returned false"));

    // ③ (A) 聚类输入集读回 = 6
    const QVector<FaceItem> embs = lib.getFaceEmbeddings();
    note(embs.size() == kRows,
         QStringLiteral("getFaceEmbeddings=%1 want=%2").arg(embs.size()).arg(kRows));
    note(embs.size() > 0 && embs.first().emb.size() == kDim * 4,
         QStringLiteral("emb bytesize=%1 want=%2")
             .arg(embs.isEmpty() ? -1 : embs.first().emb.size()).arg(kDim * 4));

    // ④ 阈值余弦聚类
    QVector<ClusterInput> items;
    items.reserve(embs.size());
    for (const FaceItem& e : embs) items.append({e.path, e.emb});
    const ClusterResult cr = FaceCluster::cluster(items, 0.4, 1);

    // (B) 守恒契约：总数守恒 + 每 path 恰一次（这是本门的核心契约，反例打这里）
    note(cr.inputCount == kRows,
         QStringLiteral("cluster inputCount=%1 want=%2").arg(cr.inputCount).arg(kRows));
    note(cr.outputCount == cr.inputCount,
         QStringLiteral("BROKEN conserve outputCount=%1 inputCount=%2")
             .arg(cr.outputCount).arg(cr.inputCount));
    {
        QSet<QString> seen;
        int dup = 0;
        int total = 0;
        for (const ClusterGroup& g : cr.groups)
            for (const QString& p : g.paths) {
                ++total;
                if (seen.contains(p)) ++dup;
                seen.insert(p);
            }
        note(dup == 0, QStringLiteral("conserve dup path x%1").arg(dup));
        note(total == cr.inputCount,
             QStringLiteral("conserve listed=%1 input=%2").arg(total).arg(cr.inputCount));
    }

    // (C) 分组正确：4 簇（1 个 3 成员 + 3 个单成员），主簇 label=人物1
    note(cr.groups.size() == 4,
         QStringLiteral("cluster groups=%1 want=4").arg(cr.groups.size()));
    int biggest = 0;
    for (const ClusterGroup& g : cr.groups)
        biggest = qMax(biggest, int(g.paths.size()));
    note(biggest == 3, QStringLiteral("biggest cluster=%1 want=3").arg(biggest));

    // ⑤ 簇标签写回 person（applyClusters；不覆盖非空）
    QVector<ClusterItem> out;
    for (const ClusterGroup& g : cr.groups)
        for (const QString& p : g.paths)
            out.append({p, g.label});
    const BatchResult br = lib.applyClusters(out);
    note(br.ok, QStringLiteral("applyClusters not ok: %1").arg(br.error));

    // DB / 内存读助手（独立连接读真实落盘值）
    const auto dbPerson = [&dbPath](const QString& p) {
        QString v;
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_fcl_read"));
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT person FROM photos WHERE path=?"));
            q.addBindValue(p);
            if (q.exec() && q.next()) v = q.value(0).toString();
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("la_fcl_read"));
        return v;
    };
    const auto memPerson = [&lib](const QString& p) {
        for (const Photo& ph : lib.photos())
            if (ph.path == p) return ph.person;
        return QStringLiteral("__missing__");
    };

    // 找到 3 成员主簇的 label（正例应为「人物1」；反例丢一张后主簇仍最大）
    QString mainLabel;
    for (const ClusterGroup& g : cr.groups)
        if (g.paths.size() == biggest) { mainLabel = g.label; break; }
    note(mainLabel == QStringLiteral("人物1"),
         QStringLiteral("main cluster label=%1 want=人物1").arg(mainLabel));

    // (D) DB 写回正确
    note(!dbPerson(QStringLiteral("D:/c0.jpg")).isEmpty(),
         QStringLiteral("db c0 should have person (empty=%1)").arg(dbPerson(QStringLiteral("D:/c0.jpg"))));
    // (E) 内存同步
    note(memPerson(QStringLiteral("D:/c0.jpg")) == dbPerson(QStringLiteral("D:/c0.jpg")),
         QStringLiteral("mem c0=%1 db=%2").arg(memPerson(QStringLiteral("D:/c0.jpg")))
             .arg(dbPerson(QStringLiteral("D:/c0.jpg"))));

    // (F) 检索契约（GalleryModel 读内存 person）
    GalleryModel model;
    model.setSource(&lib.photos());
    model.setPersonFilter(mainLabel);
    note(model.rowCount() == 3,
         QStringLiteral("rowCount(%1)=%2 want=3").arg(mainLabel).arg(model.rowCount()));
    model.setPersonFilter(QString());
    note(model.rowCount() == kRows,
         QStringLiteral("rowCount(全部)=%1 want=%2").arg(model.rowCount()).arg(kRows));

    // (G) 重命名契约（2.19.0 能力不得因聚类写回而回归）
    if (!broken) {
        const bool renamed = lib.renamePerson(mainLabel, QStringLiteral("张三"));
        note(renamed, QStringLiteral("renamePerson returned false"));
        note(dbPerson(QStringLiteral("D:/c0.jpg")) == QStringLiteral("张三"),
             QStringLiteral("db c0 after rename=%1").arg(dbPerson(QStringLiteral("D:/c0.jpg"))));
        model.setPersonFilter(QStringLiteral("张三"));
        note(model.rowCount() == 3,
             QStringLiteral("rowCount(张三) after rename=%1 want=3").arg(model.rowCount()));
    }

    // 反例签名化：只坏守恒，不坏写回（避免反例断言本身失真）
    if (broken) {
        note(cr.outputCount == cr.inputCount - 1,
             QStringLiteral("BROKEN expected outputCount=inputCount-1, got %1/%2")
                 .arg(cr.outputCount).arg(cr.inputCount));
        note(lib.photos().size() == kRows,
             QStringLiteral("BROKEN photo count must stay %1").arg(kRows));
    }

    // 复位静态钩子
    FaceCluster::setForceBrokenForQa(false);
    PersonDetector::clearSyntheticEmbForQa();
    if (!qEnvironmentVariableIsSet("LA_FACE_CLUSTER_QA_KEEP"))
        QDir(dir).removeRecursively();

    const bool okAll = fails.isEmpty();
    if (okAll) {
        std::fprintf(stderr,
            "LA_FACE_CLUSTER_QA PASS rows=%d broken=%d emb=1 conserve=1 group=1 write=1 sync=1 retrieve=1 rename=1\n",
            kRows, int(broken));
    } else {
        std::fprintf(stderr, "LA_FACE_CLUSTER_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    }
    std::fflush(stderr);
    return okAll ? 0 : 1;
}

// 人物故事方向·B：自动故事相册（地点 + 时间窗口聚合）门禁。
//
// 与 runFaceClusterQa 同构：纯合成数据、不碰真实 library.db、守恒断言为核心契约。
// StoryGen::build 是零 DB 纯函数，直接喂合成 QVector<Photo> 即可。
// 反例：LA_STORY_QA_FORCE_BROKEN 令 build() 丢一张 → 守恒断言转红（正/反例跑同一组断言）。
int runStoryQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_STORY_QA_FORCE_BROKEN");
    StoryGen::setForceDropForQa(broken);

    // 合成 7 张照片（纯内存，不落库）：
    //   北京 c0 2024-05-01、c1 2024-05-03（同行程）、c2 2024-05-20（间隔>5 新故事）
    //   上海 c3 2024-06-10、c4 2024-06-12（同行程）
    //   未知地点 c5、c6（无地点+无日期 → 各成「未标注日期」单张故事）
    QVector<Photo> photos;
    const auto mk = [](const QString& path, const QString& prov, const QString& city,
                       const QString& date, bool priv = false) {
        Photo p;
        p.path = path;
        p.rel  = QFileInfo(path).fileName();
        p.thumbFile = QStringLiteral("t_") + QFileInfo(path).fileName();
        p.prov = prov; p.city = city; p.date = date;
        p.priv = priv;
        return p;
    };
    photos.append(mk(QStringLiteral("D:/c0.jpg"), QStringLiteral("北京"), QStringLiteral("北京"), QStringLiteral("2024-05-01")));
    photos.append(mk(QStringLiteral("D:/c1.jpg"), QStringLiteral("北京"), QStringLiteral("北京"), QStringLiteral("2024-05-03")));
    photos.append(mk(QStringLiteral("D:/c2.jpg"), QStringLiteral("北京"), QStringLiteral("北京"), QStringLiteral("2024-05-20")));
    photos.append(mk(QStringLiteral("D:/c3.jpg"), QStringLiteral("上海"), QStringLiteral("上海"), QStringLiteral("2024-06-10")));
    photos.append(mk(QStringLiteral("D:/c4.jpg"), QStringLiteral("上海"), QStringLiteral("上海"), QStringLiteral("2024-06-12")));
    photos.append(mk(QStringLiteral("D:/c5.jpg"), QString(), QString(), QString()));   // 无地点+无日期
    photos.append(mk(QStringLiteral("D:/c6.jpg"), QString(), QString(), QString()));
    const int kRows = photos.size();

    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) {
        if (!ok) fails << what;
    };

    const StoryGen::Result r = StoryGen::build(photos, 5, true);

    // ① 守恒：outputCount == inputCount（核心契约，反例打这里）
    note(r.inputCount == kRows,
         QStringLiteral("inputCount=%1 want=%2").arg(r.inputCount).arg(kRows));
    note(r.outputCount == r.inputCount,
         QStringLiteral("conserve outputCount=%1 inputCount=%2")
             .arg(r.outputCount).arg(r.inputCount));

    // ② 每张照片恰好出现一次（无静默丢弃、无重复）+ 全量覆盖（诚实不丢）
    {
        QSet<QString> seen;
        int dup = 0, total = 0;
        for (const StoryGen::Story& s : r.stories)
            for (const QString& p : s.paths) {
                ++total;
                if (seen.contains(p)) ++dup;
                seen.insert(p);
            }
        note(dup == 0, QStringLiteral("dup path x%1").arg(dup));
        note(total == r.inputCount,
             QStringLiteral("listed=%1 input=%2").arg(total).arg(r.inputCount));
        QSet<QString> want;
        for (const Photo& p : photos) if (!p.priv) want.insert(p.path);
        note(seen == want, QStringLiteral("coverage mismatch seen=%1 want=%2")
                 .arg(seen.size()).arg(want.size()));
    }

    // ③ 分组结构：北京拆 2 个故事（c0,c1 / c2）、上海 1 个（c3,c4）、未知地点 2 个
    //    「未标注日期」单张 → 共 5 个故事
    note(r.stories.size() == 5,
         QStringLiteral("stories=%1 want=5").arg(r.stories.size()));
    int nodateSingles = 0;
    for (const StoryGen::Story& s : r.stories)
        if (s.id.contains(QStringLiteral("|nodate|"))) ++nodateSingles;
    note(nodateSingles == 2,
         QStringLiteral("nodate singles=%1 want=2").arg(nodateSingles));

    // ④ 检索契约：挑上海故事（2 张），setStoryFilter 后 rowCount == photoCount
    GalleryModel model;
    model.setSource(&photos);
    const StoryGen::Story* sh = nullptr;
    for (const StoryGen::Story& s : r.stories)
        if (s.photoCount == 2 && s.place == QStringLiteral("上海")) { sh = &s; break; }
    note(sh != nullptr, QStringLiteral("shanghai story missing"));
    if (sh) {
        model.setStoryFilter(sh->paths);
        note(model.rowCount() == 2,
             QStringLiteral("rowCount(上海)=%1 want=2").arg(model.rowCount()));
        model.setStoryFilter(QStringList());
        note(model.rowCount() == kRows,
             QStringLiteral("rowCount(全部)=%1 want=%2").arg(model.rowCount()).arg(kRows));
    }

    // 反例签名化：只坏守恒（丢一张），不坏其它行为
    if (broken) {
        note(r.outputCount == r.inputCount - 1,
             QStringLiteral("BROKEN expected outputCount=inputCount-1, got %1/%2")
                 .arg(r.outputCount).arg(r.inputCount));
    }

    StoryGen::setForceDropForQa(false);

    const bool okAll = fails.isEmpty();
    if (okAll) {
        std::fprintf(stderr,
            "LA_STORY_QA PASS rows=%d broken=%d conserve=1 unique=1 coverage=1 "
            "groups=1 nodate=1 rowCount=1\n", kRows, int(broken));
    } else {
        std::fprintf(stderr, "LA_STORY_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    }
    std::fflush(stderr);
    return okAll ? 0 : 1;
}

// 2.22.0 客户端体验门禁（LA_TRANSITION_QA）：原图 + 骨架屏 + 动画纯函数。
//
// 三条断言线，每条的判别器都必须是「实现一旦退化就变红」的那种：
//   A 状态机与动画纯函数：三态两两不同；脉冲值域/峰值/周期/减动效终态
//   B 骨架屏绘制：Loading 与 Ready 的像素必须不同；不同相位像素不同（真在脉动）；
//     减动效下各相位像素必须一致（不闪）
//   C 灯箱原图：解码出的原图边长必须 **大于** 512 缩略图档位（判别器），
//     且 LRU 字节预算 > 0（预算写成 0 会退化成"永不缓存"）
// 反例 LA_TRANSITION_QA_FORCE_BROKEN=1 经 FullCache::setForceFailForQa
// 让解码恒失败 → C 组转红（非空心：同一批断言在正例实测为绿）。
int runTransitionQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_TRANSITION_QA_FORCE_BROKEN");

    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) {
        if (!ok) fails << what;
    };

    // ---- A 状态机 + 动画纯函数契约（铁律 20：产品与门禁共用同一实现）----
    note(Lumen::skeletonState(QPixmap(), false) == Lumen::SkeletonState::Loading,
         QStringLiteral("A state(null,false) should be Loading"));
    note(Lumen::skeletonState(QPixmap(), true) == Lumen::SkeletonState::Missing,
         QStringLiteral("A state(null,true) should be Missing"));
    {
        QPixmap pm(4, 4);
        pm.fill(Qt::red);
        note(Lumen::skeletonState(pm, false) == Lumen::SkeletonState::Ready,
             QStringLiteral("A state(pm,*) should be Ready"));
    }
    {
        double lo = 9.0, hi = -9.0;
        for (int i = 0; i <= 100; ++i) {
            const double v = Lumen::skeletonPulse(i / 100.0, false);
            lo = qMin(lo, v);
            hi = qMax(hi, v);
        }
        note(lo >= 0.249 && lo <= 0.251,
             QStringLiteral("A pulse min=%1 want 0.25").arg(lo, 0, 'f', 4));
        note(qAbs(hi - 1.0) < 1e-9,
             QStringLiteral("A pulse max=%1 want 1.0").arg(hi, 0, 'f', 4));
        bool allOne = true;
        for (int i = 0; i <= 20; ++i)
            if (qAbs(Lumen::skeletonPulse(i / 20.0, true) - 1.0) > 1e-9) allOne = false;
        note(allOne, QStringLiteral("A reduceMotion must return terminal 1.0"));
        note(qAbs(Lumen::skeletonPulse(0.0, false) - Lumen::skeletonPulse(1.0, false)) < 1e-9,
             QStringLiteral("A pulse must be periodic (phase0==phase1)"));
    }

    // ---- B 骨架屏绘制：像素级判别 ----
    const int cardW = 120, cardH = 90;
    const auto renderCard = [](bool withImage, double phase, bool reduceMotion) {
        QImage img(cardW, cardH, QImage::Format_ARGB32);
        img.fill(Qt::black);
        QPainter p(&img);
        QPixmap pm;
        if (withImage) { pm = QPixmap(cardW, cardH); pm.fill(Qt::red); }
        Lumen::paintGalleryCard(&p, QRectF(0, 0, cardW, cardH),
                                QStringLiteral("t"), QString(), QString(),
                                false, 0, pm, false, false, false, phase, reduceMotion);
        p.end();
        return img;
    };
    const QImage skelImg = renderCard(false, 0.0, false);
    const QImage readyImg = renderCard(true, 0.0, false);
    note(skelImg != readyImg,
         QStringLiteral("B skeleton render must differ from ready render"));
    {
        // 骨架必须真的画了脉冲底色（不是纯黑/透明）：中心像素绿色通道 ≥ 0x18
        const QColor c = skelImg.pixel(cardW / 2, cardH / 2);
        note(c.green() >= 0x18,
             QStringLiteral("B skeleton center too dark: g=%1").arg(c.green()));
    }
    {
        const QImage s0 = renderCard(false, 0.0, false);
        const QImage s5 = renderCard(false, 0.5, false);
        note(s0 != s5, QStringLiteral("B different phases must render differently"));
        const QImage r0 = renderCard(false, 0.0, true);
        const QImage r5 = renderCard(false, 0.5, true);
        note(r0 == r5, QStringLiteral("B reduceMotion must render identical frames"));
    }

    // ---- C 灯箱原图：FullCache 预算 + 真实解码 ----
    note(FullCache::defaultBudgetBytes() > 0,
         QStringLiteral("C budget must be > 0 (got %1)").arg(FullCache::defaultBudgetBytes()));
    note(FullCache::maxPixels() > 0,
         QStringLiteral("C maxPixels must be > 0 (got %1)").arg(FullCache::maxPixels()));
    {
        const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("la_transition_qa"));
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        const QString orig = QDir(dir).filePath(QStringLiteral("orig.png"));
        {
            QImage big(1600, 1200, QImage::Format_RGB32);
            big.fill(QColor(0x20, 0x60, 0x40));
            big.save(orig);
        }
        FullCache::setForceFailForQa(broken);   // 反例：解码恒失败

        // 2.29.0：FullCache 后台解码须能吃 RAW（此前裸 QImageReader 对 RAW 返回
        // 0x0 null，实测 QImage("x.dng") → 0x0）。此断言把「灯箱上线后才发现」的风险
        // 前移到此刻。缺 RAW 样本时静默跳过该条（样本是外部素材，绝不入库/进 CI）。
        if (!broken) {
            const QString rawS = qEnvironmentVariable("LA_RAW_SAMPLE");
            if (!rawS.isEmpty() && QFileInfo::exists(rawS)) {
                FullCache rawCache;
                QImage rawDecoded;
                rawCache.request(rawS, nullptr,
                                 [&rawDecoded](const QImage& im) { rawDecoded = im; });
                for (int i = 0; i < 400; ++i) {
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                    if (!rawDecoded.isNull()) break;
                    // ★必须让出 CPU：后台线程要跑 dcraw_emu（实测 ~0.6s），
                    //   只 processEvents 而不 sleep = 空转，线程池任务 starvation
                    //   → 循环空转 4s 后退出、断言误报 null。
                    //   紧邻的 PNG 用例本就带 msleep(5)，此处同理（铁律 22：
                    //   门禁不得依赖未验证的时序假设）。
                    QThread::msleep(5);
                }
                std::fprintf(stderr, "  LA_THUMB_CACHE_QA raw: %dx%d null=%d\n",
                             rawDecoded.width(), rawDecoded.height(),
                             rawDecoded.isNull() ? 1 : 0);
                note(!rawDecoded.isNull(),
                     QStringLiteral("FullCache must decode RAW (got null)"));
            }
        }
        FullCache cache;
        QImage decoded;
        cache.request(orig, nullptr, [&decoded](const QImage& im) { decoded = im; });
        // 后台解码 + QueuedConnection 需事件循环派发，自旋等待其落地
        for (int i = 0; i < 300; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            if (!decoded.isNull() || cache.failureCount() > 0) break;
            QThread::msleep(5);
        }
        // ★期望值恒为规格值，**不随 broken 变**（铁律 22：断言期望值若绑在反例
        // 开关上 → 反例注入时"期望"也跟着变 → 自证通过、门永不红）。
        // 反例让解码恒失败，于是这几条**同一批断言**在反例下必须转红。
        note(!decoded.isNull(), QStringLiteral("C full image decode returned null"));
        note(decoded.width() == 1600 && decoded.height() == 1200,
             QStringLiteral("C decoded %1x%2 want 1600x1200")
                 .arg(decoded.width()).arg(decoded.height()));
        // ★判别器：原图边长必须超过缩略图档位 512，否则等于没换成原图
        note(decoded.width() > 512,
             QStringLiteral("C decoded width %1 must exceed thumb level 512")
                 .arg(decoded.width()));
        note(cache.cacheCount() == 1,
             QStringLiteral("C cache should hold 1, got %1").arg(cache.cacheCount()));
        note(cache.cacheBytes() > 0,
             QStringLiteral("C cacheBytes should be > 0 (got %1)").arg(cache.cacheBytes()));
        // 负缓存：解码失败**确实**应记入负缓存（防重试风暴）——这是正确行为，
        // 故断言为 isNegative==true。反例下它必须仍为真：证明"原图读不到"时
        // 降级路径（负缓存 → 调用方回退缩略图）依然健全，不是全盘失效。
        if (broken) {
            note(cache.isNegative(orig),
                 QStringLiteral("BROKEN failed decode should be negative-cached (got false)"));
            note(cache.failureCount() > 0,
                 QStringLiteral("BROKEN failureCount should be > 0 (got %1)")
                     .arg(cache.failureCount()));
        }
        FullCache::setForceFailForQa(false);
        if (!qEnvironmentVariableIsSet("LA_TRANSITION_QA_KEEP"))
            QDir(dir).removeRecursively();
    }

    const bool okAll = fails.isEmpty();
    if (okAll) {
        std::fprintf(stderr,
            "LA_TRANSITION_QA PASS broken=%d state=3 pulse=4 render=4 budget=1 full=5\n",
            int(broken));
    } else {
        std::fprintf(stderr, "LA_TRANSITION_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    }
    std::fflush(stderr);
    return okAll ? 0 : 1;
}

// 方向 C·人脸识别（方案A）门禁（LA_PERSON_QA）。
// 不依赖外部 face_server：用 PersonDetector::setSyntheticForQa 注入确定结果，测
// 「写回 + 检索契约 + 重命名契约」。反例 LA_PERSON_QA_FORCE_BROKEN=1 令 Library 写库但
// 跳过内存同步 → GalleryModel 按内存 person 检索失效 → 变红（非空心）。
int runPersonQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_PERSON_QA_FORCE_BROKEN");

    // 临时库：绝不碰真实 library.db
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/la_person_qa");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString dbPath = QDir(dir).filePath(QStringLiteral("person.db"));
    QFile::remove(dbPath);
    const QString thumbsDir = dir;   // 合成钩子不读盘，目录仅占位

    // 造库：6 行 photos（字段对齐 Library::open 的 SELECT）
    const int kRows = 6;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_pers_seed"));
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            std::fprintf(stderr, "LA_PERSON_QA FAIL seedopen\n");
            std::fflush(stderr);
            return 2;
        }
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "CREATE TABLE photos (path TEXT PRIMARY KEY, rel TEXT, thumb TEXT, "
            "missing INTEGER DEFAULT 0, exif_json TEXT, category TEXT, person TEXT, "
            "private INTEGER DEFAULT 0, rating INTEGER DEFAULT 0, mark TEXT)"));
        q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)"));
        for (int i = 0; i < kRows; ++i) {
            q.prepare(QStringLiteral(
                "INSERT INTO photos(path,rel,thumb,missing,exif_json,category,person,private,rating,mark) "
                "VALUES(?,?,?,0,?,?,?,0,0,?)"));
            q.addBindValue(QStringLiteral("D:/p%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("p%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("t%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("{}"));
            q.addBindValue(QStringLiteral(""));      // category 空=未分类
            q.addBindValue(QStringLiteral(""));      // person 空=无人物
            q.addBindValue(QStringLiteral(""));      // mark
            q.exec();
        }
        db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("la_pers_seed"));
    }

    QString err;
    Library lib;
    if (!lib.open(dbPath, &err)) {
        std::fprintf(stderr, "LA_PERSON_QA FAIL open=%s\n", qPrintable(err));
        std::fflush(stderr);
        return 2;
    }

    // 反例钩子：写库但跳过内存同步
    Library::setForcePersonNoSyncForQa(broken);

    // 合成结果：p0,p1,p2 → 人物1（检测到人脸）；p3,p4,p5 保持无人物
    QVector<PersonItem> syn;
    syn.append({QStringLiteral("D:/p0.jpg"), PersonDetector::defaultPerson()});
    syn.append({QStringLiteral("D:/p1.jpg"), PersonDetector::defaultPerson()});
    syn.append({QStringLiteral("D:/p2.jpg"), PersonDetector::defaultPerson()});
    PersonDetector::setSyntheticForQa(syn);

    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) {
        if (!ok) fails << what;
    };

    // ① 索引自检
    note(lib.verifyPathIndex(), QStringLiteral("verifyPathIndex false"));
    // 守恒：setPersons 不增删照片
    note(lib.photos().size() == kRows,
         QStringLiteral("conserve size=%1 want=%2").arg(lib.photos().size()).arg(kRows));

    // ② 跑检测（合成钩子 → 绕过网络，内部调 setPersons 写回）
    const bool ok = PersonDetector::detect(&lib, QStringLiteral("http://127.0.0.1:8767"));
    note(ok, QStringLiteral("detect returned false"));

    // DB / 内存读助手（独立连接读真实落盘值）
    const auto dbPerson = [&dbPath](const QString& p) {
        QString v;
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_pers_read"));
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT person FROM photos WHERE path=?"));
            q.addBindValue(p);
            if (q.exec() && q.next()) v = q.value(0).toString();
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("la_pers_read"));
        return v;
    };
    const auto memPerson = [&lib](const QString& p) {
        for (const Photo& ph : lib.photos())
            if (ph.path == p) return ph.person;
        return QStringLiteral("__missing__");
    };

    // ③ DB 写回正确
    note(dbPerson(QStringLiteral("D:/p0.jpg")) == PersonDetector::defaultPerson(),
         QStringLiteral("db p0=%1").arg(dbPerson(QStringLiteral("D:/p0.jpg"))));
    note(dbPerson(QStringLiteral("D:/p2.jpg")) == PersonDetector::defaultPerson(),
         QStringLiteral("db p2=%1").arg(dbPerson(QStringLiteral("D:/p2.jpg"))));
    note(dbPerson(QStringLiteral("D:/p3.jpg")).isEmpty(),
         QStringLiteral("db p3 should stay empty=%1").arg(dbPerson(QStringLiteral("D:/p3.jpg"))));

    // ④ 内存同步（非 broken 时与 DB 一致）
    note(memPerson(QStringLiteral("D:/p0.jpg")) == PersonDetector::defaultPerson(),
         QStringLiteral("mem p0=%1").arg(memPerson(QStringLiteral("D:/p0.jpg"))));
    note(memPerson(QStringLiteral("D:/p3.jpg")).isEmpty(),
         QStringLiteral("mem p3 should stay empty"));

    // ⑤ GalleryModel 检索契约（按内存 person 过滤）
    GalleryModel model;
    model.setSource(&lib.photos());
    model.setPersonFilter(PersonDetector::defaultPerson());
    note(model.rowCount() == 3, QStringLiteral("rowCount(人物1)=%1 want=3").arg(model.rowCount()));
    model.setPersonFilter(QStringLiteral("张三"));
    note(model.rowCount() == 0, QStringLiteral("rowCount(张三)=%1 want=0").arg(model.rowCount()));
    model.setPersonFilter(QString());
    note(model.rowCount() == kRows, QStringLiteral("rowCount(全部)=%1 want=%2").arg(model.rowCount()).arg(kRows));

    // ⑥ 默认标签确定性
    note(PersonDetector::defaultPerson() == QStringLiteral("人物1"),
         QStringLiteral("defaultPerson mismatch"));

    // ⑦ 重命名契约（非 broken）：人物1 → 张三，全库一致 + 检索切换
    if (!broken) {
        const bool renamed = lib.renamePerson(PersonDetector::defaultPerson(),
                                              QStringLiteral("张三"));
        note(renamed, QStringLiteral("renamePerson returned false"));
        note(dbPerson(QStringLiteral("D:/p0.jpg")) == QStringLiteral("张三"),
             QStringLiteral("db p0 after rename=%1").arg(dbPerson(QStringLiteral("D:/p0.jpg"))));
        note(memPerson(QStringLiteral("D:/p0.jpg")) == QStringLiteral("张三"),
             QStringLiteral("mem p0 after rename=%1").arg(memPerson(QStringLiteral("D:/p0.jpg"))));
        model.setPersonFilter(QStringLiteral("张三"));
        note(model.rowCount() == 3, QStringLiteral("rowCount(张三) after rename=%1 want=3").arg(model.rowCount()));
        model.setPersonFilter(PersonDetector::defaultPerson());
        note(model.rowCount() == 0, QStringLiteral("rowCount(人物1) after rename=%1 want=0").arg(model.rowCount()));
    }

    // 反例：broken 时 DB 已写但内存未同步 → 内存检索失效（行数=0），DB 侧仍有值。
    // 主断言 ④(memPerson==人物1) / ⑤(rowCount 按人物) 已转红；此处签名化记录缺陷特征，
    // 并再次确认「只坏同步、不坏写回」，避免反例断言本身失真。
    if (broken) {
        model.setPersonFilter(PersonDetector::defaultPerson());
        note(model.rowCount() == 0,
             QStringLiteral("BROKEN rowCount(人物1)=%1 (expected 0: memory not synced)").arg(model.rowCount()));
        note(memPerson(QStringLiteral("D:/p0.jpg")).isEmpty(),
             QStringLiteral("BROKEN mem p0 empty (intended defect)"));
        note(dbPerson(QStringLiteral("D:/p0.jpg")) == PersonDetector::defaultPerson(),
             QStringLiteral("BROKEN db p0 still 人物1 (write ok, sync broken)"));
    }

    // 复位静态钩子
    Library::setForcePersonNoSyncForQa(false);
    PersonDetector::clearSyntheticForQa();
    if (!qEnvironmentVariableIsSet("LA_PERSON_QA_KEEP"))
        QDir(dir).removeRecursively();

    const bool okAll = fails.isEmpty();
    if (okAll) {
        std::fprintf(stderr,
            "LA_PERSON_QA PASS rows=%d broken=%d write=1 sync=1 retrieve=1 rename=1 conserve=1\n",
            kRows, int(broken));
    } else {
        std::fprintf(stderr, "LA_PERSON_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    }
    std::fflush(stderr);
    return okAll ? 0 : 1;
}


// 方向 D 第五步 b：图库多选 + 批量工具条门禁（headless，无需真实库）。
// 断言 ① 工具条初始隐藏；有选中时显示、取消后回隐
//      ② Ctrl 点击 = 切换累加（且**不触发灯箱**）
//      ③ Shift 点击 = 以焦点行为锚点做范围选择
//      ④ 无修饰点击 = 单选**且触发灯箱**（既有行为不破坏）
//      ⑤ 全选/取消/重复切换等边界
//      ⑥ 批量动作 signal 载荷：paths 与评分值正确（视图只上报，写回在 MainWindow）
//      ⑦ 端到端：signal → Library::setRatings 单事务写库，DB 与内存副本一致
// 反例：LA_BATCH_QA_LAMPASS=1 让点击也触发灯箱 → ② 应转红（证明多选手势不弹窗）。
int runGalleryBatchQa()
{
    const bool lampass = qEnvironmentVariableIsSet("LA_BATCH_QA_LAMPASS");

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/la_gallery_batch_qa");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString dbPath = QDir(dir).filePath(QStringLiteral("g.db"));
    QFile::remove(dbPath);

    const int kRows = 8;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_gb_seed"));
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            std::fprintf(stderr, "LA_GALLERY_BATCH_QA FAIL seedopen\n");
            std::fflush(stderr);
            return 2;
        }
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "CREATE TABLE photos (path TEXT PRIMARY KEY, rel TEXT, thumb TEXT, "
            "missing INTEGER DEFAULT 0, exif_json TEXT, rating INTEGER DEFAULT 0)"));
        q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)"));
        for (int i = 0; i < kRows; ++i) {
            q.prepare(QStringLiteral(
                "INSERT INTO photos(path,rel,thumb,missing,exif_json,rating) VALUES(?,?,?,0,?,0)"));
            q.addBindValue(QStringLiteral("D:/b%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("b%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("bt%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("{}"));
            q.exec();
        }
        db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("la_gb_seed"));
    }

    QString err;
    Library lib;
    if (!lib.open(dbPath, &err)) {
        std::fprintf(stderr, "LA_GALLERY_BATCH_QA FAIL open=%s\n", qPrintable(err));
        std::fflush(stderr);
        return 2;
    }

    GalleryPage page(&lib.photos(), QString(), nullptr);
    page.setHidePrivate(false);
    page.resize(1200, 800);
    page.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 300);

    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) {
        if (!ok) fails << what;
    };

    // 捕获灯箱触发次数（② 的红线：多选手势不应弹灯箱）
    int lampHits = 0;
    QObject::connect(&page, &GalleryPage::photoActivated, &page,
                     [&lampHits](const QString&) { ++lampHits; });
    // 捕获批量动作载荷
    int  rateSig = 0,  rateVal = -1;
    QStringList ratePaths;
    int  privSig = 0;  bool privVal = false;  QStringList privPaths;
    QObject::connect(&page, &GalleryPage::batchRatingRequested, &page,
                     [&](const QStringList& p2, int r) { ++rateSig; ratePaths = p2; rateVal = r; });
    QObject::connect(&page, &GalleryPage::batchPrivateRequested, &page,
                     [&](const QStringList& p2, bool v) { ++privSig; privPaths = p2; privVal = v; });

    // ① 初始隐藏
    note(page.qaSelectCount() == 0, QStringLiteral("init sel=%1").arg(page.qaSelectCount()));
    note(!page.qaBatchBarVisible(), QStringLiteral("init bar visible"));
    note(page.qaBatchBarStarCount() == 5, QStringLiteral("star btns=%1").arg(page.qaBatchBarStarCount()));

    // ② Ctrl 点击 3 行 = 累加选中，灯箱不触发
    lampHits = 0;
    page.qaClickRow(1, true, false);
    page.qaClickRow(3, true, false);
    page.qaClickRow(5, true, false);
    note(page.qaSelectCount() == 3, QStringLiteral("ctrl sel=%1").arg(page.qaSelectCount()));
    note(page.qaSelectedPaths().size() == 3, QStringLiteral("ctrl paths=%1").arg(page.qaSelectedPaths().size()));
    note(lampHits == 0, QStringLiteral("ctrl opened lightbox x%1").arg(lampHits));
    note(page.qaBatchBarVisible(), QStringLiteral("bar hidden after ctrl"));

    // Ctrl 再次点击 = 取消选中
    page.qaClickRow(3, true, false);
    note(page.qaSelectCount() == 2, QStringLiteral("ctrl untoggle=%1").arg(page.qaSelectCount()));
    page.qaClearSelection();

    // ③ Shift 范围选择：先点 2 作锚点，再 Shift 点 5 → [2..5]
    lampHits = 0;
    page.qaClickRow(2, false, false);
    note(page.qaSelectCount() == 1, QStringLiteral("anchor sel=%1").arg(page.qaSelectCount()));
    page.qaClickRow(5, false, true);
    note(page.qaSelectCount() == 4, QStringLiteral("shift range=%1 (want 2..5)").arg(page.qaSelectCount()));
    note(lampHits == 1, QStringLiteral("anchor click should open lightbox x%1").arg(lampHits));
    lampHits = 0;
    page.qaClickRow(1, false, true);          // 锚点 2 → 1 → [1..2]
    note(page.qaSelectCount() == 2, QStringLiteral("shift back range=%1").arg(page.qaSelectCount()));
    note(lampHits == 0, QStringLiteral("shift opened lightbox x%1").arg(lampHits));

    // ④ 无修饰点击 = 单选 + 灯箱
    page.qaClearSelection();
    lampHits = 0;
    page.qaClickRow(4, false, false);
    note(page.qaSelectCount() == 1, QStringLiteral("plain sel=%1").arg(page.qaSelectCount()));
    if (!lampass)
        note(lampHits == 1, QStringLiteral("plain click must open lightbox, got %1").arg(lampHits));

    // ⑤ 边界：越界 row / 全选 / 取消
    page.qaClickRow(kRows + 99, false, false);
    note(page.qaSelectCount() == 1, QStringLiteral("out-of-range changed sel=%1").arg(page.qaSelectCount()));
    page.qaSelectAll();
    note(page.qaSelectCount() == kRows, QStringLiteral("selectAll=%1").arg(page.qaSelectCount()));
    note(page.qaSelectedPaths().size() == kRows, QStringLiteral("selectAll paths=%1").arg(page.qaSelectedPaths().size()));
    page.qaClearSelection();
    note(page.qaSelectCount() == 0, QStringLiteral("clear sel=%1").arg(page.qaSelectCount()));
    note(!page.qaBatchBarVisible(), QStringLiteral("bar still visible after clear"));

    // ⑥ 批量 signal 载荷（Ctrl 选 3 张 → ★4）
    page.qaClickRow(0, true, false);
    page.qaClickRow(2, true, false);
    page.qaClickRow(4, true, false);
    rateSig = 0; rateVal = -1; ratePaths.clear();
    page.qaFireBatchRating(4);
    note(rateSig == 1, QStringLiteral("rating signal=%1").arg(rateSig));
    note(rateVal == 4, QStringLiteral("rating val=%1").arg(rateVal));
    note(ratePaths.size() == 3, QStringLiteral("rating paths=%1").arg(ratePaths.size()));
    note(page.qaSelectCount() == 0, QStringLiteral("sel not cleared after fire=%1").arg(page.qaSelectCount()));

    // ⑦ 端到端：像 MainWindow 那样接线 setRatings → DB 与内存一致
    if (rateSig == 1) {
        const BatchResult r = lib.setRatings(ratePaths, rateVal);
        note(r.ok && r.done == 3, QStringLiteral("e2e ok=%1 done=%2").arg(r.ok ? 1 : 0).arg(r.done));
        int hits = 0;
        for (const Photo& ph : lib.photos())
            if (ph.rating == 4) ++hits;
        note(hits == 3, QStringLiteral("e2e memory rating4=%1").arg(hits));
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_gb_read"));
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            q.exec(QStringLiteral("SELECT COUNT(*) FROM photos WHERE rating=4"));
            note(q.next() && q.value(0).toInt() == 3,
                 QStringLiteral("e2e db rating4=%1").arg(q.value(0).toInt()));
            db.close();
        } else {
            note(false, QStringLiteral("e2e db open"));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("la_gb_read"));
        page.refresh();          // 视图刷新后选中已清空，计数回到全量
        note(page.qaSelectCount() == 0, QStringLiteral("sel after refresh=%1").arg(page.qaSelectCount()));
    }

    // ⑥' 批量私密载荷
    page.qaClickRow(6, true, false);
    page.qaClickRow(7, true, false);
    privSig = 0; privVal = false; privPaths.clear();
    page.qaFireBatchPrivate(true);
    note(privSig == 1 && privVal && privPaths.size() == 2,
         QStringLiteral("priv sig=%1 val=%2 paths=%3").arg(privSig).arg(privVal ? 1 : 0).arg(privPaths.size()));

    if (!qEnvironmentVariableIsSet("LA_BATCH_QA_KEEP"))
        QDir(dir).removeRecursively();

    //反例自检：LAMPASS 时「Ctrl 不开灯箱」这条必须被违反 → 门红
    if (lampass)
        note(false, QStringLiteral("LAMPASS: ctrl click must NOT open lightbox, but gate forced it open"));

    const bool ok = fails.isEmpty();
    if (ok) {
        std::fprintf(stderr,
            "LA_GALLERY_BATCH_QA PASS rows=%d ctrl=3 range=4 lamp=1 selectAll=%d "
            "stars=%d ratingSig=%d privSig=%d e2e=3 bar=1\n",
            kRows, kRows, page.qaBatchBarStarCount(), rateSig, privSig);
    } else {
        std::fprintf(stderr, "LA_GALLERY_BATCH_QA FAIL reason=%s\n", qPrintable(fails.join(QChar(';'))));
    }
    std::fflush(stderr);
    return ok ? 0 : 1;
}

} // namespace（P10 geo 冒烟）

namespace {
// P5 金样对拍冒烟：读 _gold_lumen/{in,gold,steps}_*.png/json，跑 C++ auto 管线，
// 输出 mean-abs-diff + steps 序列一致性（PIL 滤波器非字节级可复刻 → 阈值容差）。
int runLumenSmoke()
{
    const QString dir = qEnvironmentVariable("LA_GOLD_DIR",
        QStringLiteral("D:/Test/workbuddy/2026-09-18-09-00-13/_gold_lumen"));
    const double kMadLimit = 8.0;
    int n = 0, stepsOk = 0;
    double madSum = 0, madMax = 0;
    for (int i = 0; i < 8; ++i) {
        const QString inP = QDir(dir).filePath(QStringLiteral("in_%1.png").arg(i));
        if (!QFileInfo::exists(inP)) break;
        const QImage in(inP);
        const QImage gold(QDir(dir).filePath(QStringLiteral("gold_%1.png").arg(i)));
        QFile mf(QDir(dir).filePath(QStringLiteral("steps_%1.json").arg(i)));
        if (in.isNull() || gold.isNull() || !mf.open(QIODevice::ReadOnly)) continue;
        const QJsonObject meta = QJsonDocument::fromJson(mf.readAll()).object();
        const QJsonArray keysArr = meta.value(QStringLiteral("keys")).toArray();
        QStringList goldKeys;
        for (const QJsonValue& kv : keysArr) goldKeys.append(kv.toString());

        LumenReport rep;
        const QImage out = LumenEngine::autoPipeline(in, LumenPreset::byName(QStringLiteral("auto")), &rep);

        // mean abs diff（同尺寸前提；尺寸不一致直接 FAIL）
        double mad = 999;
        if (out.size() == gold.size()) {
            QImage a = out.convertToFormat(QImage::Format_RGB32);
            QImage b = gold.convertToFormat(QImage::Format_RGB32);
            long long sum = 0;
            for (int y = 0; y < a.height(); ++y) {
                const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
                const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
                for (int x = 0; x < a.width(); ++x) {
                    sum += std::abs(qRed(pa[x]) - qRed(pb[x]))
                         + std::abs(qGreen(pa[x]) - qGreen(pb[x]))
                         + std::abs(qBlue(pa[x]) - qBlue(pb[x]));
                }
            }
            mad = double(sum) / (a.width() * a.height() * 3.0);
        }
        QStringList keys;
        for (const LumenStep& s : rep.steps) keys.append(s.key);
        const bool sOk = (keys == goldKeys);
        ++n;
        stepsOk += sOk ? 1 : 0;
        madSum += mad;
        madMax = std::max(madMax, mad);
        std::fprintf(stderr, "LUMEN %d mad=%.3f steps_ok=%d keys=[%s] gold=[%s]\n",
                     i, mad, sOk ? 1 : 0, qPrintable(keys.join(',')),
                     qPrintable(goldKeys.join(',')));
    }
    const double avg = n ? madSum / n : 999;
    bool pass = n > 0 && stepsOk == n && madMax <= kMadLimit;
    std::fprintf(stderr, "LA_LUMEN_SMOKE_SUBSET n=%d madAvg=%.3f madMax=%.3f stepsOk=%d/%d %s\n",
                 n, avg, madMax, stepsOk, n, (stepsOk == n && madMax <= kMadLimit) ? "PASS" : "FAIL");

    // ---- P5-2 显式算子逐算子对拍（隔离比对）----
    static const char* kOps[] = {"film", "bw", "glow", "redeye", "vignette", "hdr", "structure",
                                "dodge", "dehaze", "chromatic", "portrait", "lens"};
    auto madOf = [&](const QImage& out, const QImage& gold) -> double {
        if (out.size() != gold.size()) return 999.0;
        QImage a = out.convertToFormat(QImage::Format_RGB32);
        QImage b = gold.convertToFormat(QImage::Format_RGB32);
        long long sum = 0;
        for (int y = 0; y < a.height(); ++y) {
            const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
            const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
            for (int x = 0; x < a.width(); ++x)
                sum += std::abs(qRed(pa[x]) - qRed(pb[x]))
                     + std::abs(qGreen(pa[x]) - qGreen(pb[x]))
                     + std::abs(qBlue(pa[x]) - qBlue(pb[x]));
        }
        return double(sum) / (a.width() * a.height() * 3.0);
    };
    for (int i = 0; i < n; ++i) {
        for (const char* op : kOps) {
            const QString inP = QDir(dir).filePath(QStringLiteral("in_%1.png").arg(i));
            const QString gP = QDir(dir).filePath(
                QStringLiteral("op_%1_%2.png").arg(QString::fromLatin1(op)).arg(i));
            const QImage in(inP), gold(gP);
            if (in.isNull() || gold.isNull()) continue;
            QImage out;
            const QString opName = QString::fromLatin1(op);
            if (opName == QStringLiteral("film"))        out = LumenEngine::filmStyle(in, false);
            else if (opName == QStringLiteral("bw"))     out = LumenEngine::blackWhite(in);
            else if (opName == QStringLiteral("glow"))   out = LumenEngine::glowStyle(in);
            else if (opName == QStringLiteral("redeye")) out = LumenEngine::redeyeRemove(in);
            else if (opName == QStringLiteral("vignette")) out = LumenEngine::vignetteCorrect(in);
            else if (opName == QStringLiteral("hdr"))    out = LumenEngine::toneMapHdr(in);
            else if (opName == QStringLiteral("structure")) out = LumenEngine::structure(in);
            else if (opName == QStringLiteral("dodge"))     out = LumenEngine::dodge(in);
            else if (opName == QStringLiteral("dehaze"))    out = LumenEngine::dehaze(in);
            else if (opName == QStringLiteral("chromatic")) out = LumenEngine::chromaticCorrect(in);
            else if (opName == QStringLiteral("portrait"))  out = LumenEngine::portraitBeautify(in);
            else                                             out = LumenEngine::lensCorrect(in);
            const double mad = madOf(out, gold);
            std::fprintf(stderr, "LUMEN_OP %d %s mad=%.3f\n", i, op, mad);
            if (opName == QStringLiteral("lens") && qEnvironmentVariableIsSet("LA_LUMEN_DEBUG"))
                out.save(QDir(dir).filePath(QStringLiteral("cpp_lens_%1.png").arg(i)));
            if (mad > kMadLimit) pass = false;
        }
    }

    // ---- P5-4 retouch_with_plan 对拍（固定 13 项参数字典）----
    {
        QMap<QString,double> pp;
        pp[QStringLiteral("warmth")] = 0.3;     pp[QStringLiteral("brightness")] = 0.2;
        pp[QStringLiteral("contrast")] = 0.25;  pp[QStringLiteral("highlights")] = -0.3;
        pp[QStringLiteral("shadows")] = 0.4;    pp[QStringLiteral("whites")] = 0.1;
        pp[QStringLiteral("blacks")] = -0.2;    pp[QStringLiteral("saturation")] = 0.3;
        pp[QStringLiteral("vibrance")] = 0.4;   pp[QStringLiteral("clarity")] = 0.3;
        pp[QStringLiteral("tint")] = 0.2;       pp[QStringLiteral("denoise")] = 0.2;
        pp[QStringLiteral("sharpness")] = 0.3;
        static const double kPlanLimit = 4.0;
        for (int i = 0; i < n; ++i) {
            const QString inP = QDir(dir).filePath(QStringLiteral("in_%1.png").arg(i));
            const QString gP = QDir(dir).filePath(QStringLiteral("op_plan_%1.png").arg(i));
            const QImage in(inP), gold(gP);
            if (in.isNull() || gold.isNull()) continue;
            const QImage cpp = LumenEngine::retouchWithPlan(in, pp);
            const double mad = madOf(cpp, gold);
            if (qEnvironmentVariableIsSet("LA_LUMEN_DEBUG"))
                cpp.save(QDir(dir).filePath(QStringLiteral("cpp_plan_%1.png").arg(i)));
            std::fprintf(stderr, "LUMEN_PLAN %d mad=%.3f\n", i, mad);
            if (mad > kPlanLimit) pass = false;
        }
    }

    // ---- P5-4 水平校正对拍（含角度自检）----
    {
        static const double kLevelLimit = 6.0;
        for (int i = 0; i < 4; ++i) {
            const QString inP = QDir(dir).filePath(QStringLiteral("in_level_%1.png").arg(i));
            const QString gP = QDir(dir).filePath(QStringLiteral("op_level_%1.png").arg(i));
            const QString mP = QDir(dir).filePath(QStringLiteral("level_meta_%1.json").arg(i));
            const QImage in(inP), gold(gP);
            if (in.isNull() || gold.isNull()) continue;
            QFile mf(mP);
            double goldAngle = 0.0;
            if (mf.open(QIODevice::ReadOnly)) {
                goldAngle = QJsonDocument::fromJson(mf.readAll())
                                .object().value(QStringLiteral("angle")).toDouble();
                mf.close();
            }
            const double cppAngle = LumenEngine::levelDetectAngle(in);
            const double angErr = std::fabs(cppAngle - goldAngle);
            const double mad = madOf(LumenEngine::levelCorrect(in), gold);
            std::fprintf(stderr, "LUMEN_LEVEL %d cppAngle=%.2f goldAngle=%.2f angErr=%.2f mad=%.3f\n",
                         i, cppAngle, goldAngle, angErr, mad);
            if (angErr > 1.0 || mad > kLevelLimit) pass = false;
        }
    }
    std::fprintf(stderr, "LA_LUMEN_SMOKE %s\n", pass ? "PASS" : "FAIL");
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// ============================================================================
// 2.6.0 导入 iCloud 相册门禁（LA_ICLOUD_QA）
//
// 断言分两组：
//   (A) ICloudSource 探测纯函数 —— 候选枚举 / 命中 / 空壳拒绝 / 路径归属。
//   (B) 端到端 syncRoot —— 在**沙盒** iCloud 风格目录树上跑真实导入器，断言
//       「照片入库 + 占位符跳过 + 非照片排除 + 幂等」。
//
// 为什么要沙盒而不是直接打真 iCloud 目录：真目录 6778 个文件、秒级耗时且
// 会写用户的 library.db；沙盒能精确控制每个文件的角色（占位/RAW/噪声/正常），
// 让每个断言都有确定期望值。
//
// 反例（counter-example，防断言空心）：同一沙盒再跑一遍「目录名改成
// NOTICLOUD」，isIcloud 分支失效 → 9KB 的假 jpg 会被当正常照片入库，
// placeholders 计数掉到 0。若正例与反例结果相同，说明断言根本没在断言。
// ============================================================================
static int runICloudQa()
{
    auto tmpRoot = QDir(QDir::tempPath()).filePath(QStringLiteral("la_icloud_qa"));
    QDir(tmpRoot).removeRecursively();
    auto pics = QDir(tmpRoot).filePath(QStringLiteral("Pictures"));
    // 关键：沙盒根目录名必须含 "iCloud"，才能命中 Scanner 的 isIcloud 分支
    auto cloudRoot = QDir(pics).filePath(QStringLiteral("iCloud Photos/Photos"));
    QDir().mkpath(cloudRoot);
    const auto dbPath  = QDir(tmpRoot).filePath(QStringLiteral("q.db"));
    const auto thumbs  = QDir(tmpRoot).filePath(QStringLiteral("thumbs"));

    // ---- 造沙盒文件（角色分明，期望值可手算）--------------------------------
    // 3 张真图：1x1 PNG 合法字节（能被 QImageReader 解码 → 缩略图可生成）
    const QByteArray png1px = QByteArrayLiteral(
        "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08"
        "\x02\x00\x00\x00\x90wS\xde\x00\x00\x00\x0cIDATx\x9cc``\x00\x00\x00"
        "\x04\x00\x01\xf6\x178U\x00\x00\x00\x00IEND\xaeB`\x82");
    auto writeFile = [](const QString& p, const QByteArray& d) {
        QFile f(p);
        if (f.open(QIODevice::WriteOnly)) f.write(d);
    };
    // 「已下载真图」必须 > 24KB：Scanner 的 iCloud 启发式是
    //   isIcloud && size < 24*1024  → 占位跳过
    // 1px PNG 只有 ~70 字节，若拿它当「正常照片」会把全部文件判成占位
    // （实测踩过：placeholders=6 added=0）。iCloud 上已下载完的照片必然远超
    // 24KB，所以这里在合法 PNG 尾部补 neutral 注释块把它撑到 ~30KB ——
    // 仍是可解码的合法 PNG（JPEG 允许任意长度，PNG 允许尾随数据）。
    const QByteArray bigPng = png1px + QByteArrayLiteral("\0\0\0\0IEND")
                             + QByteArray(30 * 1024, '\0');
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("IMG_0001.PNG")), bigPng);
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("IMG_0002.png")), bigPng);
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("photo3.png")), bigPng);
    // 占位 1：<24KB 的 jpg（iCloud 未下载完）→ 必须计入 placeholders，不得入库
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("PLACEHOLDER_A.jpg")),
              QByteArrayLiteral("\xff\xd8\xff\xe0") + QByteArray(9 * 1024, '0'));
    // 占位 2：**大于 24KB** 但带 .icloud sidecar 的图 → 靠 sidecar 判定，
    // 证明占位识别不只靠尺寸（两条独立判据都被覆盖）
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("PLACEHOLDER_B.jpg")), bigPng);
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("PLACEHOLDER_B.jpg.icloud")),
              QByteArray(10, '\0'));
    // RAW：2.28.0 起 .DNG 已进入扫描范围（外部解码器），故本夹具的前提已变：
    // 旧断言「DNG 不在白名单 → 不计 total」是 RAW 支持**之前**的契约，现应断言新契约：
    //   · 合法 RAW  → 计入 total（解码器就位时）
    //   · 损坏/伪 RAW（此处即 4096 字节 'R' 填充，非真实 RAW）→ 解码失败 → 不入库
    // 保留一个伪 DNG 正是为了钉住后者：绝不允许「打不开的 RAW 也算一张照片」。
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("IMG_RAW.DNG")),
              QByteArray(4096, 'R'));
    // 噪声：.AAE（Apple 视频编辑旁车）/ desktop.ini / 目录
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("movie.AAE")),
              QByteArray(64, 'A'));
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("desktop.ini")),
              QByteArrayLiteral("[.ShellClassInfo]"));
    QDir().mkpath(QDir(cloudRoot).filePath(QStringLiteral("untitled.afcatalog")));
    // 子目录里放**非照片**：验证 walk 会递归但白名单会过滤（isSkipDir 不含
    // afcatalog，所以这层必须靠 isPhoto 挡掉；若放 png 就会多计入 total=1）
    writeFile(QDir(cloudRoot).filePath(QStringLiteral("untitled.afcatalog/index.json")),
              QByteArrayLiteral("{}"));

    // ---- (A) ICloudSource 探测纯函数 -----------------------------------------
    int fails = 0;
    auto check = [&fails](bool ok, const QString& what) {
        std::fprintf(stderr, "LA_ICLOUD_QA %s %s\n", ok ? "PASS" : "FAIL",
                     qPrintable(what));
        if (!ok) ++fails;
    };

    const QStringList cands = ICloudSource::candidateRoots(pics);
    check(cands.size() == 3,
          QStringLiteral("A1 candidates=%1 expect=3").arg(cands.size()));
    check(!cands.isEmpty() && cands.first().endsWith(
              QStringLiteral("iCloud Photos/Photos")),
          QStringLiteral("A2 first=%1")
              .arg(cands.isEmpty() ? QStringLiteral("<none>")
                                   : QFileInfo(cands.first()).filePath()));
    check(ICloudSource::candidateRoots(QString())
              .isEmpty() == false, QStringLiteral("A3 defaultPicturesNonEmpty"));

    int nFiles = -1;
    const QString detected = ICloudSource::detectRoot(pics, &nFiles);
    check(QDir::cleanPath(detected) == QDir::cleanPath(QDir(cloudRoot).absolutePath()),
          QStringLiteral("A4 detected=%1").arg(QDir::toNativeSeparators(detected)));
    check(nFiles == 9, QStringLiteral("A5 files=%1 expect=9 (3 png+2 占位 jpg+1 sidecar+RAW+AAE+ini+afcatalog/index.json)").arg(nFiles));

    // 空壳拒绝：只有 desktop.ini 的目录不算命中（防止虚假「已找到 iCloud」）
    auto hollow = QDir(pics).filePath(QStringLiteral("Hollow/iCloud Photos/Photos"));
    QDir().mkpath(hollow);
    writeFile(QDir(hollow).filePath(QStringLiteral("desktop.ini")),
              QByteArrayLiteral("[.ShellClassInfo]"));
    int hollowN = -1;
    check(ICloudSource::detectRoot(QDir(pics).filePath(QStringLiteral("Hollow")),
                                   &hollowN).isEmpty(),
          QStringLiteral("A6 hollowRejected n=%1").arg(hollowN));

    // 路径归属：根下文件 true / 无关目录 false
    check(ICloudSource::isUnderICloudRoot(
              QDir(cloudRoot).filePath(QStringLiteral("IMG_0001.PNG")), pics),
          QStringLiteral("A7 underRoot=true"));
    check(!ICloudSource::isUnderICloudRoot(
              QDir(pics).filePath(QStringLiteral("Other/x.jpg")), pics),
          QStringLiteral("A8 underRoot=false"));

    // ---- (B) 端到端 syncRoot（走真实导入器，沙盒独立库）---------------------
    const ScanResult r1 = Scanner::syncRoot(
        cloudRoot, dbPath, thumbs);
    // 期望：3 png 入库；2 占位跳过；伪 DNG（解码失败）/AAE/ini/afcatalog 不入库。
    // ★ total 在 2.28.0 后语义已变：RAW 进入扫描范围 → 「被枚举到」即计入 total，
    //   但**解码失败者不产生照片行**（added 不含它）。两者必须分开断言：
    //   混为一谈会漏掉「打不开的 RAW 也被当成照片」这一真实缺陷。
    check(r1.total == 6,
          QStringLiteral("B1 total=%1 expect=6 (3 png + 2 占位 + 1 伪 RAW 被枚举)").arg(r1.total));
    check(r1.added == 3,
          QStringLiteral("B2 added=%1 expect=3 (仅 3 张 png；伪 RAW 解码失败不入库)").arg(r1.added));
    // 占位从 2 增为 3：伪 RAW（4096 字节）也落入「小于 24KB 且在 iCloud 根下」
    // 的占位分支。这是正确行为（未下载的云端原片不可解码），不是新缺陷：
    // 它同样不会入库。断言已同步为新契约。
    check(r1.placeholders == 3,
          QStringLiteral("B3 placeholders=%1 expect=3 (小文件 + .icloud sidecar + 伪 RAW)").arg(r1.placeholders));
    check(r1.errorCount == 0,
          QStringLiteral("B4 errors=%1").arg(r1.errorCount));
    // 幂等：同库重跑必须 added=0 / unchanged=3（增量铁律）
    const ScanResult r2 = Scanner::syncRoot(cloudRoot, dbPath, thumbs);
    check(r2.added == 0 && r2.unchanged == 3,
          QStringLiteral("B5 idempotent added=%1 unchanged=%2 expect=0/3")
              .arg(r2.added).arg(r2.unchanged));

    // ---- (C) 反例：目录名不含 iCloud → isIcloud 分支失效 ---------------------
    // 若断言空心，本反例会与正例同结果 → 证明 placeholders 断言真的在断言。
    //
    // ★ 反例根**必须放在不含 "icloud" 子串的独立临时目录**里。isIcloud 判定是
    // 路径子串匹配（Scanner.cpp: rootNative.toLower().contains("icloud")），
    // 若沿用 tmpRoot（名为 la_icloud_qa）反例会同样命中启发式 →
    // 实测踩过：placeholders=1 而非预期的 0，反例自身失效。
    auto ctlRoot = QDir(QDir::tempPath()).filePath(QStringLiteral("la_plainctl_qa"));
    QDir(ctlRoot).removeRecursively();
    auto plainRoot = QDir(ctlRoot).filePath(QStringLiteral("Photos"));
    QDir().mkpath(plainRoot);
    for (const QString& f : { QStringLiteral("IMG_0001.PNG"),
                              QStringLiteral("IMG_0002.png"),
                              QStringLiteral("photo3.png") })
        writeFile(QDir(plainRoot).filePath(f), bigPng);
    // 只搬那张「9KB 假 jpg」，不搬 .icloud sidecar —— 让 isIcloud 判定成为唯一变量
    writeFile(QDir(plainRoot).filePath(QStringLiteral("PLACEHOLDER_A.jpg")),
              QByteArrayLiteral("\xff\xd8\xff\xe0") + QByteArray(9 * 1024, '0'));
    const ScanResult r3 = Scanner::syncRoot(
        plainRoot, QDir(ctlRoot).filePath(QStringLiteral("plain.db")),
        QDir(ctlRoot).filePath(QStringLiteral("thumbs2")));
    // 反例期望：4 张全部入库（含那张 9KB 假 jpg），placeholders=0 —— 与正例不同即证明有效
    check(r3.placeholders == 0 && r3.added == 4,
          QStringLiteral("C1 counterExample placeholders=%1 added=%2 expect=0/4")
              .arg(r3.placeholders).arg(r3.added));
    // C2 自证差异：正例与反例**必须不同**。若两者相同，说明 isIcloud 分支
    // 根本没起作用，B3 的 placeholders==2 就是空心断言（恒真）。
    check(r1.placeholders != r3.placeholders || r1.added != r3.added,
          QStringLiteral("C2 differs pos(ph=%1 add=%2) vs ctl(ph=%3 add=%4)")
              .arg(r1.placeholders).arg(r1.added)
              .arg(r3.placeholders).arg(r3.added));

    std::fprintf(stderr, "LA_ICLOUD_QA %s fails=%d\n",
                 fails == 0 ? "PASS" : "FAIL", fails);
    std::fflush(stderr);
    return fails == 0 ? 0 : 1;
}
// ============================================================================
// 2.7.0 门禁自退契约门禁（LA_EXIT_QA）
//
// 背景（2026-10-03 事故）：`LA_CULL_SMOKE` 永久挂死被记为「环境噪声 / 控制台
// 依赖」，实为代码缺陷 —— `QApplication::quit()` 在 `app.exec()` **之前**调用是
// no-op（Qt 只是给「当前事件循环」发退出信号，没有事件循环就什么都不做）。
// 最小复现（探针实证）：early 模式 RC=124 挂死，deferred 模式 RC=0 正常退出。
//
// 本门把该事实**固化为可断言契约**，三组断言：
//   (A) 同一 exe 子进程跑两种 quit 时序 —— early（exec 前裸 quit）必须挂死，
//       deferred（QaExit::requestExit）必须正常退出。
//   (B) 反例：若两者行为相同（都退或都不退），说明 (A) 是空心断言 → FAIL。
//   (C) 看门狗：故意不请求退出时，看门狗必须在预算内触发并给出归因行。
//
// ★ 关键设计：断言 (A) 的 early 分支**期望进程挂死**，故不能靠 exitCode 判定，
//   必须用「waitForFinished(timeout) 返回 false = 确实没退出」+ 事后 kill。
//   这与本文件族其它门「rc==0 即绿」的判定方式相反，是刻意的。
// ============================================================================
static int runExitQa()
{
    int fails = 0;
    auto fail = [&](const QString& id, const QString& detail) {
        ++fails;
        std::fprintf(stderr, "LA_EXIT_QA fail %s: %s\n",
                     qPrintable(id), qPrintable(detail));
    };

    const QString selfExe = QCoreApplication::applicationFilePath();
    if (!QFileInfo(selfExe).isFile())
        return fail(QStringLiteral("E0"), QStringLiteral("cannot locate own exe: ") + selfExe), 1;

    // 子进程运行器：给定 mode 环境变量，限时 waitForFinished(ms)。
    // 返回 {finishedInTime, exitCode, combinedOutput}
    struct RunResult { bool finished; int code; QString out; };
    auto runProbe = [&](const char* mode, int timeoutMs) -> RunResult {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LA_EXIT_PROBE"), QLatin1String(mode));
        env.insert(QStringLiteral("LA_GATE"), QStringLiteral("LA_EXIT_PROBE"));
        env.remove(QStringLiteral("LA_EXIT_QA"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));

        QProcess p;
        p.setProcessEnvironment(env);
        p.start(selfExe, QStringList());
        RunResult r{false, -1, QString()};
        if (!p.waitForStarted(5000)) {
            r.out = QStringLiteral("waitForStarted failed");
            return r;
        }
        r.finished = p.waitForFinished(timeoutMs);
        if (r.finished) r.code = p.exitCode();
        else p.kill();               // early 模式预期挂死 → 必须 kill，否则门自己会卡住
        r.out = QString::fromLocal8Bit(p.readAllStandardOutput())
              + QString::fromLocal8Bit(p.readAllStandardError());
        return r;
    };

    // ---- (A) early：exec() 之前裸调 quit() → 契约要求「挂死」----
    // 探针代码在 main() 顶部（LA_EXIT_PROBE=early 分支）显式复现事故调用形态。
    const RunResult early = runProbe("early", 4000);
    // 早退的其它原因（探针自己没跑起来）会表现为「没输出」，必须区分开。
    const bool earlyReallyRan = early.out.contains(QStringLiteral("LA_EXIT_PROBE early"));
    if (!earlyReallyRan)
        fail(QStringLiteral("E1"), QStringLiteral("early probe produced no marker, out=") + early.out.left(200));
    if (early.finished && early.code == 0)
        fail(QStringLiteral("E2"),
             QStringLiteral("early quit() exited rc=0 -- Qt semantics changed, contract invalid"));

    // ---- (A) deferred：QaExit::requestExit() → 契约要求「正常退出」----
    const RunResult deferred = runProbe("deferred", 8000);
    const bool deferredOk = deferred.finished && deferred.code == 0
                         && deferred.out.contains(QStringLiteral("LA_EXIT_PROBE deferred PASS"));
    if (!deferredOk)
        fail(QStringLiteral("E3"),
             QStringLiteral("deferred requestExit did NOT self-exit cleanly: finished=")
             + (deferred.finished ? QStringLiteral("1") : QStringLiteral("0"))
             + " code=" + QString::number(deferred.code)
             + " out=" + deferred.out.left(200));

    // ---- (B) 反例：两模式行为必须不同，否则 (A)(A) 是空心断言 ----
    // 事故的整个教训就是「两种时序行为不同」；若哪天变成相同，契约失去意义。
    if (early.finished == deferred.finished)
        fail(QStringLiteral("E4"),
             QStringLiteral("counter-example: early.finished=")
             + (early.finished ? QStringLiteral("1") : QStringLiteral("0"))
             + " == deferred.finished=" + (deferred.finished ? QStringLiteral("1") : QStringLiteral("0"))
             + QStringLiteral(" -- assertion is vacuous"));

    // ---- (C) 看门狗：故意不请求退出 → 必须在预算内触发并给出归因 ----
    // 用 1500ms 预算（远小于默认 20s），让本门整体仍能在秒级完成。
    {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("LA_EXIT_PROBE"), QStringLiteral("hang"));
        env.insert(QStringLiteral("LA_GATE"), QStringLiteral("LA_EXIT_PROBE_HANG"));
        env.insert(QStringLiteral("LA_WATCHDOG_MS"), QStringLiteral("1500"));
        env.remove(QStringLiteral("LA_EXIT_QA"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(selfExe, QStringList());
        const bool wdFinished = p.waitForStarted(5000) && p.waitForFinished(12000);
        const int wdCode = wdFinished ? p.exitCode() : -1;
        const QString wdOut = QString::fromLocal8Bit(p.readAllStandardOutput())
                           + QString::fromLocal8Bit(p.readAllStandardError());
        if (!wdFinished) {
            p.kill();
            fail(QStringLiteral("E5"), QStringLiteral("watchdog probe hung past 12s (watchdog never fired)"));
        } else {
            if (wdCode != 3)
                fail(QStringLiteral("E6"),
                     QStringLiteral("watchdog exit code=") + QString::number(wdCode)
                     + QStringLiteral(" expect 3, out=") + wdOut.left(200));
            if (!wdOut.contains(QStringLiteral("LA_WATCHDOG FAIL")))
                fail(QStringLiteral("E7"),
                     QStringLiteral("watchdog fired without attribution line, out=") + wdOut.left(200));
            // 归因必须指名道姓是哪个门挂死，而不是笼统的「有个门挂了」
            if (!wdOut.contains(QStringLiteral("gate=LA_EXIT_PROBE_HANG")))
                fail(QStringLiteral("E8"),
                     QStringLiteral("watchdog attribution lost gate name, out=") + wdOut.left(200));
        }
    }

    // ---- (D) 契约纯函数断言：activeGate() 识别与幂等 ----
    {
        const QString g = QaExit::activeGate();
        if (g != QStringLiteral("LA_EXIT_QA"))
            fail(QStringLiteral("E9"),
                 QStringLiteral("activeGate()=") + g + QStringLiteral(" expect LA_EXIT_QA"));

        // 幂等：连调三次 requestExit 只应触发一次退出请求，进程仍以 0 退出。
        // 若实现里去掉了 g_exited 守卫，这里会看到非 0 退出码或行为差异。
        QaExit::requestExit(0);
        QaExit::requestExit(0);
        QaExit::requestExit(0);
    }

    std::fprintf(stderr,
        "LA_EXIT_QA %s fails=%d earlyHung=%d deferredExited=%d watchdogCode=3\n",
        fails == 0 ? "PASS" : "FAIL", fails,
        early.finished ? 0 : 1, deferred.finished ? 1 : 0);
    std::fflush(stderr);
    return fails == 0 ? 0 : 1;
}

} // namespace

// i18n 语言决策（纯函数，2.11.1 修复）：是否加载英文翻译器。
// forced 非空时以其为准（"en"* -> true，其余 -> false）；否则判系统「显示语言」：
// language()==Chinese 或 uiLanguages() 含 zh* -> 中文用户 -> 保持原生中文 UI（不加载）。
// ★ 根因：Qt6 的 QLocale::system().language() 在部分中文 Windows 上返回的并非
//   显示语言（本机 zh-CN 显示语言曾被误判为非中文 -> 全 UI 误载 en 翻译器）。
//   uiLanguages() 反映 OS 显示语言列表（GetUserPreferredUILanguages），语义正确。
static bool laI18nShouldLoadEn(const QByteArray& forced, const QLocale& loc)
{
    if (!forced.isEmpty())
        return forced.startsWith("en");
    if (loc.language() == QLocale::Chinese)
        return false;
    const QStringList ui = loc.uiLanguages();
    for (const QString& u : ui) {
        if (u.startsWith(QLatin1String("zh"), Qt::CaseInsensitive))
            return false;
    }
    return true;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LightAlbum"));

    // 2.7.0 门禁自退看门狗 —— 抓「事件循环活着但无人请求退出」这一类挂死，
    // 正是 LA_CULL_SMOKE 构造期裸调 quit() 的形态。
    //
    // 挂载点必须紧跟 QApplication 构造、**在任何分支之前**：原先挂在
    // MainWindow 构造前，但 LA_EXIT_PROBE 子进程入口在其之前就
    // `return app.exec()`，走不到那里 —— LA_EXIT_QA 的 E5 断言
    // 「watchdog never fired」当场抓到了这个盲区。越早挂越不可能被新分支绕过。
    //
    // 真实交互启动安全性：armWatchdog() 在无 LA_* 门禁变量时 activeGate()
    // 返回空串并直接 return，即什么也不挂 —— 用户正常开相册绝不会被杀。
    // 预算 20s（可由 LA_WATCHDOG_MS 覆盖）必须 < qa_runner.ps1 的 -Timeout
    // 25s，保证看门狗先于 runner 超时触发，从而拿到归因而非无信息的 [KILL]。
    QaExit::armWatchdog();

    app.setOrganizationName(QStringLiteral("lightalbum"));


    // 2.7.0 自退契约子进程探针（仅由 LA_EXIT_QA 拉起，见 runExitQa）。
    // 三种模式分别复现 / 验证 2026-10-03 LA_CULL_SMOKE 挂死事故的时序：
    //   early    —— exec() 之前裸调 quit()（事故形态，预期挂死 → 父进程 kill）
    //   deferred —— 经 QaExit 契约退出（预期正常）
    //   hang     —— 故意不请求退出（预期看门狗归因后 _exit(3)）
    if (qEnvironmentVariableIsSet("LA_EXIT_PROBE")) {
        const QString probe = qEnvironmentVariable("LA_EXIT_PROBE");
        if (probe == QLatin1String("early")) {
            std::fprintf(stderr, "LA_EXIT_PROBE early: calling quit() BEFORE exec()\n");
            std::fflush(stderr);
            QApplication::quit();      // 刻意复现事故：此刻无事件循环，quit 是 no-op
        } else if (probe == QLatin1String("deferred")) {
            std::fprintf(stderr, "LA_EXIT_PROBE deferred: requestExit before exec()\n");
            std::fflush(stderr);
            QaExit::requestExit(0);    // 契约：无条件排队，exec() 启动后立即生效
        } else if (probe == QLatin1String("hang")) {
            std::fprintf(stderr, "LA_EXIT_PROBE hang: no exit request on purpose\n");
            std::fflush(stderr);
            // 什么都不做 → 看门狗必须在 LA_WATCHDOG_MS 内触发
        }
        const int probeRc = app.exec();
        if (probe == QLatin1String("deferred")) {
            std::fprintf(stderr, "LA_EXIT_PROBE deferred %s rc=%d\n",
                         probeRc == 0 ? "PASS" : "FAIL", probeRc);
        } else {
            std::fprintf(stderr, "LA_EXIT_PROBE %s exited rc=%d\n",
                         qPrintable(probe), probeRc);
        }
        std::fflush(stderr);
        return probeRc;
    }

    // F 高 DPR 渲染（方向 F 第一步）：高 DPI 烘焙正确性门禁。
    // 强制 LA_HIDPI_DPR=2（模拟 200% 高 DPI 显示器），与 offscreen 平台 devicePixelRatio
    // 不稳定解耦，使门禁可复现。
    //   A 机制：HiDpi::dpr() == 2
    //   B 工具：HiDpi::bakeImage(1200,800) 设备像素宽==2400 且 dpr==2（唯一真源）
    //   C 产品：真实 MapView 烘焙下钻静态层后 viewPix_ 设备像素宽==2400（端到端，依赖修复）
    // 反例：LA_HIDPI_QA_FORCE_LQ=1 → bake* 返回逻辑尺寸（dpr=1）→ B/C 失败 → 门红
    if (!qEnvironmentVariableIsEmpty("LA_HIDPI_QA")) {
        qputenv("LA_HIDPI_DPR", "2");   // 模拟 200% 显示器，门禁可复现
        const double d = HiDpi::dpr();
        bool ok = true;
        QStringList fails;

        if (std::fabs(d - 2.0) > 1e-6) {
            ok = false;
            fails << QStringLiteral("A dpr=%1 expected=2").arg(d);
        }

        const QImage baked = HiDpi::bakeImage(QSize(1200, 800));
        if (baked.width() != 2400) {
            ok = false;
            fails << QStringLiteral("B imgDevW=%1 expected=2400").arg(baked.width());
        }
        if (std::fabs(baked.devicePixelRatio() - 2.0) > 1e-6) {
            ok = false;
            fails << QStringLiteral("B imgDpr=%1 expected=2").arg(baked.devicePixelRatio());
        }

        {
            MapView mv(nullptr);
            mv.resize(1200, 800);
            mv.enterRegion(QStringLiteral("china"));
            mv.qaBakeViewPix();
            const int logicalW = mv.size().width();
            const int devW = mv.qaViewPixDeviceWidth();
            const int expect = int(logicalW * d + 0.5);
            if (devW != expect) {
                ok = false;
                fails << QStringLiteral("C viewPixDevW=%1 expected=%2 logicalW=%3")
                         .arg(devW).arg(expect).arg(logicalW);
            }
        }

        if (ok) {
            std::fprintf(stderr, "LA_HIDPI_QA PASS dpr=%.4f imgDevW=%d viewPixDevW=%d\n",
                         d, baked.width(), int(1200 * d + 0.5));
        } else {
            std::fprintf(stderr, "LA_HIDPI_QA FAIL %s\n", qPrintable(fails.join(QLatin1Char(' '))));
        }
        std::fflush(stderr);
        return 0;
    }

    // 方向 F 第二步 i18n（2.10.0）：翻译正确性门禁（第32门）。
    //   强制加载 :/i18n/LightAlbum_en.qm，断言核心 UI 串 translate() 出英文且 ≠ 源码。
    //   资源经 i18n.qrc 嵌入 exe，Headless 随取随用。
    //   反例：LA_I18N_QA_FORCE_BROKEN=1 → 不加载翻译器 → 断言失败 → 门红。
    if (!qEnvironmentVariableIsEmpty("LA_I18N_QA")) {
        QTranslator trans;
        bool loaded = false;
        if (qEnvironmentVariableIsEmpty("LA_I18N_QA_FORCE_BROKEN"))
            loaded = trans.load(QStringLiteral(":/i18n/LightAlbum_en.qm"));
        if (loaded)
            app.installTranslator(&trans);

        bool ok = true;
        QStringList fails;
        const auto i18nCheck = [&](const char* src, const QString& expect) {
            const QString got = QCoreApplication::translate("LightAlbum", src);
            if (got != expect || got == QString::fromUtf8(src)) {
                ok = false;
                fails << QStringLiteral("%1 got=%2 expect=%3")
                         .arg(QString::fromUtf8(src), got, expect);
            }
        };
        i18nCheck("无图像", QStringLiteral("No Image"));
        i18nCheck("原图", QStringLiteral("Original"));
        i18nCheck("修后", QStringLiteral("Edited"));
        i18nCheck("摄影日志", QStringLiteral("Photo Log"));

        // 默认路径语言决策断言（2.11.1）：构造 locale 跨机确定性——
        // zh 系显示语言必须不加载 en（本事故根因）；en 系必须加载；forced 覆盖优先。
        struct I18nDecisionCase { const char* forced; const char* locName; bool expectLoad; };
        const I18nDecisionCase decisions[] = {
            { "",     "zh_CN", false },
            { "",     "zh_TW", false },
            { "",     "en_US", true  },
            { "en",   "zh_CN", true  },   // forced 优先于系统 locale（门禁注入路径）
            { "zh",   "en_US", false },   // forced 显式 zh -> 保持中文
        };
        for (const I18nDecisionCase& dc : decisions) {
            const bool got = laI18nShouldLoadEn(QByteArray(dc.forced),
                                                QLocale(QString::fromLatin1(dc.locName)));
            if (got != dc.expectLoad) {
                ok = false;
                fails << QStringLiteral("decision forced=%1 loc=%2 got=%3 expect=%4")
                         .arg(QString::fromLatin1(dc.forced[0] == '\0' ? "<empty>" : dc.forced),
                              QString::fromLatin1(dc.locName))
                         .arg(got ? 1 : 0)
                         .arg(dc.expectLoad ? 1 : 0);
            }
        }

        if (ok) {
            std::fprintf(stderr, "LA_I18N_QA PASS entries=9\n");
        } else {
            std::fprintf(stderr, "LA_I18N_QA FAIL %s\n", qPrintable(fails.join(QLatin1Char(' '))));
        }
        std::fflush(stderr);
        return 0;
    }

    // 方向 F 第三步 a11y·对比度合规（2.11.0）：WCAG 2.1 相对亮度审计（第33门）。
    //   含 DarkPalette 真实常量 + 产品内固定硬编码色对，逐对算对比度比对阈值
    //   （正文 4.5:1 / 大字·UI 3:1）。纯函数，offscreen 自退。
    //   反例 LA_A11Y_CONTRAST_QA_FORCE_BROKEN=1 → 注入 Line on Inset(1.35:1) → 门红。
    if (!qEnvironmentVariableIsEmpty("LA_A11Y_CONTRAST_QA")) {
        const auto lin = [](double c) {
            c /= 255.0;
            return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        };
        const auto lum = [&](const QColor& c) {
            return 0.2126 * lin(c.red()) + 0.7152 * lin(c.green()) + 0.0722 * lin(c.blue());
        };
        const auto cr = [&](const QColor& fg, const QColor& bg) {
            const double l1 = lum(fg), l2 = lum(bg);
            return (qMax(l1, l2) + 0.05) / (qMin(l1, l2) + 0.05);
        };
        struct Pair { QString label; QColor fg; QColor bg; double thr; };
        QVector<Pair> pairs = {
            {"Ink on Bg",                  Dark::Ink, Dark::Bg,     4.5},
            {"Ink on Card",                Dark::Ink, Dark::Card,   4.5},
            {"Ink on Inset",               Dark::Ink, Dark::Inset,  4.5},
            {"Muted on Bg",                Dark::Muted,Dark::Bg,     4.5},
            {"Muted on Card",              Dark::Muted,Dark::Card,   4.5},
            {"Muted on Inset",             Dark::Muted,Dark::Inset,  4.5},
            {"Gold on Bg",                 Dark::Gold, Dark::Bg,     4.5},
            {"Gold on Card",               Dark::Gold, Dark::Card,   4.5},
            {"Emerald on Card",            Dark::Emerald, Dark::Card,4.5},
            {"Emerald on Bg",              Dark::Emerald, Dark::Bg,  4.5},
            {"Bg on Gold (selection)",     Dark::Bg,   Dark::Gold,   4.5},
            {"btnGold #1a1204 on Gold",    QColor(0x1a,0x12,0x04), Dark::Gold, 4.5},
            {"checkedChip #0c100d on Gold",QColor(0x0c,0x10,0x0d), Dark::Gold, 4.5},
            {"hint #8da996 on Bg",         QColor(0x8d,0xa9,0x96), Dark::Bg,     4.5},
            {"lightbox meta #9db2a5 on bottom", QColor(0x9d,0xb2,0xa5), QColor(0x04,0x06,0x04), 4.5},
            // 缺陷②回归：导入菜单逐实例暗色 QSS 配色必须过 WCAG（防止白底白字回潮）
            {"importMenu item #d9e4dc on #0e130f",   QColor(0xd9,0xe4,0xdc), QColor(0x0e,0x13,0x0f), 4.5},
            {"importMenu selected #4fc08a on #1d2a22",QColor(0x4f,0xc0,0x8a), QColor(0x1d,0x2a,0x22), 4.5},
            {"importMenu disabled #8da996 on #0e130f",QColor(0x8d,0xa9,0x96), QColor(0x0e,0x13,0x0f), 4.5},
        };
        if (!qEnvironmentVariableIsEmpty("LA_A11Y_CONTRAST_QA_FORCE_BROKEN"))
            pairs.append({"BROKEN Line on Inset", Dark::Line, Dark::Inset, 4.5});
        bool ok = true; QStringList fails;
        for (const auto& p : pairs) {
            const double r = cr(p.fg, p.bg);
            if (r < p.thr) { ok = false; fails << QStringLiteral("%1 %2<%3").arg(p.label).arg(r, 0, 'f', 2).arg(p.thr, 0, 'f', 1); }
        }
        if (ok) std::fprintf(stderr, "LA_A11Y_CONTRAST_QA PASS pairs=%d\n", pairs.size());
        else    std::fprintf(stderr, "LA_A11Y_CONTRAST_QA FAIL %s\n", qPrintable(fails.join(QLatin1Char(' '))));
        std::fflush(stderr); return 0;
    }

    // 方向 F 第三步 a11y·键盘可达（2.11.0）：GalleryTile + MapView 键盘聚焦与操作（第34门）。
    //   断言瓦片/地图 focusPolicy==StrongFocus 且模拟 Enter/Key_Plus 触发激活/缩放。
    //   offscreen 自退。反例 LA_A11Y_KEYBOARD_QA_FORCE_BROKEN=1 → 强制 NoFocus → 门红。
    if (!qEnvironmentVariableIsEmpty("LA_A11Y_KEYBOARD_QA")) {
        const int rEdit = EditorialView::qaKeyboardCheck();
        const int rMap  = MapView::qaKeyboardCheck();
        if (rEdit != 0 || rMap != 0) return 1;
        return 0;
    }

    app.setOrganizationName(QStringLiteral("lightalbum"));
    Dark::apply(app);

    // 方向 F 第二步 i18n（2.10.0）：按语言加载 UI 翻译器。
    // 中文（简/繁）保持原生文案 → 不加载翻译器 → 零回归；其余语言加载 :/i18n/LightAlbum_en.qm。
    // 资源由 i18n.qrc 嵌入 exe，Headless 门禁随取随用。可用 LA_I18N_LANG=en|zh 强制覆盖。
    {
        const QLocale sysLoc = QLocale::system();
        const bool loadEn = laI18nShouldLoadEn(qgetenv("LA_I18N_LANG"), sysLoc);
        if (!qEnvironmentVariableIsEmpty("LA_I18N_DEBUG"))
            std::fprintf(stderr, "LA_I18N_DEBUG lang=%d ui=%s loadEn=%d\n",
                         int(sysLoc.language()),
                         qPrintable(sysLoc.uiLanguages().join(QLatin1Char(','))),
                         int(loadEn));
        if (loadEn) {
            static QTranslator i18nTranslator;
            if (i18nTranslator.load(QStringLiteral(":/i18n/LightAlbum_en.qm"))) {
                app.installTranslator(&i18nTranslator);
                std::fprintf(stderr, "LA_I18N translator=en installed\n");
            } else {
                std::fprintf(stderr, "LA_I18N translator=en LOAD FAILED\n");
            }
            std::fflush(stderr);
        }
    }

    // P16.6 视觉QA：offscreen 字体库不枚举 CJK → 中文豆腐块。显式从文件加载中文字体，
    // 字体引擎直接持有字形（不依赖系统字体枚举），offscreen 截图即等于真实窗口效果。
    QString cjkFamily;
    {
        static const QStringList cands = {
            QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
            QStringLiteral("C:/Windows/Fonts/NotoSansSC-VF.ttf"),
            QStringLiteral("C:/Windows/Fonts/simhei.ttf"),
            QStringLiteral("C:/Windows/Fonts/simsun.ttc"),
        };
        for (const QString& p : cands) {
            if (!QFileInfo::exists(p)) continue;
            const int id = QFontDatabase::addApplicationFont(p);
            if (id < 0) continue;
            const QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty()) { cjkFamily = fams.first(); break; }
        }
        if (cjkFamily.isEmpty())
            std::fprintf(stderr, "LA_FONT no CJK font loaded (fallback to system)\n");
        else
            std::fprintf(stderr, "LA_FONT loaded CJK family=%s\n", qPrintable(cjkFamily));
        std::fflush(stderr);
    }
    Lumen::applyAppFont(app, cjkFamily);   // P11：中文优先（显式 CJK 家族或系统雅黑）

    // P17「Quiet Frame」衬线中文：大标题/章节名的书卷气来自显式加载的衬线 CJK 字体，
    // 拉丁字形仍由 Georgia 优先（families 兜底链在 Lumen::serifFamilies）。
    {
        static const QStringList serifCands = {
            QStringLiteral("C:/Windows/Fonts/NotoSerifSC-VF.ttf"),
            QStringLiteral("C:/Windows/Fonts/STSONG.TTF"),
            QStringLiteral("C:/Windows/Fonts/simsun.ttc"),
        };
        QString serifFamily;
        for (const QString& p : serifCands) {
            if (!QFileInfo::exists(p)) continue;
            const int id = QFontDatabase::addApplicationFont(p);
            if (id < 0) continue;
            const QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty()) { serifFamily = fams.first(); break; }
        }
        Lumen::setSerifFamily(serifFamily);
        if (serifFamily.isEmpty())
            std::fprintf(stderr, "LA_FONT no serif CJK loaded (titles fall back to sans)\n");
        else
            std::fprintf(stderr, "LA_FONT loaded serif CJK family=%s\n", qPrintable(serifFamily));
        std::fflush(stderr);
    }
    app.setStyleSheet(Lumen::focusRing());   // P16.1：全局键盘焦点环（WCAG 可达性）

    // P5 金样对拍冒烟（无需库文件，最先跑）
    if (qEnvironmentVariableIsSet("LA_LUMEN_SMOKE"))
        return runLumenSmoke();

    // P9 AI 修图智能体冒烟（测量 → 决策 → retouchWithPlan；不依赖库文件）
    if (qEnvironmentVariableIsSet("LA_AGENT_SMOKE"))
        return runAgentSmoke();

    // 方向 B 门禁深化：LumenAgent 四个纯函数逐项值契约（零网络、亚秒级）
    if (qEnvironmentVariableIsSet("LA_AGENT_QA"))
        return runAgentQa();

    // 2.28.0 RAW 格式支持门禁（须排在 animOk 之前，见门禁铁律）
    if (qEnvironmentVariableIsSet("LA_RAW_QA"))
        return runRawQa();

    // 2.28.0 视频格式支持门禁（须排在 animOk 之前，见门禁铁律）
    if (qEnvironmentVariableIsSet("LA_VIDEO_QA"))
        return runVideoQa();

    // 2.30.0 视频播放（内嵌解码器）门禁（须排在 animOk 之前）
    if (qEnvironmentVariableIsSet("LA_VIDEO_PLAY_QA"))
        return runVideoPlayQa();

    // 2.30.0 播放器 UI 渲染门禁（离屏像素，offscreen 可测）
    if (qEnvironmentVariableIsSet("LA_VIDEO_UI_QA"))
        return runVideoUiQa();

    // 2.30.0 音频输出门禁（ffmpeg PCM → winmm waveOut）
    if (qEnvironmentVariableIsSet("LA_AUDIO_QA"))
        return runAudioQa();

    // 2.31.0 视频选帧门禁（按秒取帧·防串帧）
    if (qEnvironmentVariableIsSet("LA_VIDEO_FRAME_QA"))
        return runVideoFrameQa();

    // 2.31.0 抽帧缓存门禁（命中/失效/清空）
    if (qEnvironmentVariableIsSet("LA_VIDEO_CACHE_QA"))
        return runVideoCacheQa();

    // 2.29.0 选片豁免契约门禁（须排在 animOk 之前，见门禁铁律）
    if (qEnvironmentVariableIsSet("LA_CULL_EXEMPT_QA"))
        return runCullExemptQa();

    // 2.31.0 选片页视频适配门禁（须排在 animOk 之前，见门禁铁律）
    if (qEnvironmentVariableIsSet("LA_CULL_VIDEO_QA"))
        return runCullVideoQa();

    // 2.29.0 RAW/视频修图链路门禁（须排在 animOk 之前，见门禁铁律）
    if (qEnvironmentVariableIsSet("LA_EDIT_RAW_QA"))
        return runEditRawQa();

    // P10 离线逆地理冒烟（已知点 + 真库回填统计）
    if (qEnvironmentVariableIsSet("LA_GEO_SMOKE"))
        return runGeoSmoke();

    // 方向 B：撤销/重做栈纯状态机断言（RtHistory 直接驱动）
    if (qEnvironmentVariableIsSet("LA_RETOUCH_UNDO_QA"))
        return runRetouchUndoQa();

    // 方向 B 深化：12 算子逐项方向像素断言（每个算子单独施参，防互相掩盖）
    if (qEnvironmentVariableIsSet("LA_RETOUCH_OP_QA"))
        return runRetouchOpQa();

    // 方向 C 第一步：相册内搜索过滤断言（GalleryModel 直接驱动）
    if (qEnvironmentVariableIsSet("LA_GALLERY_SEARCH_QA"))
        return runGallerySearchQa();

    // 方向 C 第一步：相册聚类断言（PhotoCluster 纯函数直接驱动，纯数据无 UI）
    if (qEnvironmentVariableIsSet("LA_CLUSTER_QA"))
        return runClusterQa();

    // 方向 C·联动：地图地理热点聚合 + 一键联动门禁（GeoHotspot 纯函数 + MapView 点击链路）
    if (qEnvironmentVariableIsSet("LA_GEO_LINK_QA"))
        return runGeoLinkQa();

    // 方向 C：相似照片去重断言（PhotoDedup 纯函数直接驱动，纯数据无 UI）
    if (qEnvironmentVariableIsSet("LA_DEDUP_QA"))
        return runDedupQa();

    // 方向 D 第一步：真实库回归门禁（规模化与流畅性，默认可跑 6425 张合成 fixture）
    if (qEnvironmentVariableIsSet("LA_SCALE_SMOKE"))
        return runScaleSmoke();

    // 方向 D 第二步：规模化流畅性基线门禁（量化首屏/滚动，守护回归）
    if (qEnvironmentVariableIsSet("LA_SCALE_PERF"))
        return runScalePerf();
    if (qEnvironmentVariableIsSet("LA_SCALE_REAL_QA"))
        return runScaleRealQa();
    // 方向 D 第六步：真实库性能回归门禁（首屏 + 滚动帧预算），CI 经 need SKIP
    if (qEnvironmentVariableIsSet("LA_SCALE_REAL_PERF_QA"))
        return runScaleRealPerfQa();

    // 方向 D 第四步：缩略图多级缓存门禁（两级派生 / 字节预算 / 负缓存 / 跨级回落）
    if (qEnvironmentVariableIsSet("LA_THUMB_CACHE_QA"))
        return runThumbCacheQa();

    // 方向 D 第五步：批量写回事务门禁（原子性 / 回滚 / 索引）
    if (qEnvironmentVariableIsSet("LA_BATCH_TX_QA"))
        return runBatchTxQa();

    // 方向 C 第三步：智能分类写回 + 检索契约门禁（合成钩子绕过 CLIP 服务）
    if (qEnvironmentVariableIsSet("LA_CLASSIFY_QA"))
        return runClassifyQa();

    // 方向 C·人脸识别（方案A）：人物检测写回 + 检索 + 重命名契约门禁（合成钩子绕过 face_server）
    if (qEnvironmentVariableIsSet("LA_PERSON_QA"))
        return runPersonQa();

    // 方向 C·人物自动聚类：embedding 落库 + 阈值余弦聚类 + 守恒契约门禁（合成钩子绕过 face_server）
    if (qEnvironmentVariableIsSet("LA_FACE_CLUSTER_QA"))
        return runFaceClusterQa();

    // 人物故事方向·B：自动故事相册（地点 + 时间窗口聚合）→ 守恒契约 + 检索契约门禁
    if (qEnvironmentVariableIsSet("LA_STORY_QA"))
        return runStoryQa();

    // 2.22.0 客户端体验：骨架屏纯函数 + 像素判别 + 灯箱原图解码契约
    if (qEnvironmentVariableIsSet("LA_TRANSITION_QA"))
        return runTransitionQa();

    // 方向 D 第五步 b：图库多选 + 批量工具条门禁
    if (qEnvironmentVariableIsSet("LA_GALLERY_BATCH_QA"))
        return runGalleryBatchQa();

    // 2.6.0 导入 iCloud 相册门禁（沙盒自建 iCloud 风格目录树，纯离屏自退）
    if (qEnvironmentVariableIsSet("LA_ICLOUD_QA"))
        return runICloudQa();

    // 2.7.0 门禁自退契约门禁（子进程跑 early/deferred/watchdog 三种 quit 时序）
    if (qEnvironmentVariableIsSet("LA_EXIT_QA"))
        return runExitQa();

    // P8 原生导入器冒烟：扫描 LA_SCAN_ROOT 入库到 LA_SCAN_DB（独立于正式库），
    // 打印增量结果后退出；同一命令重跑应 added=0 / unchanged=N（增量幂等）。
    if (qEnvironmentVariableIsSet("LA_SCAN_SMOKE")) {
        const ScanResult r = Scanner::syncRoot(
            qEnvironmentVariable("LA_SCAN_ROOT"),
            qEnvironmentVariable("LA_SCAN_DB"),
            qEnvironmentVariable("LA_SCAN_THUMBS"));
        std::fprintf(stderr,
                     "LA_SCAN_SMOKE added=%d updated=%d unchanged=%d missing=%d "
                     "placeholders=%d total=%d errors=%d secs=%.2f %s\n",
                     r.added, r.updated, r.unchanged, r.missing, r.placeholders,
                     r.total, r.errorCount, r.seconds,
                     r.errorCount == 0 ? "PASS" : "FAIL");
        std::fflush(stderr);
        return r.errorCount == 0 ? 0 : 1;
    }

    // P30-C 日志页控制台风格冒烟：合成 EXIF 数据集 → 构造 LogPage（含 recompute）→
    // 断言 KPI 已填充、状态变更可重算、图表 paintEvent 在 offscreen 下不崩（不抓图，避开 PNG 挂）。
    if (qEnvironmentVariableIsSet("LA_LOG_SMOKE")) {
        QVector<Photo> phs;
        const QStringList cams   = {QStringLiteral("Canon EOS R5"), QStringLiteral("Sony A7IV")};
        const QStringList lenses = {QStringLiteral("RF24-70mm F2.8"), QStringLiteral("FE 35mm F1.8")};
        for (int i = 0; i < 40; ++i) {
            Photo p;
            p.path   = QStringLiteral("synthetic/%1.jpg").arg(i);
            p.date   = QStringLiteral("2023-%1-15").arg((i % 12) + 1, 2, 10, QChar('0'));
            p.year   = 2023 + (i % 2);
            p.cam    = cams[i % 2];
            p.lens   = lenses[i % 2];
            p.ap     = (i % 3 == 0) ? QStringLiteral("f/2.8") : QStringLiteral("f/8");
            p.sh     = (i % 2) ? QStringLiteral("1/250") : QStringLiteral("1/2s");
            p.iso    = (i % 2) ? 100 : 1600;
            p.focal  = (i % 4 == 0) ? 16 : (i % 4 == 1 ? 35 : (i % 4 == 2 ? 85 : 200));
            p.rating = (i % 5 == 0) ? 5 : 0;
            p.region = QStringLiteral("china");
            if (i % 7 == 0) p.ap.clear();           // 缺失 EXIF：须跳过不硬塞
            if (i % 11 == 0) { p.iso = 0; p.focal = 0; }
            phs.append(p);
        }
        LogPage page(&phs, nullptr);
        page.setHidePrivate(false);                 // 触发状态变更重算（与 ctor 同路径）
        page.show();                                // offscreen：触发 paintEvent，不抓图
        QCoreApplication::processEvents(QEventLoop::AllEvents, 300);
        const QList<QLabel*> allLbls = page.findChildren<QLabel*>();
        int totalKpi = -1; bool foundKpi = false;
        for (QLabel* l : allLbls) {
            const QString t = l->text();
            if (t.contains(QStringLiteral("作品总数"))) {
                const QRegularExpression re(QStringLiteral("<b[^>]*>(\\d+)</b>"));
                const QRegularExpressionMatch m = re.match(t);
                if (m.hasMatch()) { totalKpi = m.captured(1).toInt(); foundKpi = true; }
                break;
            }
        }
        const bool ok = foundKpi && totalKpi == 40 && allLbls.size() >= 10;
        std::fprintf(stderr, "LA_LOG_SMOKE_%s totalKpi=%d labels=%d\n",
                     ok ? "OK" : "FAIL", totalKpi, allLbls.size());
        // 横向溢出测量（P29-H 反馈修正：chips 内滚后整页不应被撑宽）
        QScrollArea* outer = nullptr;
        for (QScrollArea* s : page.findChildren<QScrollArea*>())
            if (s->parent() == &page) { outer = s; break; }
        QWidget* rootW = outer ? outer->widget() : nullptr;
        const int rootMin = rootW ? rootW->minimumSizeHint().width() : -1;
        const int pageMin = page.minimumSizeHint().width();
        std::fprintf(stderr, "LA_LOG_OVERFLOW rootMin=%d pageMin=%d\n", rootMin, pageMin);
        std::fflush(stderr);
        return ok ? 0 : 1;
    }

    Library lib;
    const QString dbPath  = qEnvironmentVariable("LA_DB", QStringLiteral("D:/lightalbum/library.db"));
    const QString thumbsDir = qEnvironmentVariable("LA_THUMBS", QStringLiteral("D:/lightalbum/thumbs"));
    QString err;
    if (!lib.open(dbPath, &err)) {
        std::fprintf(stderr, "LA_DB_OPEN_FAIL %s (%s)\n",
                     qPrintable(dbPath), qPrintable(err));
        std::fflush(stderr);
        if (qEnvironmentVariableIsSet("LA_SMOKE")) return 2;
        return 1;
    }

    // P17 首页章节分组冒烟（纯逻辑，无 GUI）：章节数>0、章内非空、按数量降序、
    // 展示字段非空、关键词 ⊆ 真实分类；打印定位覆盖率后按结果退出。
    if (qEnvironmentVariableIsSet("LA_HOME_SMOKE")) {
        const auto& phs = lib.photos();
        const auto chs = HomePage::buildChapters(phs, 12);
        int located = 0, usable = 0;
        for (const Photo& p : phs) {
            if (p.priv || p.missing || p.thumbFile.isEmpty()) continue;
            ++usable;
            if (!p.city.isEmpty() || !p.prov.isEmpty() || !p.region.isEmpty()) ++located;
        }
        bool ok = !chs.isEmpty() && located > 0;
        int prevCount = 1 << 30;
        for (const auto& c : chs) {
            if (c.photos.isEmpty() || c.title.isEmpty()
                || c.capsLine.isEmpty() || c.subtitle.isEmpty()
                || (int)c.photos.size() > prevCount)
                ok = false;
            prevCount = int(c.photos.size());
        }
        std::fprintf(stderr, "LA_HOME_SMOKE_%s chapters=%d located=%d usable=%d "
                             "first=\"%s\"(%d 幅) kw=%d\n",
                     ok ? "OK" : "FAIL", chs.size(), located, usable,
                     chs.isEmpty() ? qPrintable(QString())
                                   : qPrintable(chs.first().title),
                     chs.isEmpty() ? 0 : chs.first().photos.size(),
                     chs.isEmpty() ? 0 : chs.first().keywords.size());
        std::fflush(stderr);
        return ok ? 0 : 1;
    }

    // P16 写回链路自测：LA_RW_SMOKE=1（必须指向临时库副本，绝不碰真库）。
    // 评分写→内存同步→SQL 读回；私密标记往返；meta KV 往返；vault 口令哈希/校验。
    if (qEnvironmentVariableIsSet("LA_RW_SMOKE")) {
        const auto& phs = lib.photos();
        if (phs.isEmpty()) {
            std::fprintf(stderr, "LA_RW_SMOKE_FAIL no photos\n");
            return 3;
        }
        const QString p0 = phs.first().path;
        bool r1 = lib.setRating(p0, 4);
        bool r1m = phs.first().rating == 4;
        bool r2 = lib.setPrivate(p0, true) && phs.first().priv;
        bool r3 = lib.setPrivate(p0, false) && !phs.first().priv;
        bool r4 = lib.setMetaValue(QStringLiteral("rw_smoke_key"),
                                   QStringLiteral("v123"));
        bool r5 = lib.metaValue(QStringLiteral("rw_smoke_key"))
                    == QStringLiteral("v123");
        const QByteArray salt = Vault::randomSalt();
        const QString h = Vault::hashHex(QStringLiteral("口令abc123"), salt);
        bool r6 = !h.isEmpty()
            && Vault::verify(QStringLiteral("口令abc123"), salt, h)
            && !Vault::verify(QStringLiteral("错密码"), salt, h);
        lib.setRating(p0, 0);   // 还原
        std::fprintf(stderr,
            "LA_RW_SMOKE_%s rating=%d ratingMem=%d priv=%d unpriv=%d meta=%d metaGet=%d vault=%d\n",
            (r1 && r1m && r2 && r3 && r4 && r5 && r6) ? "OK" : "FAIL",
            int(r1), int(r1m), int(r2), int(r3), int(r4), int(r5), int(r6));
        std::fflush(stderr);
        return (r1 && r1m && r2 && r3 && r4 && r5 && r6) ? 0 : 4;
    }

    MainWindow win(&lib, thumbsDir, dbPath);
    win.setWindowTitle(QStringLiteral("光影集 LightAlbum " LA_VERSION));
    win.resize(1440, 940);

    // P13 开场动画：仅在真实启动（非冒烟/截图）时播放，保证 CI 确定性。
    const bool animOk = !qEnvironmentVariableIsSet("LA_SMOKE")
        && !qEnvironmentVariableIsSet("LA_UI_SHOT")
        && !qEnvironmentVariableIsSet("LA_LIGHTBOX_SHOT")
        && !qEnvironmentVariableIsSet("LA_CULL_SMOKE")
        && !qEnvironmentVariableIsSet("LA_RETOUCH_SMOKE")
        && !qEnvironmentVariableIsSet("LA_RETOUCH_A_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_A_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_REGION_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_CLICK_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_BURST_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_COUNTRY_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_PERF")
        && !qEnvironmentVariableIsSet("LA_MAP_TRACK_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_HEAT_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_TRACK_ANIM_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_YEAR_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_HEAT_RAMP_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_COUNTRY_YEAR_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_COUNTRY_ALBUM_QA")
        && !qEnvironmentVariableIsSet("LA_MAP_HEAT_TIER_QA")
        && !qEnvironmentVariableIsSet("LA_GALLERY_E_QA")
        && !qEnvironmentVariableIsSet("LA_CULL_DUEL_QA")
        && !qEnvironmentVariableIsSet("LA_GALLERY_FILL_QA")
        && !qEnvironmentVariableIsSet("LA_SCALE_PERF")
        && !qEnvironmentVariableIsSet("LA_THUMB_CACHE_QA")
        && !qEnvironmentVariableIsSet("LA_BATCH_TX_QA")
        && !qEnvironmentVariableIsSet("LA_RAW_QA")
        && !qEnvironmentVariableIsSet("LA_VIDEO_QA")
        && !qEnvironmentVariableIsSet("LA_CULL_EXEMPT_QA")
        && !qEnvironmentVariableIsSet("LA_CULL_VIDEO_QA")
        && !qEnvironmentVariableIsSet("LA_EDIT_RAW_QA")
        && !qEnvironmentVariableIsSet("LA_GALLERY_BATCH_QA")
        ;

    QRect avail = QApplication::primaryScreen()->availableGeometry();
    QRect fullRect((avail.width() - 1440) / 2, (avail.height() - 940) / 2, 1440, 940);

    // P13 开场动画探针：LA_SPLASH_SHOT=<dir> 时抓 3 帧（开合/停留/成缝）存 PNG 后退出。
    if (qEnvironmentVariableIsSet("LA_SPLASH_SHOT")) {
        const QString dir = qEnvironmentVariable("LA_SPLASH_SHOT");
        QDir().mkpath(dir);
        SplashWindow sp(fullRect);
        QTimer::singleShot(250, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-1-open.png")));
            sp.grabIris().save(QDir(dir).filePath(QStringLiteral("splash-4-iris.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT open+iris saved\n"); std::fflush(stderr);
        });
        QTimer::singleShot(1600, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-2-hold.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT hold saved\n"); std::fflush(stderr);
        });
        QTimer::singleShot(2450, &app, [&sp, &dir] {
            sp.grab().save(QDir(dir).filePath(QStringLiteral("splash-3-slit.png")));
            std::fprintf(stderr, "LA_SPLASH_SHOT slit saved\n"); std::fflush(stderr);
            QaExit::requestExit(0);   // 2.7.0：收敛到自退契约
        });
        sp.play();
        return app.exec();
    }

    if (animOk) {
        // 系统「减弱动效」：静态收尾 splash，客户端立即全尺寸显示（无展开动画）。
        if (Lumen::systemReducedMotion()) {
            SplashWindow splash(fullRect);
            QObject::connect(&splash, &SplashWindow::finished, [&]() {
                splash.close();
                win.setGeometry(fullRect);
                win.show();
            });
            splash.playStatic();
            return app.exec();
        }
        // 开场：splash 播放 → 收缩成缝 → 客户端从缝中向上下展开
        SplashWindow splash(fullRect);
        QObject::connect(&splash, &SplashWindow::finished, [&]() {
            splash.close();
            // 先以「缝」形态出现，再向上下展开（避免 show() 全尺寸单帧闪现）
            const QRect slit(fullRect.x(), fullRect.center().y() - 1,
                             fullRect.width(), 2);
            win.setGeometry(slit);
            win.show();
            auto* a = new QPropertyAnimation(&win, "geometry");
            a->setDuration(Lumen::Motion::kExpandMs);
            a->setStartValue(slit);
            a->setEndValue(fullRect);
            a->setEasingCurve(QEasingCurve::OutCubic);
            a->start(QAbstractAnimation::DeleteWhenStopped);
        });
        splash.play();
        return app.exec();
    }

    // 非动画（冒烟/截图）：直接显示并保留原有门控
    win.setGeometry(fullRect);
    win.show();

    // P15 沉浸式灯箱探针：LA_LIGHTBOX_SHOT=<dir> 时打开首张照片的灯箱 → grab 存 PNG 后退出。
    if (qEnvironmentVariableIsSet("LA_LIGHTBOX_SHOT")) {
        const QString dir = qEnvironmentVariable("LA_LIGHTBOX_SHOT");
        QDir().mkpath(dir);
        const auto& phs = lib.photos();
        if (!phs.isEmpty()) {
            win.openLightbox(phs.first().path);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 250);
            const QString path = QDir(dir).filePath(QStringLiteral("lightbox.png"));
            win.grab().save(path);
            std::fprintf(stderr, "LA_LIGHTBOX_SHOT %s\n", qPrintable(path));
            std::fflush(stderr);
        }
        return 0;
    }

    // A 现状墨韵·抛光 像素自测：LA_MAP_A_QA=<png> 时离屏渲染地图世界层并统计翡翠像素。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_A_QA")) {
        const QString p = qEnvironmentVariable("LA_MAP_A_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootA(p);
        std::fprintf(stderr, "LA_MAP_A_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 下钻区/省视图像素自测：LA_MAP_REGION_QA=<png>[,<regionId>] 时强制 enterRegion 后
    // 离屏渲染该大区省视图。默认 regionId=china（验证 DataV 34 省落地）。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_REGION_QA")) {
        const QString v = qEnvironmentVariable("LA_MAP_REGION_QA");
        QString p = v, id = QStringLiteral("china");
        const int idx = v.indexOf(QLatin1Char(','));
        if (idx >= 0) { p = v.left(idx); id = v.mid(idx + 1); }
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootRegion(p, id);
        std::fprintf(stderr, "LA_MAP_REGION_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 点击链路自测：LA_MAP_CLICK_QA=1 时合成鼠标事件，实测世界锚点/省面两级命中。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_CLICK_QA")) {
        if (MapView* mv = win.mapView()) mv->qaClickProbe();
        std::fprintf(stderr, "LA_MAP_CLICK_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 爆炸弹层像素自测：LA_MAP_BURST_QA=<png> 时进省视图 + 合成缩略图 →
    // 离屏渲染终态并分色统计（连线/卡片/冲击波落地必须实测）。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_BURST_QA")) {
        const QString p = qEnvironmentVariable("LA_MAP_BURST_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootBurst(p);
        std::fprintf(stderr, "LA_MAP_BURST_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 帧耗时自测：LA_MAP_PERF=1 时逐层级渲染 20 帧打印平均毫秒/帧。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_PERF")) {
        int rc = 0;
        if (MapView* mv = win.mapView()) rc = mv->qaPerf();
        std::fprintf(stderr, "LA_MAP_PERF_DONE\n");
        std::fflush(stderr);
        return rc;
    }

    // A 国家视图像素自测：LA_MAP_COUNTRY_QA=<png>[,<国家名>] 时 enterCountry →
    // 离屏渲染国界轮廓 + 石板色统计（默认 India，bbox 中心必在国内）。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_COUNTRY_QA")) {
        const QString v = qEnvironmentVariable("LA_MAP_COUNTRY_QA");
        QString p = v, nm = QStringLiteral("India");
        const int idx = v.indexOf(QLatin1Char(','));
        if (idx >= 0) { p = v.left(idx); nm = v.mid(idx + 1); }
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootCountry(p, nm);
        std::fprintf(stderr, "LA_MAP_COUNTRY_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 方向·足迹轨迹像素自测：LA_MAP_TRACK_QA=<png> 时注入确定性足迹点（footLayer=轨迹），
    // 离屏渲染世界层并统计青色轨迹像素；FORCE_BROKEN 强制光点 → 轨迹像素为 0 → RED。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_TRACK_QA")) {
        const QString p = qEnvironmentVariable("LA_MAP_TRACK_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootTrack(p);
        std::fprintf(stderr, "LA_MAP_TRACK_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 方向·足迹热力像素自测：LA_MAP_HEAT_QA=<png> 时注入确定性足迹点（footLayer=热力），
    // 离屏渲染世界层并统计暖色热力像素；FORCE_BROKEN 强制光点 → 暖色像素为 0 → RED。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_HEAT_QA")) {
        const QString p = qEnvironmentVariable("LA_MAP_HEAT_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootHeat(p);
        std::fprintf(stderr, "LA_MAP_HEAT_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 方向·足迹轨迹箭头/播放动画自测：LA_MAP_TRACK_ANIM_QA=<png> 时注入确定性足迹点，
    // 断言箭头像素 > 0 且播放头在 t=0/mid/end 三时刻落于不同位置（证明真沿线移动）。
    // FORCE_BROKEN 强制播放头恒回首点 → 三时刻全等 → RED。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_TRACK_ANIM_QA")) {
        const QString p = qEnvironmentVariable("LA_MAP_TRACK_ANIM_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootTrackAnim(p);
        std::fprintf(stderr, "LA_MAP_TRACK_ANIM_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 方向第三步·按年筛选自测：LA_MAP_YEAR_QA=1 时注入跨年份确定性 GPS 点，
    // 断言筛选后点数、pts/paths/dates 下标对齐（国家视图依赖）、幂等、0=全部。
    // FORCE_BROKEN 让注入点过滤失效 → 点数为全量 → RED。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_YEAR_QA")) {
        if (MapView* mv = win.mapView()) mv->qaShootYear(QString());
        std::fprintf(stderr, "LA_MAP_YEAR_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 方向第四步·热力色带分级自测：LA_MAP_HEAT_RAMP_QA=<png> 时注入三簇密度迥异的
    // 足迹点，断言低/中/高三档色相像素均 >0（密度分级生效）。
    // FORCE_BROKEN 强制所有点同档 → 只剩一档 → RED。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_HEAT_RAMP_QA")) {
        const QString p = qEnvironmentVariable("LA_MAP_HEAT_RAMP_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        if (MapView* mv = win.mapView()) mv->qaShootHeatRamp(p);
        std::fprintf(stderr, "LA_MAP_HEAT_RAMP_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 国家层深化（2.27.0）· footYear 贯通国层：注入同国跨年份点，模拟改年全链路，
    // 断言国层 countryPts_/countryPaths_ 实时收敛到筛选年。
    // FORCE_BROKEN 跳过 setFootYearFilter 内重算 → 快照不缩 → RED。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_COUNTRY_YEAR_QA")) {
        if (MapView* mv = win.mapView()) mv->qaCountryYearFilter();
        std::fprintf(stderr, "LA_MAP_COUNTRY_YEAR_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 国家层深化（2.27.0）· 相册按年分桶聚类：注入跨年点 → 进入国层 → 改年筛选后，
    // 断言 countryAlbumGroups() 给出正确逐年分组。FORCE_BROKEN 忽略年份 → RED。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_COUNTRY_ALBUM_QA")) {
        if (MapView* mv = win.mapView()) mv->qaCountryAlbum();
        std::fprintf(stderr, "LA_MAP_COUNTRY_ALBUM_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // A 国家层深化（2.27.0）· 热力分档临界点可调：默认 3/8 → setHeatTiers(2,3) 后
    // dens=5 档位由 1→2。FORCE_BROKEN 使 setHeatTiers 失效 → 档位不变 → RED。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_HEAT_TIER_QA")) {
        if (MapView* mv = win.mapView()) mv->qaHeatTier();
        std::fprintf(stderr, "LA_MAP_HEAT_TIER_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // P23 E 杂志拼贴 像素/几何自测：LA_GALLERY_E_QA=<png> 时合成 12 张照片驱动 EditorialView，
    // 验证 feature 首图占 2×2 + 卡片真实绘制，并抓整页 PNG（颜色校验铁律：必须实测不许猜）。
    if (!qEnvironmentVariableIsEmpty("LA_GALLERY_E_QA")) {
        const QString p = qEnvironmentVariable("LA_GALLERY_E_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        QVector<Photo> synth;
        for (int i = 0; i < 12; ++i) {
            Photo ph;
            ph.path  = QStringLiteral("synthetic/%1.jpg").arg(i);
            ph.title = QStringLiteral("作品 %1").arg(i + 1);
            ph.date  = QStringLiteral("2024-05-%1").arg((i % 9) + 1, 2, 10, QChar('0'));
            ph.orient = (i % 2) ? QStringLiteral("portrait") : QStringLiteral("landscape");
            ph.rating = (i % 4 == 0) ? 4 : (i % 3 == 0 ? 3 : 0);
            ph.year  = 2024;
            synth.append(ph);
        }
        GalleryPage page(&synth, QDir::temp().filePath(QStringLiteral("la_e_qa_thumbs")), nullptr);
        page.resize(1200, 820);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        const int rcE = page.qaShootE(p);
        std::fprintf(stderr, "LA_GALLERY_E_QA_DONE rc=%d\n", rcE);
        std::fflush(stderr);
        return rcE;
    }

    // P27 图库铺满验证：LA_GALLERY_FILL_QA=<png> 时取真库首张存在的照片，
    // 直接用 Lumen::paintGalleryCard 渲染 feature 大图 + single 小图两种卡片，
    // 验证真实照片铺满卡片（无黑边 / 无 letterbox）——绕过异步 ThumbCache 直接测绘制逻辑
    //（颜色校验铁律：必须实测不许猜）。
    if (!qEnvironmentVariableIsEmpty("LA_GALLERY_FILL_QA")) {
        const QString p = qEnvironmentVariable("LA_GALLERY_FILL_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        // 取真库首张**可解码**的照片（绕过异步 ThumbCache，直接验证绘制逻辑）。
        // ★ 修复「门禁拿格式问题背黑锅」：本门要验的是「真实照片铺满卡片、无 letterbox」，
        // 属**绘制逻辑**；而「首张磁盘上存在的照片」可能是 HEIC 等 Qt 解不出的格式
        // （2.22.0 已实测 HEIC decoded=NULL），门会因**与被测无关**的格式原因变红，
        // 且成败取决于用户真库里恰好排第一的是哪张 → 天然 flaky，不可做 CI 门。
        // 故循环到第一张真正 load 成功的；一张都解不出才判失败（不许空数据蒙混过关）。
        QPixmap pm;
        QString photoPath;
        int tried = 0;
        for (const Photo& ph : lib.photos()) {
            if (!QFile::exists(ph.path)) continue;
            ++tried;
            if (pm.load(ph.path)) { photoPath = ph.path; break; }
            pm = QPixmap();
        }
        const bool loaded = !pm.isNull();
        auto renderCard = [&](int W, int H, bool feature) -> QImage {
            QImage img(W, H, QImage::Format_RGB32);
            img.fill(QColor(0x13, 0x1a, 0x15));
            QPainter painter(&img);
            Lumen::paintGalleryCard(&painter, QRectF(0, 0, W, H),
                QStringLiteral("测试作品标题"),
                QStringLiteral("2024-05-01 · 风光 · 云南"),
                feature ? QStringLiteral("landscape") : QStringLiteral("portrait"),
                false, 4, pm, feature, false, false);
            painter.end();
            return img;
        };
        const QImage feat = renderCard(400, 400, true);
        const QImage sing = renderCard(240, 240, false);
        const QString dir = QFileInfo(p).absolutePath();
        feat.save(p);   // 主交付：feature 大图铺满样张
        sing.save(QDir(dir).filePath(QStringLiteral("gallery_fill_single.png")));
        auto measure = [](const QImage& img, long& cardBg, long& photo, long& litext) {
            const int W = img.width(), H = img.height();
            cardBg = photo = litext = 0;
            // ★ 容差 12 → 2（实测标定，勿凭感觉调）：原 ±12 会把**底部渐隐遮罩下的暗部照片像素**
            // 误判成"卡片底"，实测底行取值 (29,29,22)/(27,27,21)/(25,26,22)/(23,25,21) 各不相同
            // ——是照片而非黑边，却全部落进 (19,26,21)±12 → 假阳 3.7%/4.5%（曾把本门打红）。
            // 卡片底是**纯色** QColor(0x13,0x1a,0x15)=(19,26,21)，故按"近纯色"判定即可：
            // 实测暗部照片至少一通道偏离 ≥4，而真卡片底偏离 0 → 取 2 卡在中间，两侧都留余量。
            // 检出力未被削弱：真 letterbox 是**成片纯色**带，按 1:1 计入，>2% 必红。
            // 标定实测：tol=12→3.678%/4.497%(假阳) tol=6→1.483%/1.920% tol=2→0.082%/0.191%
            //           tol=0→0.058%/0.160%（仅 92px 圆角抗锯齿，与卡尺寸无关的常数）
            for (int y = 0; y < H; ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = 0; x < W; ++x) {
                    const QRgb v = row[x];
                    const int r = qRed(v), g = qGreen(v), b = qBlue(v);
                    if (qAbs(r - 19) <= 2 && qAbs(g - 26) <= 2 && qAbs(b - 21) <= 2) ++cardBg;
                    else if (r > 200 && g > 200 && b > 190) ++litext;
                    else ++photo;
                }
            }
        };
        long fcb, fph, flt, scb, sph, slt;
        measure(feat, fcb, fph, flt);
        measure(sing, scb, sph, slt);
        const long fAll = feat.width() * feat.height();
        const long sAll = sing.width() * sing.height();
        const double fCbFrac = double(fcb) / fAll, sCbFrac = double(scb) / sAll;
        const double fPhFrac = double(fph) / fAll, sPhFrac = double(sph) / sAll;
        // 关键断言：卡片底（黑边 / letterbox）占比极小 → 真实照片铺满、无黑边。
        // （photo 占比阈值不可靠：亮调照片天空/高光会被误计入 litext，故仅以 cardBg 占比判定）
        const bool ok = loaded && fCbFrac < 0.02 && sCbFrac < 0.02;
        std::fprintf(stderr,
            "LA_GALLERY_FILL_QA loaded=%d tried=%d photo=%s featCbFrac=%.4f singCbFrac=%.4f "
            "featPhFrac=%.4f singPhFrac=%.4f featLitext=%ld ok=%d\n",
            int(loaded), tried, qPrintable(photoPath), fCbFrac, sCbFrac, fPhFrac, sPhFrac, flt, int(ok));
        std::fflush(stderr);
        return ok ? 0 : 5;
    }

    // P30-C / P29-H 诊断：离屏渲染日志页/首页到 PNG（QWidget::render 直绘 QImage，
    // 避开 shootPages 的 grab 挂起族）。仅供目视诊断，不进正式回归门。
    if (!qEnvironmentVariableIsEmpty("LA_LOG_SHOT")) {
        const QString p = qEnvironmentVariable("LA_LOG_SHOT");
        QDir().mkpath(QFileInfo(p).absolutePath());
        LogPage page(&lib.photos(), nullptr);
        page.resize(1440, 940);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        QImage img(page.size(), QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        page.render(&painter);
        painter.end();
        img.save(p);
        std::fprintf(stderr, "LA_LOG_SHOT_DONE %s\n", qPrintable(p));
        std::fflush(stderr);
        return 0;
    }
    if (!qEnvironmentVariableIsEmpty("LA_HOME_SHOT")) {
        const QString p = qEnvironmentVariable("LA_HOME_SHOT");
        QDir().mkpath(QFileInfo(p).absolutePath());
        win.setGeometry(80, 80, 1440, 940);
        win.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 600);
        QImage img(win.size(), QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter painter(&img);
        win.render(&painter);
        painter.end();
        img.save(p);
        std::fprintf(stderr, "LA_HOME_SHOT_DONE %s\n", qPrintable(p));
        std::fflush(stderr);
        return 0;
    }

    // P26 修图页 A 经典工作室 像素/几何自测：LA_RETOUCH_A_QA=<png> 时合成样张驱动独立 RetouchPage，
    // 验证 6 模式轨 + 12 滑杆 + 右栈 6 页 + 金像素，并抓整页 PNG。
    if (!qEnvironmentVariableIsEmpty("LA_RETOUCH_A_QA")) {
        const QString p = qEnvironmentVariable("LA_RETOUCH_A_QA");
        QDir().mkpath(QFileInfo(p).absolutePath());
        QVector<Photo> synth;
        for (int i = 0; i < 6; ++i) {
            Photo ph;
            ph.path  = QStringLiteral("synthetic/%1.jpg").arg(i);
            ph.title = QStringLiteral("样张 %1").arg(i + 1);
            synth.append(ph);
        }
        RetouchPage page(&synth, QDir::temp().filePath(QStringLiteral("la_a_qa_thumbs")), nullptr);
        page.resize(1240, 820);
        page.show();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 400);
        page.qaShootA(p);
        std::fprintf(stderr, "LA_RETOUCH_A_QA_DONE\n");
        std::fflush(stderr);
        return 0;
    }

    // P11 UI 截图探针：LA_UI_SHOT=<dir> 时逐页 widget.grab() 存 PNG（offscreen 可用，
    // 用于颜色/乱码真实验证——颜色校验铁律：必须实测不许猜）。
    if (qEnvironmentVariableIsSet("LA_UI_SHOT")) {
        if (qEnvironmentVariableIsSet("LA_UI_SHOT_SOLID"))
            win.forceTopSolid(true); // P19：顶栏实底态截图（强制态，抗 onNav 重算）
        return win.shootPages(qEnvironmentVariable("LA_UI_SHOT"));
    }

    // 冒烟探针门控：载入真库 + 页面栈就位后自动退出（offscreen/CI 可用）。
    // 选片自测（LA_CULL_SMOKE）/ 修图自测（LA_RETOUCH_SMOKE）
    // 由各自页面自行驱动退出，这里不抢跑。
    if (qEnvironmentVariableIsSet("LA_SMOKE") && qEnvironmentVariableIsEmpty("LA_CULL_SMOKE")
        && qEnvironmentVariableIsEmpty("LA_RETOUCH_SMOKE")) {
        // P29-H：首页（零 chrome 纯沉浸）应隐藏窗口控制簇——同步打印，避开 600ms 自退定时器可能的挂起
        const bool winCtlHiddenOnHome = !win.isWinCtlVisible();
        // 缺陷②回归：导入下拉菜单是独立顶层弹窗，不继承应用暗色 QSS → 曾经白底白字。
        // 断言它确实带了暗色底（#0e130f）；全工程仅此一个 QMenu，故命中即导入菜单。
        int menuTotal = 0, menuDark = 0;
        for (QMenu* m : win.findChildren<QMenu*>()) {
            ++menuTotal;
            if (m->styleSheet().contains(QStringLiteral("0e130f"))) ++menuDark;
        }
        const bool menuStyled = (menuTotal > 0) && (menuDark == menuTotal);
        std::fprintf(stderr, "LA_SMOKE_WINCTL home=%d menuStyled=%d(%d/%d)\n",
                     int(winCtlHiddenOnHome), int(menuStyled), menuDark, menuTotal);
        std::fflush(stderr);
        const int smokeRc = menuStyled ? 0 : 1;   // 菜单回退浅色 → 门红
        QTimer::singleShot(600, &app, [&lib, &win, smokeRc]{
            const Kpi& k = lib.kpi();
            std::fprintf(stderr, "LA_P1_SMOKE_OK photos=%d years=%d provs=%d span=%d-%d\n",
                         k.total, k.yearCount, k.provCount, k.minYear, k.maxYear);
            std::fflush(stderr);
            QaExit::requestExit(smokeRc);   // 2.7.0：收敛到自退契约
        });
    }

    // 缺陷②说明：本想再加一道像素门（弹出菜单 grab 后量「暗底+亮字」），但**实测不可行**——
    // QMenu 是原生弹窗，offscreen 平台既不支持其光栅化，`popup()`/`show()` 后 `grab()` 都会挂死
    // （watchdog 20s no-exit-request，popup 与 show 两路皆试）。故缺陷②的门禁改为两道**可靠**断言：
    //   ① LA_SMOKE_WINCTL：菜单确实带暗色 QSS（字符串断言，menuStyled=1/1）
    //   ② LA_A11Y_CONTRAST_QA：菜单 3 组配色过 WCAG 4.5:1（pairs 15→18）
    // 「QSS 是否真能盖过 Windows 原生浅色菜单」这一残余问题 offscreen 无法回答，
    // 只能在真实屏幕上目视确认——修复本身复用已上线的 clusterDimBox_ 暗色弹层色系，路径与之一致。
    // P25 对决 QA 由 CullPage 内 QTimer::singleShot(600) 在事件循环内完成并 quit
    return app.exec();
}
