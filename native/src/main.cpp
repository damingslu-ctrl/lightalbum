// LightAlbum Native — P1 入口
// 决策依据：docs/adr/ADR-0001-full-cpp-qt-widgets-rewrite.md
// 分期计划：docs/REWRITE-PLAN.md
// 冒烟：LA_SMOKE=1 + QT_QPA_PLATFORM=offscreen → 载入真库后自动退出并打 LA_P1_SMOKE_OK
#include <QApplication>
#include <QCoreApplication>
#include <QThread>                          // 2.33.0 LA_GUI_THREAD_QA：写回线程归属断言
#include <QtConcurrent/QtConcurrentRun>     // 2.33.0 LA_GUI_THREAD_QA：起真实 worker 线程
#include <QFuture>
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
#include <thread>      // 2.40.0 T4a #75 真底座臂：runner 入口必须经非 GUI worker 线程（spec §2.2）
#include <atomic>      // 2.40.0 T4a #75 G7 并发窗口在飞标志
#include <cstring>                      // R3-T1 DET 决定性子断言：std::memcmp 逐字节比对
#include <algorithm>          // 2.35.0 std::reverse / std::rotate（O 组顺序不变性排列）
#include "core/Library.h"
#include "core/LumenEngine.h"
#include "core/LumenAgent.h"
#include "core/GeoIndex.h"
#include "core/PhotoCluster.h"
#include "core/PhotoDedup.h"
#include "core/BrowseScope.h"   // 2.33.0 灯箱翻图作用域纯函数（LA_PRIVACY_QA 直调）
#include "core/GeoHotspot.h"     // 方向 C·联动：地理热点聚合（与渲染/筛选同源）
#include "core/Classifier.h"   // 方向 C·智能分类：CLIP 客户端桥（QA 门用合成钩子）
#include "core/PersonDetector.h"  // 方向 C·人脸识别（方案A）：faceinfer 子进程编排层（QA 门用合成钩子）
#include "core/AiWriter.h"     // 单写者投递器：门在主线程，写回经 QueuedConnection + processEvents 落地
#include "core/FaceCluster.h"     // 方向 C·人物自动聚类：阈值余弦聚类纯函数（QA 门直调）
#include "core/StoryGen.h"        // 人物故事方向·B：自动故事相册纯函数（LA_STORY_QA 直调）
#include "ui/GalleryCardPainter.h"  // 2.22.0 骨架屏纯函数（QA 门直调，铁律 20 同源）
#include "ui/FullCache.h"           // 2.22.0 灯箱原图 LRU（QA 门直调）
#include "core/MotionKit.h"         // 2.34.0 动效引擎纯函数（LA_MOTION_QA 直调，铁律 20 同源）
#include "core/MotionDriver.h"      // 2.34.0 引擎 Qt 驱动器（LA_MOTION_QA 直调）
#include "core/EraseBridge.h"   // 2.35.0 AI 消除契约门（LA_ERASE_QA 直调）
#include "core/EraseRunner.h"   // 2.35.0 执行器 QA 钩子（qaInjectBadSize）
#include "core/AiProto.h"      // 2.40.0 第四轮 T4a：#75 G5p 全词比对回归腿直调 parseReq
#include "core/AiSubprocessRunner.h"  // 2.40.0 第四轮 T4a：#75 G3/G6/G7/G8 经真底座断言
#include "core/FaceRunner.h"          // 2.40.0 T12 修复波第 2 轮：#75 G9 两轮同进程的换血钩子（qaRetireChildForQa，EraseRunner QA 钩子先例）
#include "core/ExifReader.h"    // 2.38.0 R2 EXIF 容器门（LA_EXIF_CONTAINER_QA 直调）
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
#include "ui/DedupDialog.h"   // 2.33.0 隐私门：去重对话框私密照隔离（LA_DEDUP_PRIVACY_QA 直调）
#include "ui/VideoLightbox.h" // T11：LA_UI_SHOT_POPUP 弹层打开态取证
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

// 2.38.0 R1：一键增强接线门（fast 层 exe 门；E5/E9d 禁止 ps1 入 fast 层，spec §2.4 层级意图保留）。
// 断言 src/ui/ 对 13 个 LumenEngine 显式算子各 >=1 处真实调用（先剥注释再匹配），
// 且 RetouchPage.cpp 含事件接线（opBtn_ / onEnhOpClicked 同时出现）。
// F1 fail-open 守卫：扫描根找不到或 0 文件 -> 直接 FAIL（绝不静默放行，铁律34）。
// 反例：LA_LUMEN_OPS_UI_QA_FORCE_BROKEN=1 -> 期望表冻结为 13 算子、绝不随开关变（铁律22），
//        仅抑制覆盖统计 -> covered=0/13、分母不变 -> 必红（证明逐算子断言真的咬合，铁律34）。
int runLumenOpsUiQa()
{
    const QStringList kOps = {
        QStringLiteral("filmStyle"),     QStringLiteral("blackWhite"),   QStringLiteral("glowStyle"),
        QStringLiteral("redeyeRemove"),  QStringLiteral("vignetteCorrect"), QStringLiteral("toneMapHdr"),
        QStringLiteral("structure"),     QStringLiteral("dodge"),        QStringLiteral("dehaze"),
        QStringLiteral("chromaticCorrect"), QStringLiteral("portraitBeautify"),
        QStringLiteral("levelCorrect"),  QStringLiteral("lensCorrect"),
    };
    const QStringList expected = kOps;               // 期望值恒为规格 13 算子，绝不随反例开关变化（铁律22）
    const bool broken = !qEnvironmentVariableIsEmpty("LA_LUMEN_OPS_UI_QA_FORCE_BROKEN");

    QString root;
    for (const QString& cand : { QCoreApplication::applicationDirPath() + QStringLiteral("/../src/ui"),
                                 QCoreApplication::applicationDirPath() + QStringLiteral("/src/ui"),
                                 QStringLiteral("src/ui") }) {
        if (QFileInfo(cand).isDir()) { root = cand; break; }
    }
    int files = 0;
    QSet<QString> covered;
    bool wiring = false;
    if (!root.isEmpty()) {
        const QFileInfoList list = QDir(root).entryInfoList(
            { QStringLiteral("*.cpp"), QStringLiteral("*.h") }, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : list) {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;
            QString text = QString::fromUtf8(f.readAll());
            ++files;
            text.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"),
                QRegularExpression::DotMatchesEverythingOption));      // 剥块注释
            text.remove(QRegularExpression(QStringLiteral("//[^\\n]*"))); // 剥行注释
            for (const QString& op : expected)
                if (!broken && text.contains(QStringLiteral("LumenEngine::") + op + QLatin1Char('(')))
                    covered.insert(op);              // 反例臂：不记覆盖→13 算子全落 missing→必红（期望值不变）
            if (fi.fileName() == QLatin1String("RetouchPage.cpp")
                && text.contains(QStringLiteral("opBtn_"))
                && text.contains(QStringLiteral("onEnhOpClicked")))
                wiring = true;
        }
    }
    QStringList missing;
    for (const QString& op : expected) if (!covered.contains(op)) missing << op;
    const bool ok = files > 0 && wiring && missing.isEmpty();
    std::fprintf(stderr, "LA_LUMEN_OPS_UI_QA %s covered=%d/%d files=%d wiring=%d missing=[%s]\n",
                 ok ? "PASS" : "FAIL", covered.size(), expected.size(), files, wiring ? 1 : 0,
                 qPrintable(missing.join(QStringLiteral(","))));
    std::fflush(stderr);
    return ok ? 0 : 1;
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
//   H 两阶段契约（2.33.0）：describe()+group() 拆分后与 findGroups 等价，
//     进度逐张上报、缺图诚实标 ok=false、取消即刻停下
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

    // H 两阶段拆分（2.33.0）：describe() 可进 worker 线程，group() 留在 GUI 线程取译文。
    //   拆开后必须与一体式 findGroups 完全一致（同组数、同保留者、同重复集）。
    //   断言只比 path/pixels，不比 reason 文案（铁律22：断言不依赖被测开关的语言环境）。
    int progCalls = 0, progLast = -1;
    const QVector<DedupDesc> descs = PhotoDedup::describe(
        phs, dir, true, [&](int d) { ++progCalls; progLast = d; });
    chk(progCalls == phs.size(), "progCalls");                  // 每张回调一次，不多不少
    chk(progLast == phs.size(), "progLast");                    // 末次进度 = 总张数
    chk(descs.size() == phs.size(), "descSize");
    bool missingHonest = false;
    for (const DedupDesc& d : descs)
        if (d.path == d1p)
            missingHonest = !d.ok && d.phash == 0 && d.md5.isEmpty() && d.pixels == 1000000;
    chk(missingHonest, "descMissingHonest");                    // 缺图不得伪造特征
    const QVector<DupGroup> split = PhotoDedup::group(descs, 8);
    bool same = (split.size() == groups.size());
    for (int i = 0; same && i < split.size(); ++i)
        same = split[i].keptPath == groups[i].keptPath
            && split[i].dupPaths == groups[i].dupPaths
            && split[i].keptPixels == groups[i].keptPixels;
    chk(same, "splitEqualsFused");
    //   取消即刻停下：残缺结果不可继续用于分组（否则半个数据集就能"发现"重复）
    const QVector<DedupDesc> cancelled = PhotoDedup::describe(
        phs, dir, true, nullptr, [] { return true; });
    chk(cancelled.isEmpty(), "cancelStops");

    PhotoDedup::setForceDoubleCountForQa(false);   // 还原（探针不污染环境）

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr, "LA_DEDUP_QA %s marks=%d checks=%d groups=%d dups=%d descs=%d broken=%d\n",
                 pass ? "PASS" : "FAIL", marks, checks, groups.size(), dupTotal,
                 descs.size(), int(broken));
    if (!pass)
        std::fprintf(stderr, "LA_DEDUP_QA failed: %s\n", qPrintable(failed.join(',')));
    std::fflush(stderr);
    return pass ? 0 : 1;
}

// 2.33.0 隐私门门禁（LA_PRIVACY_QA）：vault 锁态必须贯穿三个此前漏网的入口。
//   A 灯箱翻图（BrowseScope::nextVisible 纯函数，与 PhotoLightbox::navigate 同源）
//   B 选片页（CullModel/CullProxy：chips 计数、锁定区、结果网格、标记列表）
//   C 相似照片去重对话框（DedupDialog：私密照必须在「注入点」就不进分析输入集，
//     否则不分组、不渲染、还会被「标记其余为待删」误伤）
// ★每条都成对断言「锁定态排除 + 解锁态照常」：只断言前者会退化成人人皆空的假绿
//   （铁律34：不可失败的断言即缺陷）。正因这对角断言互相制衡，A/B 两段无需注入钩子；
//   C 段另备真缺陷钩子，反例 LA_PRIVACY_QA_FORCE_BROKEN=1 令注入点过滤失效 → RED。
// C 段同时守护去重后台化改造：分析必须能在超时内落地（qaWaitFinished 为真）。
int runPrivacyQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_PRIVACY_QA_FORCE_BROKEN");
    DedupDialog::setForcePrivacyLeakForQa(broken);

    int marks = 0, checks = 0;
    QStringList failed;
    auto chk = [&](bool ok, const char* tag = "") {
        ++checks; if (ok) ++marks; else if (tag && *tag) failed << QString::fromLatin1(tag);
    };

    // ================= A 灯箱翻图：作用域 + 锁态 =================
    {
        auto slot = [](const char* p, bool priv) {
            BrowseSlot s; s.path = QString::fromLatin1(p); s.priv = priv; return s;
        };
        // 公开/私密交替，任何「忽略锁态」或「把集合掏空」的实现都会在这里露馅
        const QVector<BrowseSlot> seq = {
            slot("p0", false), slot("v1", true), slot("p2", false),
            slot("v3", true),  slot("p4", false) };
        chk(BrowseScope::nextVisible(seq, 0, +1, true) == 2, "A1_lockedFwdSkipsPriv");
        chk(BrowseScope::nextVisible(seq, 2, +1, true) == 4, "A2_lockedFwdAgain");
        chk(BrowseScope::nextVisible(seq, 4, -1, true) == 2, "A3_lockedBackSkipsPriv");
        chk(BrowseScope::nextVisible(seq, 4, +1, true) == -1, "A4_endNoWrap");
        chk(BrowseScope::nextVisible(seq, 0, -1, true) == -1, "A5_startNoWrap");
        chk(BrowseScope::nextVisible(seq, 0, +1, false) == 1, "A6_unlockedReachesPriv");
        chk(BrowseScope::nextVisible(seq, 1, +1, false) == 2, "A7_unlockedFwd");
        // 边界诚实：越界下标 / 0 步长 / 空集合 / 全私密集合都不得"退而求其次"
        chk(BrowseScope::nextVisible(seq, -1, +1, true) == -1, "A8_negativeCur");
        chk(BrowseScope::nextVisible(seq, seq.size(), +1, true) == -1, "A9_curOutOfRange");
        chk(BrowseScope::nextVisible(seq, 0, 0, true) == -1, "A10_zeroDelta");
        chk(BrowseScope::nextVisible(QVector<BrowseSlot>(), 0, 1, true) == -1, "A11_empty");
        const QVector<BrowseSlot> allPriv = { slot("a", true), slot("b", true) };
        chk(BrowseScope::nextVisible(allPriv, 0, +1, true) == -1, "A12_allPrivHidden");
        chk(BrowseScope::nextVisible(allPriv, 0, +1, false) == 1, "A13_allPrivShown");
    }

    // ================= B 选片页：模型 + 代理 =================
    {
        auto item = [](const char* thumb, const char* cat, bool priv) {
            CullItem it;
            it.thumbPath = QString::fromLatin1(thumb);
            it.category  = QString::fromLatin1(cat);
            it.priv      = priv;
            it.status    = QStringLiteral("candidate");
            return it;
        };
        CullModel model;
        model.setItems(QVector<CullItem>{
            item("b0.jpg", "person", false), item("b1.jpg", "person", true),
            item("b2.jpg", "travel", false), item("b3.jpg", "travel", true) },
            QStringLiteral("."));
        // 角色如实透传（隐私门的前提是数据本身诚实）
        chk(model.data(model.index(1, 0), CullModel::PrivRole).toBool(), "B1_privRole");
        chk(!model.data(model.index(0, 0), CullModel::PrivRole).toBool(), "B2_pubRole");
        // ★默认锁：新构造的模型/代理不经任何 set 也必须排除私密（fail-safe）
        model.setMark(0, QStringLiteral("like"));
        model.setMark(1, QStringLiteral("like"));
        const QStringList lockedMarks = model.markedThumbs(QStringLiteral("like"));
        chk(lockedMarks == QStringList{ QStringLiteral("b0.jpg") }, "B3_markedLocked");
        const QMap<QString,int> lockedCounts = model.categoryCounts();
        chk(lockedCounts.value(QStringLiteral("person")) == 1
            && lockedCounts.value(QStringLiteral("travel")) == 1, "B4_chipsLocked");
        CullProxy proxy;
        proxy.setSourceModel(&model);
        chk(proxy.rowCount() == 2, "B5_proxyLocked");
        // —— 解锁态：同一份数据必须全数回来（证明上面的 2 不是空集合）——
        model.setHidePrivate(false);
        proxy.setHidePrivate(false);
        chk(model.markedThumbs(QStringLiteral("like")).size() == 2, "B6_markedUnlocked");
        const QMap<QString,int> freeCounts = model.categoryCounts();
        chk(freeCounts.value(QStringLiteral("person")) == 2
            && freeCounts.value(QStringLiteral("travel")) == 2, "B7_chipsUnlocked");
        chk(proxy.rowCount() == 4, "B8_proxyUnlocked");
    }

    // ================= C 去重对话框：注入点隔离 + 后台落地 =================
    QTemporaryDir td;
    const QString dir = td.path();
    auto disc = [](int bg, int fg) {
        QImage im(64, 64, QImage::Format_ARGB32);
        im.fill(QColor(bg, bg, bg));
        QPainter p(&im); p.setPen(Qt::NoPen); p.setBrush(QColor(fg, fg, fg));
        p.drawEllipse(16, 16, 32, 32);
        return im;
    };
    // 三对「字节完全相同」的重复：公开 x/y 各一对，私密 v 一对。
    // 私密对是否成组 = 隐私门是否生效的可观测信号。
    const QImage x = disc(255, 0), y = disc(0, 200), v = disc(120, 40);
    x.save(QDir(dir).filePath(QStringLiteral("x1.png")), "PNG");
    x.save(QDir(dir).filePath(QStringLiteral("x2.png")), "PNG");
    y.save(QDir(dir).filePath(QStringLiteral("y1.png")), "PNG");
    y.save(QDir(dir).filePath(QStringLiteral("y2.png")), "PNG");
    v.save(QDir(dir).filePath(QStringLiteral("v1.png")), "PNG");
    v.save(QDir(dir).filePath(QStringLiteral("v2.png")), "PNG");

    auto mk = [](const QString& key, const QString& dim, bool priv) {
        Photo p;
        p.path      = QStringLiteral("D:/dup/%1.jpg").arg(key);
        p.thumbFile = QStringLiteral("%1.png").arg(key);
        p.dim       = dim;
        p.priv      = priv;
        return p;
    };
    QVector<Photo> ps;
    ps.append(mk(QStringLiteral("x1"), QStringLiteral("4000x3000"), false));
    ps.append(mk(QStringLiteral("x2"), QStringLiteral("2000x1500"), false));
    ps.append(mk(QStringLiteral("v1"), QStringLiteral("3000x2000"), true));
    ps.append(mk(QStringLiteral("v2"), QStringLiteral("3000x2000"), true));
    ps.append(mk(QStringLiteral("y1"), QStringLiteral("1000x800"),  false));
    ps.append(mk(QStringLiteral("y2"), QStringLiteral("1000x800"),  false));

    auto privLeaked = [](const QVector<DupGroup>& gs) {
        for (const DupGroup& g : gs) {
            if (g.keptPath.contains(QStringLiteral("/v"))) return true;
            for (const QString& d : g.dupPaths)
                if (d.contains(QStringLiteral("/v"))) return true;
        }
        return false;
    };
    auto groupedCount = [](const QVector<DupGroup>& gs) {
        int n = 0; for (const DupGroup& g : gs) n += 1 + g.dupPaths.size(); return n;
    };

    int lockedGroups = 0, unlockedGroups = 0, lockedInputs = 0, unlockedInputs = 0;
    {   // —— 锁定态（hidePrivate=true）：私密照根本不该被看见，更不该被分析 ——
        DedupDialog dlg(&ps, dir, true);
        chk(dlg.qaWaitFinished(), "C1_lockedLanded");
        const QStringList in = dlg.qaInputPaths();
        lockedInputs = in.size();
        chk(lockedInputs == 4, "C2_lockedInputCount");
        chk(!in.contains(QStringLiteral("D:/dup/v1.jpg"))
            && !in.contains(QStringLiteral("D:/dup/v2.jpg")), "C3_lockedInputNoPriv");
        const QVector<DupGroup>& gs = dlg.qaGroups();
        lockedGroups = gs.size();
        chk(lockedGroups == 2, "C4_lockedGroups");
        chk(!privLeaked(gs), "C5_lockedNoPrivGroup");
        chk(groupedCount(gs) == 4, "C6_lockedConserve");   // 4 张公开照恰好各归组一次
    }
    {   // —— 解锁态：私密照照常参与（证明上面的绿不是空心绿）——
        DedupDialog dlg(&ps, dir, false);
        chk(dlg.qaWaitFinished(), "C7_unlockedLanded");
        unlockedInputs = dlg.qaInputPaths().size();
        chk(unlockedInputs == 6, "C8_unlockedInputCount");
        const QVector<DupGroup>& gs = dlg.qaGroups();
        unlockedGroups = gs.size();
        chk(unlockedGroups == 3, "C9_unlockedGroups");
        chk(privLeaked(gs), "C10_unlockedPrivGrouped");
        chk(groupedCount(gs) == 6, "C11_unlockedConserve");
    }

    DedupDialog::setForcePrivacyLeakForQa(false);   // 还原（探针不污染环境）

    const bool pass = (checks > 0) && (marks == checks);
    std::fprintf(stderr,
                 "LA_PRIVACY_QA %s marks=%d checks=%d lockedIn=%d unlockedIn=%d "
                 "lockedGrp=%d unlockedGrp=%d broken=%d\n",
                 pass ? "PASS" : "FAIL", marks, checks, lockedInputs, unlockedInputs,
                 lockedGroups, unlockedGroups, int(broken));
    if (!pass)
        std::fprintf(stderr, "LA_PRIVACY_QA failed: %s\n", qPrintable(failed.join(',')));
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

    // ② 跑分类（合成钩子 → 绕过网络，经 AiWriter 投递 setCategories 写回）。
    //   门在主线程：AiWriter 以 QueuedConnection 把写回投递到 qaCtx，跑完 processEvents 落地。
    QObject qaCtx;
    AiWriter writer(&lib, &qaCtx);
    const bool ok = Classifier::classify(lib.photos(), writer, thumbsDir);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
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

// 方向 C·单写者线程归属门禁（LA_GUI_THREAD_QA，2.33.0 线程安全修复）。
// 从**真实 worker 线程**经 AiWriter 投递写回，断言：① 写回落地（DB+内存）
// ② 最后写库线程 == GUI 线程（Library::lastWriteThreadId 探针）
// ③ 事件循环跑之前尚未落地（证明确实「排队」而非「当场写」，非空心）。
// 四类写回各测一遍：categories / persons / Classifier 合成 / PersonDetector 合成。
// 反例 LA_GUI_THREAD_QA_FORCE_BROKEN=1 → AiWriter 在调用线程当场写（即修复前形态）：
//   期望式（==GUI 线程、落地前未泄漏）一字不改（铁律22），仅实现改变 → 变红。
int runGuiThreadQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_GUI_THREAD_QA_FORCE_BROKEN");

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/la_gui_thread_qa");
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const QString dbPath = QDir(dir).filePath(QStringLiteral("gui.db"));
    QFile::remove(dbPath);
    const QString thumbsDir = dir;   // 合成钩子不读盘，目录仅占位

    // 造库：4 行，category/person 全空（字段对齐 Library::open 的 SELECT）
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_gt_seed"));
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            std::fprintf(stderr, "LA_GUI_THREAD_QA FAIL seedopen\n");
            std::fflush(stderr);
            return 2;
        }
        QSqlQuery q(db);
        q.exec(QStringLiteral(
            "CREATE TABLE photos (path TEXT PRIMARY KEY, rel TEXT, thumb TEXT, "
            "missing INTEGER DEFAULT 0, exif_json TEXT, category TEXT, person TEXT, "
            "private INTEGER DEFAULT 0, rating INTEGER DEFAULT 0, mark TEXT)"));
        q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)"));
        for (int i = 0; i < 4; ++i) {
            q.prepare(QStringLiteral(
                "INSERT INTO photos(path,rel,thumb,missing,exif_json,category,person,private,rating,mark) "
                "VALUES(?,?,?,0,?,'','',0,0,'')"));
            q.addBindValue(QStringLiteral("D:/g%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("g%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("t%1.jpg").arg(i));
            q.addBindValue(QStringLiteral("{}"));
            q.exec();
        }
        db.close();
        QSqlDatabase::removeDatabase(QStringLiteral("la_gt_seed"));
    }

    QString err;
    Library lib;
    if (!lib.open(dbPath, &err)) {
        std::fprintf(stderr, "LA_GUI_THREAD_QA FAIL open=%s\n", qPrintable(err));
        std::fflush(stderr);
        return 2;
    }
    // 本门只测线程归属；内存同步保持正常（同步缺陷由 CLASSIFY/PERSON_QA 覆盖）
    Library::setForceCategoryNoSyncForQa(false);
    Library::setForcePersonNoSyncForQa(false);

    QObject guiCtx;   // 所在线程 = GUI 线程 = AiWriter 的目标写线程
    const quintptr guiTid = reinterpret_cast<quintptr>(QThread::currentThread());
    AiWriter::setForceInlineForQa(broken);   // 反例：调用线程当场写（修复前形态）

    QStringList fails;
    const auto note = [&fails](bool cond, const QString& what) { if (!cond) fails << what; };

    const auto dbStr = [&dbPath](const QString& col, const QString& p) {
        QString v;
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("la_gt_read"));
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT %1 FROM photos WHERE path=?").arg(col));
            q.addBindValue(p);
            if (q.exec() && q.next()) v = q.value(0).toString();
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("la_gt_read"));
        return v;
    };
    const auto memCat = [&lib](const QString& p) {
        for (const Photo& ph : lib.photos()) { if (ph.path == p) return ph.category; }
        return QString();
    };
    const auto memPerson = [&lib](const QString& p) {
        for (const Photo& ph : lib.photos()) { if (ph.path == p) return ph.person; }
        return QString();
    };

    note(lib.verifyPathIndex(), QStringLiteral("verifyPathIndex false"));

    // ---- A：AiWriter::categories 从 worker 线程投递 ----
    {
        QVector<CatItem> syn;
        syn.append({QStringLiteral("D:/g0.jpg"), QStringLiteral("风光"), 0.9});
        AiWriter writer(&lib, &guiCtx);
        QtConcurrent::run([&writer, syn] { writer.categories(syn); }).waitForFinished();
        note(memCat(QStringLiteral("D:/g0.jpg")).isEmpty(),
             QStringLiteral("A leaked before event loop (inline write)"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        note(memCat(QStringLiteral("D:/g0.jpg")) == QStringLiteral("风光"),
             QStringLiteral("A mem g0=%1").arg(memCat(QStringLiteral("D:/g0.jpg"))));
        note(dbStr(QStringLiteral("category"), QStringLiteral("D:/g0.jpg")) == QStringLiteral("风光"),
             QStringLiteral("A db g0=%1").arg(dbStr(QStringLiteral("category"), QStringLiteral("D:/g0.jpg"))));
        note(lib.lastWriteThreadId() == guiTid,
             QStringLiteral("A lastWriteThread!=GUI tid=%1 gui=%2")
                 .arg(lib.lastWriteThreadId()).arg(guiTid));
    }

    // ---- B：AiWriter::persons 从 worker 线程投递 ----
    {
        QVector<PersonItem> syn;
        syn.append({QStringLiteral("D:/g1.jpg"), QStringLiteral("张三")});
        AiWriter writer(&lib, &guiCtx);
        QtConcurrent::run([&writer, syn] { writer.persons(syn); }).waitForFinished();
        note(memPerson(QStringLiteral("D:/g1.jpg")).isEmpty(),
             QStringLiteral("B leaked before event loop"));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        note(memPerson(QStringLiteral("D:/g1.jpg")) == QStringLiteral("张三"),
             QStringLiteral("B mem g1=%1").arg(memPerson(QStringLiteral("D:/g1.jpg"))));
        note(lib.lastWriteThreadId() == guiTid, QStringLiteral("B lastWriteThread!=GUI"));
    }

    // ---- C：Classifier::classify（合成钩子）在 worker 里跑，写回仍应落 GUI 线程 ----
    {
        QVector<CatItem> syn;
        syn.append({QStringLiteral("D:/g2.jpg"), QStringLiteral("人像"), 0.9});
        Classifier::setSyntheticForQa(syn);
        const QVector<Photo> snap = lib.photos();
        AiWriter writer(&lib, &guiCtx);
        QtConcurrent::run([&writer, snap, thumbsDir] {
            Classifier::classify(snap, writer, thumbsDir);
        }).waitForFinished();
        Classifier::clearSyntheticForQa();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        note(memCat(QStringLiteral("D:/g2.jpg")) == QStringLiteral("人像"),
             QStringLiteral("C mem g2=%1").arg(memCat(QStringLiteral("D:/g2.jpg"))));
        note(lib.lastWriteThreadId() == guiTid, QStringLiteral("C lastWriteThread!=GUI"));
    }

    // ---- D：PersonDetector::detect（合成钩子）在 worker 里跑，写回仍应落 GUI 线程 ----
    {
        QVector<PersonItem> syn;
        syn.append({QStringLiteral("D:/g3.jpg"), PersonDetector::defaultPerson()});
        PersonDetector::setSyntheticForQa(syn);
        const QVector<Photo> snap = lib.photos();
        AiWriter writer(&lib, &guiCtx);
        QtConcurrent::run([&writer, snap] {
            PersonDetector::detect(snap, writer);
        }).waitForFinished();
        PersonDetector::clearSyntheticForQa();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
        note(memPerson(QStringLiteral("D:/g3.jpg")) == PersonDetector::defaultPerson(),
             QStringLiteral("D mem g3=%1").arg(memPerson(QStringLiteral("D:/g3.jpg"))));
        note(lib.lastWriteThreadId() == guiTid, QStringLiteral("D lastWriteThread!=GUI"));
    }

    // 复位钩子（各门独立进程，仍显式清理）
    AiWriter::setForceInlineForQa(false);
    Library::setForceCategoryNoSyncForQa(false);
    Library::setForcePersonNoSyncForQa(false);
    if (!qEnvironmentVariableIsSet("LA_GUI_THREAD_QA_KEEP"))
        QDir(dir).removeRecursively();

    const bool okAll = fails.isEmpty();
    if (okAll) {
        std::fprintf(stderr,
            "LA_GUI_THREAD_QA PASS rows=4 broken=%d defer=1 write=1 thread=1 classifier=1 person=1\n",
            int(broken));
    } else {
        std::fprintf(stderr, "LA_GUI_THREAD_QA FAIL broken=%d reason=%s\n",
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
    FaceCluster::qaSetForceMergeForQa(!qEnvironmentVariableIsEmpty("LA_FACE_CLUSTER_QA_FORCE_MERGE"));

    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) {
        if (!ok) fails << what;
    };

    // ---- 2.35.0 两遍聚类纯函数判别组（O/C/S/D，铁律34：O 在旧贪心下必红）----
    // ---- 合成 embedding（确定性，无随机，铁律29）----
    // ⚠ 打包必须走本机 float 字节序（与 FaceCluster::cosine 的 reinterpret_cast 读法
    //   同源）。禁用 QDataStream（默认大端会把小端读法打成乱序）。见 unitVec 注释同款坑。
    auto blob = [](std::initializer_list<double> v) {
        QByteArray b;
        for (double x : v) { const float f = float(x);
                             b.append(reinterpret_cast<const char*>(&f), 4); }
        return b;
    };
    // 两个"不同人"方向（正交）+ 各自近重复（同向量+固定微扰），dim=8 足够证算法
    const QByteArray A  = blob({1,0,0,0,0,0,0,0});
    const QByteArray Ap = blob({0.999,0.02,0,0,0,0,0,0});     // 同人噪声代表
    const QByteArray App= blob({0.998,0.03,-0.01,0,0,0,0,0});
    const QByteArray B  = blob({0,0,0,0,0,0,0,1});
    const QByteArray Bp = blob({0,0,0,0,0,0,0.02,0.999});
    const double kThr = 0.4;

    struct CI { QString p; QByteArray e; };
    auto build = [](QVector<CI> in){
        QVector<ClusterInput> o; for (auto& x : in){ ClusterInput c; c.path=x.p; c.emb=x.e; o<<c; } return o; };

    // 集合意义分区（不依赖簇序）：path→成员集合，规范化后比较
    auto partitionKey = [](const ClusterResult& r){
        QStringList parts;
        for (const ClusterGroup& g : r.groups) {
            QStringList m = g.paths; m.sort();
            if (!m.isEmpty()) parts << m.join(QChar('|'));
        }
        parts.sort(); return parts.join(QChar(';'));
    };

    // ---- O 顺序不变性 ----
    {
        // 近阈值平面链 P1..P6（铁律29 确定性）：单位向量嵌入 dim3/dim4（既有 fixture 的
        // 空平面 → 与 A/C(dim0-2)、B(dim6-7) 全部正交 cos≈0，离阈值 0.4 裕量 ≥0.1），
        // 角度 25°/35°/60° | 100°/110°/120°。判别机制：桥接项 P3@60° 的归附在 seed-only
        // 下取决于 leader 顺序——正向 {P1P2P3}|{P4P5P6}，反向 P6@120° 先入 leader
        // （cos(P3,P6)=cos60°=0.5≥0.4）→ {P1P2}|{P3P4P5P6} → 三排列分区不等（RED，旧算法
        // 必红）；精炼遍用均值质心替代 leader 向量：P3 对 A 侧质心30° cos=0.866 > 对含己
        // 混合质心98° cos=0.788 → 三排列全部收敛同一分区 {P1P2P3}|{P4P5P6}（GREEN）。
        // ⚠ 控制器原定 3 链 0°/40°/80° 不可行（复刻实验证实）：贪心落单的 straggler 自成
        // 单例簇，精炼质心=其自身向量（cos=1 恒最大）→ 永不吸收 → 3 链在 refine ON 下
        // 仍顺序敏感（{P1P2}|{P3} vs {P1}|{P2P3}），O 会假红。6 点双段链是最小可判别形。
        auto chain = [&blob](double deg) { const double r = deg * 3.14159265358979323846 / 180.0;
                                           return blob({0,0,0,std::cos(r),std::sin(r),0,0,0}); };
        const QVector<ClusterInput> base = build({
            {"A1",A},{"A2",Ap},{"A3",App},{"B1",B},{"B2",Bp},{"A4",A},
            {"C1",blob({0.5,0.5,0,0,0,0,0,0})},{"C2",blob({0.5,0.5,0.02,0,0,0,0,0})},
            {"B3",B},{"A5",Ap},{"B4",Bp},{"A6",App},
            {"P1",chain(25)},{"P2",chain(35)},{"P3",chain(60)},
            {"P4",chain(100)},{"P5",chain(110)},{"P6",chain(120)}});
        const QVector<ClusterInput> p1 = base;
        QVector<ClusterInput> p2 = base; std::reverse(p2.begin(), p2.end());
        QVector<ClusterInput> p3 = base; for (int i=1;i<p3.size();++i) std::swap(p3[i], p3[i-1]); // 定步乱排
        const ClusterResult r1 = FaceCluster::cluster(p1, kThr, 1);
        const QString k1 = partitionKey(r1);
        const ClusterResult r2 = FaceCluster::cluster(p2, kThr, 1);
        const QString k2 = partitionKey(r2);
        const QString k3 = partitionKey(FaceCluster::cluster(p3, kThr, 1));
        note(k1 == k2 && k2 == k3, QStringLiteral("O order-invariance: partition differs across permutations"));
        // 链形断言（铁律34 防空心门）：refine ON 的期望终局 = 唯一链分裂
        // {P1,P2,P3} 与 {P4,P5,P6}（P3 被 A 侧均值质心收编）。seed-only 下反向排列
        // 给出 {P1,P2}|{P3..P6} → 本断言与顺序断言双双转红，正算法下双双为绿。
        auto chainShapeOk = [](const ClusterResult& r){
            bool left = false, right = false;
            for (const ClusterGroup& g : r.groups) {
                if (g.paths.size() == 3 && g.paths.contains(QStringLiteral("P1"))
                    && g.paths.contains(QStringLiteral("P2")) && g.paths.contains(QStringLiteral("P3"))) left = true;
                if (g.paths.size() == 3 && g.paths.contains(QStringLiteral("P4"))
                    && g.paths.contains(QStringLiteral("P5")) && g.paths.contains(QStringLiteral("P6"))) right = true;
            }
            return left && right; };
        note(chainShapeOk(r1) && chainShapeOk(r2),
             QStringLiteral("O chain: straggler P3 must be centroid-absorbed into {P1,P2} (seed-only splits)"));
    }

    // ---- C 质心优于头 ----
    {
        const QVector<ClusterInput> in = build({{"A1",A},{"A2",Ap},{"A3",App},{"B1",B},{"B2",Bp}});
        const ClusterResult r = FaceCluster::cluster(in, kThr, 1);
        bool better = true;
        for (const ClusterGroup& g : r.groups) {
            if (g.paths.size() < 2) continue;
            // 成员向量集合
            QVector<QByteArray> memb; for (const QString& p : g.paths) for (const ClusterInput& c : in) if (c.path==p) memb<<c.emb;
            const QByteArray& head = memb.first();
            // 质心 = 成员均值再单位化
            const int n = memb.first().size()/4;
            QVector<double> acc(n, 0.0);
            for (const QByteArray& e : memb){ const float* f=(const float*)e.constData(); for(int d=0;d<n;++d) acc[d]+=f[d]; }
            double s=0; QByteArray cb(n*4,'\0'); float* o=(float*)cb.data();
            for(int d=0;d<n;++d){ double v=acc[d]/memb.size(); o[d]=(float)v; s+=v*v; }
            double inv = s>0?1.0/std::sqrt(s):0.0; for(int d=0;d<n;++d) o[d]=(float)(o[d]*inv);
            double sumCent=0, sumHead=0;
            for (const QByteArray& e : memb){ sumCent += FaceCluster::cosine(e, cb); sumHead += FaceCluster::cosine(e, head); }
            if (!(sumCent/memb.size() >= sumHead/memb.size() - 1e-9)) better = false;
        }
        note(better, QStringLiteral("C centroid must not be worse than head-rep per cluster"));
    }

    // ---- S 同人必同簇（3 排列）----
    {
        const QVector<ClusterInput> base = build({{"A1",A},{"A2",Ap},{"A3",App},{"B1",B},{"B2",Bp}});
        bool ok = true;
        auto sameCluster = [](const ClusterResult& r, const QString& x, const QString& y){
            for (const ClusterGroup& g : r.groups) if (g.paths.contains(x) && g.paths.contains(y)) return true; return false; };
        for (int rot = 0; rot < 3; ++rot) {
            QVector<ClusterInput> in = base; std::rotate(in.begin(), in.begin()+rot, in.end());
            const ClusterResult r = FaceCluster::cluster(in, kThr, 1);
            if (!sameCluster(r,"A1","A2") || !sameCluster(r,"A2","A3") || !sameCluster(r,"B1","B2")) ok = false;
        }
        note(ok, QStringLiteral("S same-person must cluster together across permutations"));
    }

    // ---- D 异人必分簇 ----
    {
        const QVector<ClusterInput> in = build({{"A1",A},{"A2",Ap},{"B1",B},{"B2",Bp}});
        const ClusterResult r = FaceCluster::cluster(in, kThr, 1);
        bool seps = true;
        for (const ClusterGroup& g : r.groups)
            if (g.paths.contains("A1") && (g.paths.contains("B1")||g.paths.contains("B2"))) seps = false;
        note(seps, QStringLiteral("D different-person must not share a cluster"));
    }

    const bool pureOk = fails.isEmpty();   // 纯组判别快照（供 PASS 行四个标志）

    // ① 索引自检 + 守恒基线
    note(lib.verifyPathIndex(), QStringLiteral("verifyPathIndex false"));
    note(lib.photos().size() == kRows,
         QStringLiteral("conserve size=%1 want=%2").arg(lib.photos().size()).arg(kRows));

    // ② 抽 embedding 并落库 face_emb（合成钩子 → 绕过网络，经 AiWriter 投递写回）。
    //   门在主线程：processEvents 让 QueuedConnection 写回落地后，③ 才能读回。
    QObject qaCtx;
    AiWriter writer(&lib, &qaCtx);
    const bool okEmb = PersonDetector::detectEmbeddings(lib.photos(), writer);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    // 钩子支契约腿（诚实定位，复审 Minor「这条在合成钩子下恒真」）：合成钩子早退路径按
    // 设计 return true，故此腿**不**证明真路径的返回值语义——真路径的 false 由 #75 G9
    // 的 ERR/取消/干净三臂物理咬住。留着它只为归因：钩子支哪天改成 false，③ 起的读回
    // 断言会成片红，这条先把结论钉在「钩子本身坏了」上。
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
    FaceCluster::qaSetForceMergeForQa(false);
    PersonDetector::clearSyntheticEmbForQa();
    if (!qEnvironmentVariableIsSet("LA_FACE_CLUSTER_QA_KEEP"))
        QDir(dir).removeRecursively();

    const bool okAll = fails.isEmpty();
    if (okAll) {
        std::fprintf(stderr,
            "LA_FACE_CLUSTER_QA PASS rows=%d broken=%d emb=1 conserve=1 group=1 write=1 sync=1 retrieve=1 rename=1 order=%d centroid=%d sameper=%d diffper=%d\n",
            kRows, int(broken), int(pureOk), int(pureOk), int(pureOk), int(pureOk));
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
        // 2.33.0 对称 API（与 clearClusterFilter/hasClusterFilter 同口径）：
        // UI 侧「取消筛选」必须走 clearStoryFilter，且 hasStoryFilter 如实反映状态
        note(model.hasStoryFilter(), QStringLiteral("hasStoryFilter want=1 after set"));
        model.clearStoryFilter();
        note(!model.hasStoryFilter(), QStringLiteral("hasStoryFilter want=0 after clear"));
        note(model.rowCount() == kRows,
             QStringLiteral("rowCount(全部)=%1 want=%2").arg(model.rowCount()).arg(kRows));
    }

    // ⑤ 标题契约（2.33.0 补）。2.32.0 两处对用户撒谎：
    //    · `begin == end` 与「无日期」共用一支 → 同一天拍的有 EXIF 照片被标成
    //      「未标注日期」（合成集里 c2 = 北京 2024-05-20 单张，正是此形态）；
    //    · 跨年月标题只写起始年 → 2023-12~2024-01 显示成「2023年12月-1月」。
    //    期望值由本门**独立写死**的规格串算出（铁律 22：绝不从被测实现反推期望），
    //    但同样过 translate() 取词 → 与主机系统语言无关（铁律 29：英文系统上不误红）。
    {
        const QString bj = QStringLiteral("北京");
        const QDate d20(2024, 5, 20), dA(2024, 5, 1), dB(2024, 5, 3),
                    dC(2024, 6, 2), dY1(2023, 12, 20), dY2(2024, 1, 2);
        note(StoryGen::titleFor(bj, QDate(), QDate())
                 == QCoreApplication::translate("LightAlbum", "%1 · 未标注日期").arg(bj),
             QStringLiteral("T1 nodate title=%1").arg(StoryGen::titleFor(bj, QDate(), QDate())));
        note(StoryGen::titleFor(bj, d20, d20)
                 == QCoreApplication::translate("LightAlbum", "%1 · %2年%3月%4日")
                        .arg(bj).arg(2024).arg(5).arg(20),
             QStringLiteral("T2 single-day title=%1").arg(StoryGen::titleFor(bj, d20, d20)));
        note(StoryGen::titleFor(bj, dA, dB)
                 == QCoreApplication::translate("LightAlbum", "%1 · %2年%3月")
                        .arg(bj).arg(2024).arg(5),
             QStringLiteral("T3 same-month title=%1").arg(StoryGen::titleFor(bj, dA, dB)));
        note(StoryGen::titleFor(bj, dA, dC)
                 == QCoreApplication::translate("LightAlbum", "%1 · %2年%3月-%4月")
                        .arg(bj).arg(2024).arg(5).arg(6),
             QStringLiteral("T4 cross-month title=%1").arg(StoryGen::titleFor(bj, dA, dC)));
        note(StoryGen::titleFor(bj, dY1, dY2)
                 == QCoreApplication::translate("LightAlbum", "%1 · %2年%3月-%4年%5月")
                        .arg(bj).arg(2023).arg(12).arg(2024).arg(1),
             QStringLiteral("T5 cross-year title=%1").arg(StoryGen::titleFor(bj, dY1, dY2)));
        // 四支规格串必须两两不同：若实现把两支合并（正是 2.32 的缺陷形态），
        // 上面的逐支比对会红；这里再加一道「规格本身不塌缩」的保险。
        {
            const QStringList four = {
                QCoreApplication::translate("LightAlbum", "%1 · 未标注日期").arg(bj),
                QCoreApplication::translate("LightAlbum", "%1 · %2年%3月%4日").arg(bj).arg(2024).arg(5).arg(20),
                QCoreApplication::translate("LightAlbum", "%1 · %2年%3月").arg(bj).arg(2024).arg(5),
                QCoreApplication::translate("LightAlbum", "%1 · %2年%3月-%4年%5月").arg(bj).arg(2023).arg(12).arg(2024).arg(1),
            };
            QSet<QString> u;
            for (const QString& t : four) u.insert(t);
            note(u.size() == four.size(), QStringLiteral("T6 title specs collapsed=%1").arg(u.size()));
        }
    }

    // ⑥ 语言无关的结构判别器：有日期的故事标题必须含起始年份数字，
    //    无日期的标题必须**不含任何数字**（丢年份 / 误标日期都会在此转红）。
    {
        int datedNoYear = 0, nodateWithDigit = 0;
        for (const StoryGen::Story& s : r.stories) {
            bool anyDigit = false;
            for (const QChar& c : s.title) if (c.isDigit()) { anyDigit = true; break; }
            const bool isNodate = s.id.contains(QStringLiteral("|nodate|"));
            if (isNodate) { if (anyDigit) ++nodateWithDigit; }
            else if (!s.title.contains(QString::number(s.begin.year()))) ++datedNoYear;
        }
        note(nodateWithDigit == 0,
             QStringLiteral("S1 nodate titles carrying digits=%1").arg(nodateWithDigit));
        note(datedNoYear == 0,
             QStringLiteral("S2 dated titles missing year=%1").arg(datedNoYear));
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
            "groups=1 nodate=1 rowCount=1 title=1 spec=1 shape=1\n", kRows, int(broken));
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

// 2.34.0 动效引擎门禁（LA_MOTION_QA）。纯函数真源直测（铁律 20：产品与门禁同源），
// 无外部素材、全离屏。断言分五组：
//   A 曲线契约：端点 f(0)==0/f(1)==1、单调曲线值域不减、OutBack 过冲 >1、
//     Pulse 周期 f(0)==f(1) 且值域 [0.25,1]、skeletonPulse 逐点委托引擎 Pulse（接线未漂移）、
//     减动效兜底 qaForceTerminal → 全曲线塌成终值 1。
//   B 错峰 stagger：stagger(0)==0、单调不减、末位 <=1、total<=1 恒 0。
//   C 预设表完整性：>=5 预设、时长 >0、键非空、evaluate 输出维度==键数、未知名回退非空。
//   D 过冲回归段：StarBounce 的 glow 键 evaluate(f=2)==return_to（弹完归位），
//     主段中点已离开起点（真在弹）。
//   E 粒子轨迹纯函数 + 像素判别：f=0 在中心、f=1 散开、alpha 递减；两相位渲染必不同；
//     减动效下（forceTerminal）两相位渲染恒等（不闪）。
// 反例 LA_MOTION_QA_FORCE_BROKEN=1 经 Motion::qaSetForceTerminal(true) 令引擎全曲线
// 塌成终值 → A「起点 f(0)==0」与「Pulse min==0.25」等**同一批**断言转红（期望值不随
// broken 切换，铁律 22；正例实测为绿 → 非空心）。
int runMotionQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_MOTION_QA_FORCE_BROKEN");
    QStringList fails;
    const auto note = [&fails](bool ok, const QString& what) { if (!ok) fails << what; };

    // 反例注入：全曲线塌成终值（模拟「减动效接线断链 / 引擎被旁路」形态）。
    // 此后所有 A 组端点断言在 broken 下必红；正常态 forceTerminal=false 全绿。
    Motion::qaSetForceTerminal(broken);
    const bool savedTerm = Motion::qaForceTerminal();   // 供 E 组临时切换后复原

    // ---- A 曲线契约 ----
    for (int c = Motion::Linear; c < Motion::kCurveCount; ++c) {
        const auto cc = static_cast<Motion::Curve>(c);
        if (cc == Motion::Pulse) continue;   // Pulse 周期波，f(1)==0.25 非终值，另行断言
        note(qAbs(Motion::evalCurve(cc, 1.0) - 1.0) < 1e-6,
             QStringLiteral("A endpoint f(1)==1 curve=%1 got=%2")
                 .arg(c).arg(Motion::evalCurve(cc, 1.0), 0, 'f', 4));
    }
    for (int c = Motion::Linear; c < Motion::kCurveCount; ++c) {
        const auto cc = static_cast<Motion::Curve>(c);
        if (cc == Motion::Pulse) continue;   // Pulse 起点=峰值 1（周期波），不适用 f(0)==0
        note(qAbs(Motion::evalCurve(cc, 0.0)) < 1e-6,
             QStringLiteral("A start f(0)==0 curve=%1 got=%2")
                 .arg(c).arg(Motion::evalCurve(cc, 0.0), 0, 'f', 4));
    }
    {
        const Motion::Curve mono[] = { Motion::Linear, Motion::InQuad, Motion::OutQuad,
                                       Motion::InOutCubic, Motion::OutCubic, Motion::OutExpo };
        for (auto cc : mono) {
            double prev = Motion::evalCurve(cc, 0.0);
            bool ok = true;
            for (int i = 1; i <= 200; ++i) {
                const double v = Motion::evalCurve(cc, i / 200.0);
                if (v < prev - 1e-9) { ok = false; break; }
                prev = v;
            }
            note(ok, QStringLiteral("A monotone curve=%1").arg(int(cc)));
        }
    }
    {
        bool over = false;
        for (int i = 1; i < 200; ++i)
            if (Motion::evalCurve(Motion::OutBack, i / 200.0) > 1.0) over = true;
        note(over, QStringLiteral("A OutBack must overshoot >1 mid-flight"));
    }
    {
        double lo = 9.0, hi = -9.0;
        for (int i = 0; i <= 100; ++i) {
            const double v = Motion::evalCurve(Motion::Pulse, i / 100.0);
            lo = qMin(lo, v); hi = qMax(hi, v);
        }
        note(qAbs(Motion::evalCurve(Motion::Pulse, 0.0)
                  - Motion::evalCurve(Motion::Pulse, 1.0)) < 1e-9,
             QStringLiteral("A Pulse periodic f(0)==f(1)"));
        note(lo >= 0.249 && lo <= 0.251,
             QStringLiteral("A Pulse min=%1 want 0.25").arg(lo, 0, 'f', 4));
        note(qAbs(hi - 1.0) < 1e-6,
             QStringLiteral("A Pulse max=%1 want 1.0").arg(hi, 0, 'f', 4));
    }
    {   // 接线未漂移：Lumen::skeletonPulse(t,false) 必须逐点等于引擎 Pulse
        bool same = true;
        for (int i = 0; i <= 100; ++i) {
            const double t = i / 100.0;
            if (qAbs(Lumen::skeletonPulse(t, false)
                     - Motion::evalCurve(Motion::Pulse, t)) > 1e-9) same = false;
        }
        note(same, QStringLiteral("A skeletonPulse must delegate to engine Pulse exactly"));
    }
    {   // 减动效兜底：forceTerminal → 全曲线任意 t 塌成 1
        Motion::qaSetForceTerminal(true);
        bool allTerm = true;
        for (int c = Motion::Linear; c < Motion::kCurveCount; ++c)
            if (qAbs(Motion::evalCurve(static_cast<Motion::Curve>(c), 0.3) - 1.0) > 1e-9)
                allTerm = false;
        Motion::qaSetForceTerminal(savedTerm);
        note(allTerm, QStringLiteral("A forceTerminal must collapse all curves to 1"));
    }

    // ---- B 错峰 stagger ----
    note(qAbs(Motion::staggerStart(0, 5, 0.6)) < 1e-9,
         QStringLiteral("B stagger(0,n,*)==0"));
    {
        bool mono = true; double prev = -1.0;
        for (int i = 0; i < 8; ++i) {
            const double s = Motion::staggerStart(i, 8, 0.7);
            if (s < prev - 1e-9) mono = false;
            prev = s;
        }
        note(mono, QStringLiteral("B stagger monotone non-decreasing"));
    }
    note(Motion::staggerStart(7, 8, 0.7) <= 1.0 + 1e-9,
         QStringLiteral("B stagger last<=1"));
    note(qAbs(Motion::staggerStart(3, 1, 0.5)) < 1e-9,
         QStringLiteral("B stagger total<=1 must be 0"));

    // ---- C 预设表完整性 ----
    {
        const QVector<QByteArray> names = Motion::presetNames();
        note(names.size() >= 5,
             QStringLiteral("C preset count=%1 want>=5").arg(names.size()));
        for (const QByteArray& nm : names) {
            const Motion::Preset& p = Motion::preset(nm);
            const QString q = QString::fromLatin1(nm);
            note(p.durationMs > 0, QStringLiteral("C duration>0 %1").arg(q));
            note(!p.keys.isEmpty(), QStringLiteral("C keys nonempty %1").arg(q));
            const QVector<double> v0 = Motion::evaluate(p, 0.0);
            const QVector<double> v1 = Motion::evaluate(p, 1.0);
            note(v0.size() == p.keys.size() && v1.size() == p.keys.size(),
                 QStringLiteral("C evaluate dim==keys %1").arg(q));
        }
        const Motion::Preset& fb = Motion::preset("NoSuchPreset");
        note(fb.durationMs > 0 && !fb.keys.isEmpty(),
             QStringLiteral("C unknown name falls back to nonempty preset"));
    }

    // ---- D 过冲回归段 ----
    {
        const Motion::Preset& p = Motion::preset(Motion::kStarBounce);
        int ix = -1;
        for (int i = 0; i < p.keys.size(); ++i)
            if (p.keys[i].prop == Motion::kPropGlow) { ix = i; break; }
        note(ix >= 0, QStringLiteral("D StarBounce must have a glow key"));
        if (ix >= 0) {
            const double at2 = Motion::evaluate(p, 2.0)[ix];
            note(qAbs(at2 - p.keys[ix].return_to) < 1e-6,
                 QStringLiteral("D glow(f=2)=%1 want return_to=%2")
                     .arg(at2, 0, 'f', 4).arg(p.keys[ix].return_to, 0, 'f', 4));
            const double mid = Motion::evaluate(p, 0.5)[ix];
            note(mid > 0.2,
                 QStringLiteral("D glow(f=0.5)=%1 must leave start (real bounce)")
                     .arg(mid, 0, 'f', 4));
        }
    }

    // ---- E 粒子轨迹纯函数 + 像素判别 ----
    {
        const QPointF c0 = Motion::heartBurstPos(0, 9, 0.0, 40);
        note(qAbs(c0.x()) < 1e-6 && qAbs(c0.y()) < 1e-6,
             QStringLiteral("E burst f=0 must sit at center"));
        const QPointF e1 = Motion::heartBurstPos(0, 9, 1.0, 40);
        note(std::hypot(e1.x(), e1.y()) > 1.0,
             QStringLiteral("E burst f=1 must spread out (r=%1)")
                 .arg(std::hypot(e1.x(), e1.y()), 0, 'f', 3));
        note(Motion::heartBurstAlpha(9, 0.0) > Motion::heartBurstAlpha(9, 1.0),
             QStringLiteral("E burst alpha must decay"));

        // 像素判别走 NavFade 的 offsetY（整段路由过 evalCurve → forceTerminal 下恒终值）
        const Motion::Preset& nav = Motion::preset(Motion::kNavFade);
        int oy = -1;
        for (int i = 0; i < nav.keys.size(); ++i)
            if (nav.keys[i].prop == Motion::kPropOffsetY) { oy = i; break; }
        const auto renderBar = [&](double f) {
            QImage img(160, 40, QImage::Format_ARGB32);
            img.fill(Qt::black);
            QPainter g(&img);
            const double v = Motion::evaluate(nav, f).value(oy, 0.0);
            g.fillRect(QRect(int(v * 4), 10, 12, 20), QColor(0x4f, 0xc0, 0x8a));
            g.end();
            return img;
        };
        note(renderBar(0.2) != renderBar(0.8),
             QStringLiteral("E two motion phases must render differently"));
        // 减动效：forceTerminal → 两相位像素恒等（终值不闪）
        Motion::qaSetForceTerminal(true);
        const QImage t0 = renderBar(0.2), t1 = renderBar(0.8);
        Motion::qaSetForceTerminal(savedTerm);
        note(t0 == t1, QStringLiteral("E reduceMotion must render identical frames"));
    }

    Motion::qaSetForceTerminal(false);   // 复原全局钩子（进程单发，仅卫生）

    const bool okAll = fails.isEmpty();
    if (okAll) {
        std::fprintf(stderr,
            "LA_MOTION_QA PASS broken=%d curves=1 stagger=1 presets=1 return=1 pixel=1\n",
            int(broken));
    } else {
        std::fprintf(stderr, "LA_MOTION_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    }
    std::fflush(stderr);
    return okAll ? 0 : 1;
}

// 事件循环等待器：转 processEvents 直到谓词成立或超时（异步任务的门侧观察点；
// 超时不放行——返回谓词终值交给 note 记红，绝不无声等待）。
static bool eraseWaitUntil(const std::function<bool()>& pred, int timeoutMs)
{
    QElapsedTimer et; et.start();
    while (!pred()) {
        if (et.elapsed() >= timeoutMs) return pred();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

// 2.35.0 AI 消除契约门（离屏，走真 QProcess+帧协议，执行器=确定性 inpaint_stub）。
// A 状态机 queued→running→done；B mask 外逐字节守恒 + mask 内确有改动；
// C 子进程常驻（两次任务同 PID）；D kill 后下一任务自动重启恢复；
// E 非阻塞判别：桩飞行 220ms（LA_ERASE_STUB_DELAY_MS 模拟真模型秒级耗时）期间
//   5ms QTimer 于 running→终态窗口内的 tick 增量 >=20——同步实现整段等待冻在
//   singleShot 回调里 tick≈0 必红（RED 阶段对旧同步实现实测转红，判别力有凭据），
//   线程化后 GUI 事件循环持续转必绿。期望式不随 broken 分支（铁律22）。
// 反例 LA_ERASE_QA_FORCE_BROKEN=1 → run() 返回越界 copy 的 null 结果 →
// applyResult 诚实拒收 → A/B/D 断言同红（期望值恒为规格值，铁律22；非空心，铁律34）。
int runEraseQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_ERASE_QA_FORCE_BROKEN");
    QStringList fails;
    const auto note = [&fails](bool ok, const QString& w){ if (!ok) fails << w; };
    qputenv("LA_ERASE_STUB_DELAY_MS", "220");     // E 组判别窗口（须在桩首次 spawn 前布防）
    qputenv("LA_ERASE_STUB_CHUNK_KB", "8");       // 应答拆块必现装置（同上次：先布防再 spawn）
    qputenv("LA_ERASE_STUB_CHUNK_MS", "3");       // 块间时延：模拟真模型流式出体，短读必现

    EraseBridge bridge;
    // 契约门不允许"静默放行"：stub 缺席 = 开发构建坏了 → 诚实 FAIL（return 非 0 即 RED）。
    if (!bridge.modelReady()) {
        std::fprintf(stderr, "LA_ERASE_QA FAIL executor unavailable -- build target inpaint_stub first\n");
        std::fflush(stderr);
        return 1;
    }
    if (broken) EraseRunner::qaInjectBadSize(true);   // 必须在首次 submit 之前打开

    const int w = 200, h = 200;
    QImage base(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        base.setPixel(x, y, qRgb((x * 2) & 0xff, (y * 2) & 0xff, 0x40));
    QImage mask(w, h, QImage::Format_ARGB32);
    mask.fill(qRgba(0, 0, 0, 0));
    for (int y = 60; y <= 140; ++y) for (int x = 60; x <= 140; ++x) {
        const double dx = x - 100, dy = y - 100;
        if (dx * dx + dy * dy <= 40 * 40) mask.setPixel(x, y, qRgba(0, 0, 0, 255));
    }

    // ---- A 状态机 + B 像素守恒 ----
    const int id = bridge.submit(base, mask);
    note(bridge.task(id) != nullptr, QStringLiteral("A: task missing after submit"));
    eraseWaitUntil([&]{ const EraseTask* t = bridge.task(id);     // 等终态（同步实现回调内即完成；异步靠 queued 送达）
                       return t && (t->status == QLatin1String("done") || t->status == QLatin1String("failed")); }, 8000);
    const EraseTask* ct = bridge.task(id);
    note(ct && ct->status == QLatin1String("done"),
         QStringLiteral("A state machine reaches done, got=%1 note=%2")
             .arg(ct ? ct->status : QStringLiteral("?"))
             .arg(ct ? ct->note : QStringLiteral("?")));
    if (ct && ct->status == QLatin1String("done") && !ct->result.isNull() && ct->result.size() == base.size()) {
        int outDiff = 0, inDiff = 0, inTot = 0;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const bool inside = qAlpha(mask.pixel(x, y)) == 255;
            if (base.pixel(x, y) != ct->result.pixel(x, y)) { inside ? ++inDiff : ++outDiff; }
            if (inside) ++inTot;
        }
        note(outDiff == 0, QStringLiteral("B mask-out byte conservation, leak=%1").arg(outDiff));
        note(inTot > 0 && inDiff * 10 >= inTot,
             QStringLiteral("B mask-in changed ratio=%1").arg(inTot ? double(inDiff) / inTot : -1.0));
    } else {
        note(false, QStringLiteral("B: done result present for pixel check"));
    }

    // ---- C 子进程常驻（同 PID 跨两次 submit）----
    qint64 pidA = 0, pidB = 0;
    bridge.qaPeekRunnerPid(pidA);
    const int id2 = bridge.submit(base, mask);
    eraseWaitUntil([&]{ const EraseTask* t = bridge.task(id2);
                        return t && (t->status == QLatin1String("done") || t->status == QLatin1String("failed")); }, 8000);
    bridge.qaPeekRunnerPid(pidB);
    const EraseTask* ct2 = bridge.task(id2);   // 诊断：C 红时必须暴露 task2 终态与 err（期望式不动，铁律22）
    note(pidA != 0 && pidA == pidB,
         QStringLiteral("C child process persists, pidA=%1 pidB=%2 t2=%3 note=%4")
             .arg(pidA).arg(pidB)
             .arg(ct2 ? ct2->status : QStringLiteral("?"))
             .arg(ct2 ? ct2->note : QStringLiteral("?")));

    // ---- D 崩溃恢复（kill 后下一任务自动重启并走完状态机）----
    bridge.qaKillExecutorForTest();
    const int id3 = bridge.submit(base, mask);
    eraseWaitUntil([&]{ const EraseTask* t = bridge.task(id3);
                        return t && (t->status == QLatin1String("done") || t->status == QLatin1String("failed")); }, 8000);
    const EraseTask* t3 = bridge.task(id3);
    note(t3 && t3->status == QLatin1String("done"),
         QStringLiteral("D after child kill next task recovers, got=%1 note=%2")
             .arg(t3 ? t3->status : QStringLiteral("?"))
             .arg(t3 ? t3->note : QStringLiteral("?")));

    // ---- E 非阻塞判别（任务飞行期间 GUI 事件循环必须持续运转）----
    // 判别式：running→终态窗口内 5ms tick 增量 >=20。同步实现该窗口整体冻在
    // singleShot 回调（tick 增量为 0，必红）；线程化实现 worker 干活、GUI 转圈
    // （220ms 窗口 ≈40+ tick，必绿）。期望值不随 broken 分支（铁律22）。
    {
        int ticks = 0;
        QTimer tickTimer;
        tickTimer.setInterval(5);
        QObject::connect(&tickTimer, &QTimer::timeout, [&ticks]{ ++ticks; });
        bool runningSeen = false;
        int ticksAtRunning = -1, ticksAtTerminal = -1;
        QObject::connect(&bridge, &EraseBridge::taskUpdated, &bridge, [&](int tid){
            const EraseTask* t = bridge.task(tid);
            if (!t) return;
            if (t->status == QLatin1String("running")) {
                if (!runningSeen) { runningSeen = true; ticksAtRunning = ticks; }
            } else if (t->status == QLatin1String("done") || t->status == QLatin1String("failed")) {
                if (ticksAtTerminal < 0) ticksAtTerminal = ticks;
            }
        });
        tickTimer.start();
        const int id4 = bridge.submit(base, mask);
        const bool fin = eraseWaitUntil([&]{ const EraseTask* t = bridge.task(id4);
                                 return t && (t->status == QLatin1String("done") || t->status == QLatin1String("failed")); }, 8000);
        tickTimer.stop();
        if (ticksAtTerminal < 0) ticksAtTerminal = ticks;
        note(fin, QStringLiteral("E: task reached terminal within 8s"));
        note(runningSeen && ticksAtRunning >= 0 && ticksAtTerminal >= ticksAtRunning,
             QStringLiteral("E running observed before terminal, runT=%1 termT=%2").arg(ticksAtRunning).arg(ticksAtTerminal));
        note(ticksAtTerminal - ticksAtRunning >= 20,
             QStringLiteral("E event-loop ticks in flight=%1 (<20 => GUI was blocked)").arg(ticksAtTerminal - ticksAtRunning));
    }

    // ---- A 补：无执行器时任务必须落 blocked_no_model（spec §2.3/§2.4-A 覆盖缺口）。----
    // qaSetForceNoExecutor 令 findExe 直接返回空 -> 全新 EraseBridge.modelReady()==false
    // -> submit() 不 spawn 任何进程、只把任务挂成 blocked_no_model。正/反例双臂皆成立：
    // executor-forcing 缝先于 g_bad 生效（根本没有进程可 spawn，越界 copy 反例无从触发），
    // 故此断言双臂恒 PASS、不进 fails、不随 broken 切换（铁律22 期望值不动）。
    EraseRunner::qaSetForceNoExecutor(true);
    {
        EraseBridge blockedBridge;                        // 本地作用域，独立于上面的 bridge
        note(blockedBridge.modelReady() == false,
             QStringLiteral("A modelReady false when executor forced absent"));
        const int blockedId = blockedBridge.submit(base, mask);
        QCoreApplication::processEvents();                // blocked 态不起跑，状态应恒为 blocked_no_model
        const EraseTask* bt = blockedBridge.task(blockedId);
        note(bt && bt->status == QLatin1String("blocked_no_model"),
             QStringLiteral("A blocked_no_model park without executor, got=%1").arg(bt ? bt->status : QStringLiteral("?")));
    }
    EraseRunner::qaSetForceNoExecutor(false);            // QA 钩子复位（不得泄漏给后续门）

    // ---- G1 淘汰（2.38.0 R3）：连续 9 个任务后任务表保留最近 kTasksKeep=8，
    //      最旧已不可见、最新仍在。共享 bridge 的执行器 DELAY=220 已在顶部设定，
    //      串行等待终态即可（无随机、无时序依赖，铁律29）。
    //      ★ 正确性修正：A/C/D/E 组已在同一 bridge 上先行提交 id 1..4（blocked 补组用
    //      独立 blockedBridge，不计入），故 G1 的「最旧」并非硬编码 id=1（那是 A 组的），
    //      而是本组自身第一个 id。用局部 g1ids 记录本组 9 个 id，断言 first() 已淘汰、
    //      last() 仍在；cap 断言按 brief 原样保留。当前无淘汰实现：9 提交后
    //      qaTaskCount()=13(>8)、task(g1ids.first()) 非空 → 两条 note(false) 必红。
    {
        QVector<int> g1ids;
        int lastId = -1;
        bool allTerminal = true;
        for (int i = 0; i < 9; ++i) {
            lastId = bridge.submit(base, mask);
            g1ids.append(lastId);
            allTerminal = eraseWaitUntil([&]{ const EraseTask* t = bridge.task(lastId);
                return t && (t->status == QLatin1String("done") || t->status == QLatin1String("failed")); }, 8000);
            if (!allTerminal) break;
        }
        note(allTerminal, QStringLiteral("G1 all 9 tasks reached terminal"));
        note(bridge.qaTaskCount() <= 8,
             QStringLiteral("G1 task table capped at 8, got=%1").arg(bridge.qaTaskCount()));
        note(bridge.task(g1ids.first()) == nullptr,
             QStringLiteral("G1 oldest task evicted (id=%1)").arg(g1ids.first()));
        note(bridge.task(g1ids.last()) != nullptr,
             QStringLiteral("G1 newest task still visible (id=%1)").arg(g1ids.last()));
    }

    // ---- G2 取消（2.38.0 R3）：飞行中 cancel -> 态 cancelled + 子进程终止 +
    //      复位后新任务仍能成功。专属桥 + 专属执行器：DELAY=1500 保证取消必然
    //      落在飞行中（确定性；stub 冷启动为毫秒级，真模型 ~8s 是已文档化代价）。
    qputenv("LA_ERASE_STUB_DELAY_MS", "1500");
    {
        EraseBridge cancelBridge;
        const int cid = cancelBridge.submit(base, mask);
        bool runningSeen = eraseWaitUntil([&]{ const EraseTask* t = cancelBridge.task(cid);
            return t && t->status == QLatin1String("running"); }, 3000);
        note(runningSeen, QStringLiteral("G2 task observed running before cancel"));
        qint64 pidBefore = 0;
        eraseWaitUntil([&]{ cancelBridge.qaPeekRunnerPid(pidBefore); return pidBefore != 0; }, 5000);
        const bool accepted = cancelBridge.cancel(cid);
        const EraseTask* ct = cancelBridge.task(cid);
        note(accepted && ct && ct->status == QLatin1String("cancelled"),
             QStringLiteral("G2 cancel accepted -> cancelled, got=%1").arg(ct ? ct->status : QStringLiteral("?")));
        qint64 pidAfter = pidBefore;
        eraseWaitUntil([&]{ cancelBridge.qaPeekRunnerPid(pidAfter); return pidAfter == 0; }, 5000);
        note(pidAfter == 0, QStringLiteral("G2 runner child terminated pidBefore=%1 pidAfter=%2")
                                .arg(pidBefore).arg(pidAfter));
        // 状态定格：worker 的 taskFinished 回调抵达后不得把 cancelled 覆写成 failed/done
        const bool overwritten = eraseWaitUntil([&]{
            const EraseTask* t = cancelBridge.task(cid);
            return !t || t->status != QLatin1String("cancelled");
        }, 500);
        note(!overwritten, QStringLiteral("G2 cancelled status survives worker callback"));
        const int nid = cancelBridge.submit(base, mask);
        const bool nf = eraseWaitUntil([&]{ const EraseTask* t = cancelBridge.task(nid);
            return t && (t->status == QLatin1String("done") || t->status == QLatin1String("failed")); }, 8000);
        const EraseTask* nt = cancelBridge.task(nid);
        note(nf && nt && nt->status == QLatin1String("done"),
             QStringLiteral("G2 post-cancel recovery: next task done, got=%1").arg(nt ? nt->status : QStringLiteral("?")));
    }
    qputenv("LA_ERASE_STUB_DELAY_MS", QByteArray());   // 桩延迟复位（既有纪律）

    EraseRunner::qaInjectBadSize(false);             // 静态钩子复位（不得泄漏给后续门）
    qputenv("LA_ERASE_STUB_DELAY_MS", QByteArray()); // 桩延迟复位（同上纪律）
    qputenv("LA_ERASE_STUB_CHUNK_KB", QByteArray()); // 拆块装置复位（不得泄漏给后续门）
    qputenv("LA_ERASE_STUB_CHUNK_MS", QByteArray());

    if (fails.isEmpty())
        std::fprintf(stderr, "LA_ERASE_QA PASS broken=%d state=1 conserve=1 persist=1 recover=1 blocked=1 nonblock=1\n", int(broken));
    else
        std::fprintf(stderr, "LA_ERASE_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
}

// ---------------- 第四轮 T1 第 75 门 LA_FACE_AI_QA：AiProto 帧协议契约门（fast 层，hermetic 桩） ----------------
// T1 直spawn臂（G1/G2/G4/G5）：门以 QProcess 直起 faceinfer_stub.exe 做协议级断言。
// T4a 真底座臂（G3/G5p线腿/G6/G7/G8）：经 AiSubprocessRunner 全线程机械（owner 线程
// + BlockingQueuedConnection + 单飞闸 + 重启一次 + NoExe 判定），桩同一只，env 钩子经
// runner qaHookEnvName 一次性注入消耗。spec §2.2 GUI 线程禁调 request：门侧全部
// runner 入口经 std::thread worker 调用（主线程直调即被拒 Busy——门自身先守纪律）。
// 执行器发现沿用 EraseRunner::findExe 的 dev-fallback 口径：候选 1 = native/build/ 扁平产物
// （faceinfer/CMakeLists POST_BUILD 拷机注入），候选 2 = 仓根 faceinfer/build/。桩缺席 =
// 开发构建坏了 -> 诚实 FAIL（return 1 即 RED，不许静默放行）。
// 断言组全谱（八组齐，spec §7#75）：
//   G1 默认桩 `READY 1` 握手；
//   G2 LA_FACE_FORCE_NOMODEL -> `READY 0` 后每 REQ 收 `ERR <id> nomodel`（spec §5 保活语义）；
//   G3 [T4a] 真底座全量 roundtrip：REQ(det)->RES JSON 逐字节 + 解析后 deep-equal，
//      体经 FRAG 钩子首 spawn 子进程 64B 分片流式送达（底座体读循环读满的 runner 级兄弟腿）；
//   G4 LA_FACE_FORCE_FRAG -> RES 体（2162B 确定性 JSON）被桩按 64B 分片+片间 fflush 写出，
//      门侧必须按 EraseProto 血泪判例逐批读满（waitForReadyRead 到达批次边界任意）；
//   G5 LA_FACE_FORCE_ERR -> `ERR <id> <code>` 帧结构化：三字段、id 与 REQ 严格一致、code 属已知集；
//   G5p [T4a] AiProto 首词全词比对回归腿（T1 评审遗留#1，规则#34 反例纪律）：
//      parseReq 应用层直调拒 "REQX" 且仍收合法帧 + 线腿喂 REQX 行必收 ERR badframe
//      + sIsMainThread 谓词（主线程真/worker 假，runner GUI 拒绝的依据）；
//   G6 [T4a] LA_FACE_FORCE_HANG 一次性钩子 -> 90s 挂起 1500ms 预算内 Timeout 逃逸
//      （崩溃感知读逃逸，T1 评审遗留#2）-> 自动重启一次的干净子进程第二 call 复绿；
//   G7 [T4a] 并发单飞：LA_FACE_FORCE_SLOW 帧间 3s 睡眠窗口内第二调用者即收 Busy（不排队不死锁）；
//   G8 [T4a] exe 候选全缺 -> ensureReady 与 call 一致 NoExe（spec §2.2 exe_missing）。
// 桩按 LA_FACE_FORCE_* env 分支；门期望表是固定常量、永不随桩分支（铁律22）。
// 反例 LA_FACE_AI_QA_FORCE_BROKEN=1 只篡改期望常量 -> 八组同红，
// 退出路径与正跑完全一致（return fails.isEmpty()?0:1 不按 broken 分支，铁律22/34）。
struct FaceAiSession
{
    QProcess proc;
    QByteArray buf;      // 未消费 stdout 缓冲：握手行/帧头/分片体共用同一条字节流
};

static QString faceAiFindStub()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/faceinfer_stub.exe",                                        // dev：native/build 扁平产物
        QDir::cleanPath(appDir + "/../../faceinfer/build/faceinfer_stub.exe"), // dev build tree 源
    };
    for (const QString& p : candidates) if (QFileInfo::exists(p)) return p;
    return QString();
}

// 读一行（不含 \r\n）。EraseProto 判例：到达批次边界任意，攒够 \n 才算一行。
static bool faceAiReadLine(QProcess& p, QByteArray& buf, QByteArray& line, int timeoutMs)
{
    QElapsedTimer t; t.start();
    for (;;) {
        const int nl = buf.indexOf('\n');
        if (nl >= 0) {
            line = buf.left(nl);
            if (line.endsWith('\r')) line.chop(1);   // 行规范化：parseReq/期望比对均剥 \r
            buf.remove(0, nl + 1);
            return true;
        }
        if (t.elapsed() > timeoutMs) return false;
        const QByteArray more = p.readAllStandardOutput();
        if (!more.isEmpty()) { buf.append(more); continue; }
        if (p.state() != QProcess::Running && p.atEnd()) return false;   // 死体无余字 = 永不达
        p.waitForReadyRead(50);
    }
}

// 读满恰好 n 字节（体读循环 = EraseProto fix-async-regression 判例继承）。
static bool faceAiReadExact(QProcess& p, QByteArray& buf, QByteArray& body, qint64 n, int timeoutMs)
{
    body.clear();
    QElapsedTimer t; t.start();
    for (;;) {
        if (body.size() == n) return true;
        if (!buf.isEmpty()) {
            const int take = int(qMin<qint64>(buf.size(), n - body.size()));
            body.append(buf.constData(), take);
            buf.remove(0, take);
            continue;
        }
        if (t.elapsed() > timeoutMs) return false;
        const QByteArray more = p.readAllStandardOutput();
        if (!more.isEmpty()) { buf.append(more); continue; }
        if (p.state() != QProcess::Running && p.atEnd()) return false;
        p.waitForReadyRead(50);
    }
}

// envKv 形如 "LA_FACE_FORCE_NOMODEL=1"；空 = 默认臂。
static bool faceAiSpawn(FaceAiSession& s, const QString& stub, const QByteArray& envKv)
{
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    if (!envKv.isEmpty()) {
        const int eq = envKv.indexOf('=');
        e.insert(QString::fromLatin1(envKv.left(eq)), QString::fromLatin1(envKv.mid(eq + 1)));
    }
    s.proc.setProcessEnvironment(e);
    s.proc.setProcessChannelMode(QProcess::SeparateChannels);
    s.proc.start(stub, {});
    return s.proc.waitForStarted(5000);
}

// 关 stdin 写端 -> 桩 fgets 得 EOF 自然走人；不走再 kill 兜底。复位纪律同 LA_ERASE_QA。
static void faceAiStop(FaceAiSession& s)
{
    if (s.proc.state() == QProcess::NotRunning) return;
    s.proc.closeWriteChannel();
    if (!s.proc.waitForFinished(1500)) s.proc.kill();
    s.proc.waitForFinished(1000);
}

// REQ 一行 + path 体（QProcess::write 内部已 waitForBytesWritten，Qt6 IODevice 无 flush，同 EraseProto 注）。
static bool faceAiSendReq(FaceAiSession& s, quint64 id, const QByteArray& path)
{
    const QByteArray hdr = QByteArray("REQ ") + QByteArray::number(id)
                         + QByteArray(" path ") + QByteArray::number(path.size()) + QByteArray("\n");
    s.proc.write(hdr);
    s.proc.write(path);
    return s.proc.waitForBytesWritten(3000);
}

// ---- T4a 真底座臂助件：spec §2.2 禁 GUI 线程调 runner——门自身先守纪律，
// 一切 AiSubprocessRunner 入口经独立 worker 线程执行（主线程直调即被底座拒 Busy，
// 这正是被断言的防线本身，不能拿来当调用路径）。----
static AiSubprocessRunner::Reply faceAiCall(AiSubprocessRunner& r, const QString& kind,
                                            quint64 id, const QByteArray& body, int timeoutMs)
{
    AiSubprocessRunner::Reply out;
    std::thread th([&] { out = r.call(kind, id, body, timeoutMs); });
    th.join();
    return out;
}

static bool faceAiProbe(AiSubprocessRunner& r, AiSubprocessRunner::Rst* st)
{
    bool ok = false;
    std::thread th([&] { ok = r.ensureReady(st); });
    th.join();
    return ok;
}

// ---- #75 G9 前置条件件：把一整轮 detect 跑在非主线程上并等它结束。----
// 为什么必须是夹具自带（std::thread+join，与 faceAiCall/faceAiProbe 同款），而不是
// 本门其它腿那样用 QtConcurrent::run：QtConcurrent 走全局 QThreadPool，而该池在拿不到
// 可分配线程时**退回「在调用线程就地执行」**（Qt 6 QThreadPool::start 的 reserveThread
// 返回 -1 分支）。实测证据（本门 TMPPOOL 诊断跑，诊断已撤）：ERR 轮经 QtConcurrent 拿到
// 了真 worker（tid=1a64 ≠ 主线程 457c），其后的干净轮与 4 次预探测**全部**拿回主线程 tid，
// 于是底座按 spec §2.2 正确拒 Busy → probe 假 → detect 假 → 该轮红。
// 红的是线程分配，不是被测语义——夹具不能把「跑在 worker 上」交给运气，故在此固化前置条件。
static void runOffMainQa(const std::function<void()>& fn)
{
    std::thread th(fn);
    th.join();
}

int runFaceAiQa()
{
    const bool tamper = !qEnvironmentVariableIsEmpty("LA_FACE_AI_QA_FORCE_BROKEN");
    QStringList fails;
    const auto note = [&fails](bool ok, const QString& w){ if (!ok) fails << w; };

    const QString stub = faceAiFindStub();
    if (stub.isEmpty()) {
        std::fprintf(stderr, "LA_FACE_AI_QA FAIL faceinfer_stub.exe missing -- build faceinfer target first\n");
        std::fflush(stderr);
        return 1;
    }

    // 与桩 faceinfer_stub.cpp faceJson() 逐字节一致的确定性夹具（铁律29：无随机无宿主因子）。
    const QByteArray expectBody = [] {
        QByteArray s = "{\"ok\":true,\"count\":1,\"faces\":[{\"bbox\":[0,0,8,8],"
                       "\"score\":0.9,\"kps\":[[1,1],[2,2],[3,3],[4,4],[5,5]],"
                       "\"embedding\":[";
        for (int i = 0; i < 512; ++i) { if (i) s.append(','); s.append("0.0"); }
        s.append("]}]}");
        return s;
    }();
    const QByteArray reqPath = QByteArrayLiteral("C:/la/face_qa_fixture.jpg");

    // 固定期望表：tamper 臂只篡改常量，流程/退出路径零分支（铁律22）。
    const QByteArray expG1Ready = tamper ? QByteArray("READY 0") : QByteArray("READY 1");
    const QByteArray expG2Ready = tamper ? QByteArray("READY 1") : QByteArray("READY 0");
    const QByteArray expG2Code  = tamper ? QByteArray("loadfail") : QByteArray("nomodel");
    const QByteArray expG4Body  = tamper ? expectBody + QByteArray("X") : expectBody;
    const QByteArray expG5Code  = tamper ? QByteArray("inferfail") : QByteArray("loadfail");
    // T4a 新组期望常量（Rst 值经 int 落表比对——期望表仍是固定常量，铁律22 同构）。
    using Rst = AiSubprocessRunner::Rst;
    const int expG3Rst     = tamper ? int(Rst::Err)     : int(Rst::Ok);       // roundtrip 成功臂
    const QByteArray expG3Json = tamper ? expectBody + QByteArray("X") : expectBody;
    const QByteArray expG5pWire = tamper ? QByteArray("ERR 1 badframe") : QByteArray("ERR 0 badframe");
    const int expG5pReject = tamper ? 1 : 0;   // parseReq("REQX...") 返回码：拒=0（false）
    const int expG5pAccept = tamper ? 0 : 1;   // parseReq("REQ ...") 返回码：收=1（true，防收紧过界）
    const int expGpMain    = tamper ? 0 : 1;   // sIsMainThread 主线程真
    const int expGpWorker  = tamper ? 1 : 0;   // sIsMainThread worker 线程假
    const int expG6a       = tamper ? int(Rst::Ok)      : int(Rst::Timeout);  // 挂起臂：超时
    const int expG6b       = tamper ? int(Rst::Timeout) : int(Rst::Ok);       // 重启臂：复绿
    const QByteArray expG6bJson = tamper ? expectBody + QByteArray("X") : expectBody;
    const int expG7a       = tamper ? int(Rst::Err)     : int(Rst::Ok);       // 第一路完成
    const int expG7b       = tamper ? int(Rst::Ok)      : int(Rst::Busy);     // 第二路单飞拒
    const int expG8Ready   = tamper ? int(Rst::Ok)      : int(Rst::NoExe);    // 候选全缺 probe
    const int expG8Call    = tamper ? int(Rst::Ok)      : int(Rst::NoExe);    // 候选全缺 call

    // ---- G1 默认臂：READY 1 握手 ----
    {
        FaceAiSession s;
        QByteArray line;
        const bool got = faceAiSpawn(s, stub, QByteArray())
                      && faceAiReadLine(s.proc, s.buf, line, 5000);
        const bool ok = got && line == expG1Ready;
        note(ok, QStringLiteral("G1 READY1 handshake, got=%1")
                     .arg(QString::fromLatin1(got ? line : QByteArray("<no frame>"))));
        faceAiStop(s);
        std::fprintf(stderr, "LA_FACEAI_G G1 ok=%d\n", int(ok));
    }

    // ---- G2 NOMODEL 臂：READY 0 + 每 REQ ERR id nomodel（保活不退出） ----
    {
        FaceAiSession s;
        QByteArray line, resp;
        const bool started = faceAiSpawn(s, stub, "LA_FACE_FORCE_NOMODEL=1");
        const bool gotReady = started && faceAiReadLine(s.proc, s.buf, line, 5000);
        const bool readyOk = gotReady && line == expG2Ready;
        note(readyOk, QStringLiteral("G2 READY0 handshake, got=%1")
                          .arg(QString::fromLatin1(gotReady ? line : QByteArray("<no frame>"))));
        const bool sent = readyOk && faceAiSendReq(s, 7, reqPath);
        const bool gotResp = sent && faceAiReadLine(s.proc, s.buf, resp, 5000);
        const QList<QByteArray> tk = resp.split(' ');
        const bool frameOk = gotResp && tk.size() == 3
                          && tk[0] == QByteArray("ERR") && tk[1] == QByteArray("7")
                          && tk[2] == expG2Code;
        note(frameOk, QStringLiteral("G2 nomodel ERR frame, line=%1").arg(QString::fromLatin1(resp)));
        faceAiStop(s);
        std::fprintf(stderr, "LA_FACEAI_G G2 ok=%d\n", int(readyOk && frameOk));
    }

    // ---- G3 [T4a] 真底座全量 roundtrip：REQ(det)->RES JSON 逐字节 + 解析后 deep-equal。
    //      FRAG 钩子经 runner qaHookEnvName 一次性注入首 spawn 子进程（64B 分片流式送达
    //      = 底座体读循环读满的 runner 级兄弟腿，G4 判例在真机械上复验）。----
    {
        qputenv("LA_FACE_FORCE_FRAG", "1");    // 布防：底座 spawn 时注入子 env 并一次性消耗（分支只在桩侧，铁律22）
        AiSubprocessRunner r;
        r.configure({stub}, {}, 5000, QStringLiteral("LA_FACE_FORCE_FRAG"));
        const auto rp = faceAiCall(r, QStringLiteral("det"), 21, reqPath, 8000);
        const bool rstOk = int(rp.rst) == expG3Rst;
        const bool bytesOk = rp.json == expG3Json;                                   // 逐字节等值
        QJsonParseError pe1, pe2;
        const QJsonDocument dGot = QJsonDocument::fromJson(rp.json, &pe1);
        const QJsonDocument dExp = QJsonDocument::fromJson(expG3Json, &pe2);
        const bool deepOk = pe1.error == QJsonParseError::NoError
                         && pe2.error == QJsonParseError::NoError && dGot == dExp;   // 解析后结构化 deep-equal
        note(rstOk, QStringLiteral("G3 runner roundtrip rst=%1 exp=%2")
                         .arg(int(rp.rst)).arg(expG3Rst));
        note(bytesOk, QStringLiteral("G3 runner roundtrip body bytes=%1 exp=%2")
                          .arg(rp.json.size()).arg(expG3Json.size()));
        note(deepOk, QStringLiteral("G3 RES json deep-equal (parsed)"));
        std::fprintf(stderr, "LA_FACEAI_G G3 ok=%d json_bytes=%d\n",
                     int(rstOk && bytesOk && deepOk), int(rp.json.size()));
    }
    qputenv("LA_FACE_FORCE_FRAG", QByteArray());   // 复位纪律同 LA_ERASE_QA：每次布防必显式解除

    // ---- G4 FRAG 臂：RES 体 64B 分片必读满 ----
    {
        FaceAiSession s;
        QByteArray line, hdr, body;
        const bool started = faceAiSpawn(s, stub, "LA_FACE_FORCE_FRAG=1");
        const bool gotReady = started && faceAiReadLine(s.proc, s.buf, line, 5000);
        const bool readyOk = gotReady && line == QByteArray("READY 1");   // FRAG 不改握手语义（固定期望）
        note(readyOk, QStringLiteral("G4 READY1 handshake under FRAG, got=%1")
                          .arg(QString::fromLatin1(gotReady ? line : QByteArray("<no frame>"))));
        const bool sent = readyOk && faceAiSendReq(s, 9, reqPath);
        const bool gotHdr = sent && faceAiReadLine(s.proc, s.buf, hdr, 5000);
        const QList<QByteArray> tk = hdr.split(' ');
        const bool hdrOk = gotHdr && tk.size() == 3 && tk[0] == QByteArray("RES")
                        && tk[1] == QByteArray("9") && tk[2].toLongLong() == expectBody.size();
        note(hdrOk, QStringLiteral("G4 RES header id/len, line=%1").arg(QString::fromLatin1(hdr)));
        const bool bodyOk = hdrOk
                         && faceAiReadExact(s.proc, s.buf, body, expectBody.size(), 5000)
                         && body == expG4Body;
        note(bodyOk, QStringLiteral("G4 fragmented body read-full, bytes=%1 exp=%2")
                         .arg(body.size()).arg(expG4Body.size()));
        faceAiStop(s);
        std::fprintf(stderr, "LA_FACEAI_G G4 ok=%d body_bytes=%d\n", int(readyOk && hdrOk && bodyOk), int(body.size()));
    }

    // ---- G5 ERR 臂：ERR 帧结构化（kind/id/code 三字段，id 严格匹配） ----
    {
        FaceAiSession s;
        QByteArray line, resp;
        const bool started = faceAiSpawn(s, stub, "LA_FACE_FORCE_ERR=1");
        const bool gotReady = started && faceAiReadLine(s.proc, s.buf, line, 5000);
        const bool readyOk = gotReady && line == QByteArray("READY 1");
        note(readyOk, QStringLiteral("G5 READY1 handshake under ERR arm, got=%1")
                          .arg(QString::fromLatin1(gotReady ? line : QByteArray("<no frame>"))));
        const bool sent = readyOk && faceAiSendReq(s, 11, reqPath);
        const bool gotResp = sent && faceAiReadLine(s.proc, s.buf, resp, 5000);
        const QList<QByteArray> tk = resp.split(' ');
        const bool frameOk = gotResp && tk.size() == 3
                          && tk[0] == QByteArray("ERR") && tk[1] == QByteArray("11")
                          && tk[2] == expG5Code;
        note(frameOk, QStringLiteral("G5 structured ERR frame, line=%1").arg(QString::fromLatin1(resp)));
        faceAiStop(s);
        std::fprintf(stderr, "LA_FACEAI_G G5 ok=%d\n", int(readyOk && frameOk));
    }

    // ---- G5p [T4a] AiProto 首词全词比对回归腿（T1 评审遗留#1，规则#34 反例纪律）：
    //   应用层 parseReq 直调——"REQX 5 det 4" 必须拒（旧 strncmp(3) 前缀放行 = 本腿为红），
    //   "REQ 5 det 4" 必须收（防收紧过界误杀合法帧）；
    //   线腿——默认桩喂 REQX 行必收 ERR badframe 且不消费 body（收紧前桩会误读 body 回 RES）；
    //   谓词腿——sIsMainThread 主线程真/worker 假（runner GUI 线程拒绝的依据）。----
    {
        aiproto::Header hh;
        const QByteArray badLine  = QByteArrayLiteral("REQX 5 det 4");
        const QByteArray goodLine = QByteArrayLiteral("REQ 5 det 4");
        const int gotReject = aiproto::parseReq(badLine.constData(), size_t(badLine.size()), hh) ? 1 : 0;
        const int gotAccept = aiproto::parseReq(goodLine.constData(), size_t(goodLine.size()), hh) ? 1 : 0;
        const bool rejOk = gotReject == expG5pReject;
        const bool accOk = gotAccept == expG5pAccept;
        note(rejOk, QStringLiteral("G5p parseReq rejects REQX prefix (got=%1 exp=%2)")
                        .arg(gotReject).arg(expG5pReject));
        note(accOk, QStringLiteral("G5p parseReq still accepts well-formed REQ (got=%1 exp=%2)")
                        .arg(gotAccept).arg(expG5pAccept));
        bool wireOk = false;
        {
            FaceAiSession s;
            QByteArray line, resp;
            const bool started = faceAiSpawn(s, stub, QByteArray());
            const bool readyOk = started && faceAiReadLine(s.proc, s.buf, line, 5000)
                              && line == QByteArray("READY 1");
            s.proc.write(QByteArrayLiteral("REQX 5 det 4\ntest"));
            const bool wrote = s.proc.waitForBytesWritten(3000);
            const bool got = readyOk && wrote && faceAiReadLine(s.proc, s.buf, resp, 5000);
            wireOk = got && resp == expG5pWire;
            note(wireOk, QStringLiteral("G5p stub wire REQX answered badframe, line=%1")
                             .arg(QString::fromLatin1(got ? resp : QByteArray("<none>"))));
            faceAiStop(s);
        }
        const int gotMain = AiSubprocessRunner::sIsMainThread() ? 1 : 0;
        int gotWorker = -1;
        { std::thread th([&] { gotWorker = AiSubprocessRunner::sIsMainThread() ? 1 : 0; }); th.join(); }
        const bool predOk = gotMain == expGpMain && gotWorker == expGpWorker;
        note(predOk, QStringLiteral("G5p sIsMainThread predicate main=%1 worker=%2")
                         .arg(gotMain).arg(gotWorker));
        std::fprintf(stderr, "LA_FACEAI_G G5p ok=%d\n", int(rejOk && accOk && wireOk && predOk));
    }

    // ---- G6 [T4a] HANG -> Timeout -> 自动重启一次 -> 复绿（spec §2.2 重启机械 +
    //   T1 评审遗留#2 崩溃感知读逃逸）：桩收 REQ 后睡 90s 不回帧，底座必须 1500ms 预算
    //   内 Timeout 且拔换卡死子进程（未应答 REQ 流位不可信）；钩子一次性消耗 => 第二
    //   call 的重启子进程干净应答 Ok + 全量 JSON 复绿。----
    {
        qputenv("LA_FACE_FORCE_HANG", "1");    // 布防：首 spawn 注入并消耗，重启臂不再挂起（桩分支门不分支，铁律22）
        AiSubprocessRunner r;
        r.configure({stub}, {}, 5000, QStringLiteral("LA_FACE_FORCE_HANG"));
        const auto rp1 = faceAiCall(r, QStringLiteral("det"), 31, reqPath, 1500);
        const bool toOk = int(rp1.rst) == expG6a;
        note(toOk, QStringLiteral("G6 hung call rst=%1 exp=%2").arg(int(rp1.rst)).arg(expG6a));
        const auto rp2 = faceAiCall(r, QStringLiteral("det"), 32, reqPath, 8000);
        const bool greenOk = int(rp2.rst) == expG6b && rp2.json == expG6bJson;
        note(greenOk, QStringLiteral("G6 restarted call green rst=%1 bytes=%2")
                          .arg(int(rp2.rst)).arg(rp2.json.size()));
        std::fprintf(stderr, "LA_FACEAI_G G6 ok=%d\n", int(toOk && greenOk));
    }
    qputenv("LA_FACE_FORCE_HANG", QByteArray());

    // ---- G7 [T4a] 并发单飞拒绝（spec §2.2 单飞不变式）：桩帧间 3s 睡眠造在飞窗口；
    //   第一路（worker A）持闸完成 Ok，窗口内第二路（worker B）tryLock 失败即 Busy——
    //   不排队、不死锁。窗口余量 3s vs 300ms 布防等待，慢机亦稳。----
    {
        qputenv("LA_FACE_FORCE_SLOW", "1");
        AiSubprocessRunner r;
        r.configure({stub}, {}, 5000, QStringLiteral("LA_FACE_FORCE_SLOW"));
        AiSubprocessRunner::Reply rpA;
        std::atomic<bool> aStarted{false};
        std::thread aTh([&] { aStarted.store(true); rpA = r.call(QStringLiteral("det"), 41, reqPath, 12000); });
        while (!aStarted.load()) QThread::msleep(5);   // 等 A 进 call（拿闸在 store 之后数 ns，300ms 后必在飞）
        QThread::msleep(300);
        const auto rpB = faceAiCall(r, QStringLiteral("det"), 42, reqPath, 5000);
        aTh.join();
        const bool busyOk = int(rpB.rst) == expG7b;
        const bool aOk = int(rpA.rst) == expG7a && rpA.json == expectBody;
        note(busyOk, QStringLiteral("G7 second concurrent caller rst=%1 exp=%2")
                         .arg(int(rpB.rst)).arg(expG7b));
        note(aOk, QStringLiteral("G7 first caller completes rst=%1").arg(int(rpA.rst)));
        std::fprintf(stderr, "LA_FACEAI_G G7 ok=%d\n", int(busyOk && aOk));
    }
    qputenv("LA_FACE_FORCE_SLOW", QByteArray());

    // ---- G8 [T4a] exe 候选全缺 -> NoExe（spec §2.2 exe_missing 结构化失败）：
    //   ensureReady 与 call 一致判 NoExe，绝不 spawn、绝不误报模型态/超时。----
    {
        AiSubprocessRunner r;
        r.configure({stub + QStringLiteral(".missing_a.exe"),
                     stub + QStringLiteral(".missing_b.exe")}, {}, 5000, QString());
        Rst st = Rst::Err;
        const bool pOk = faceAiProbe(r, &st);
        const bool readyNoExe = !pOk && int(st) == expG8Ready;
        note(readyNoExe, QStringLiteral("G8 ensureReady exe_missing ok=%1 rst=%2 exp=%3")
                             .arg(int(pOk)).arg(int(st)).arg(expG8Ready));
        const auto rp = faceAiCall(r, QStringLiteral("det"), 51, reqPath, 3000);
        const bool callNoExe = int(rp.rst) == expG8Call;
        note(callNoExe, QStringLiteral("G8 call exe_missing rst=%1 exp=%2")
                            .arg(int(rp.rst)).arg(expG8Call));
        std::fprintf(stderr, "LA_FACEAI_G G8 ok=%d\n", int(readyNoExe && callNoExe));
    }

    // ---- G9 [T12 修复波 FIX-8 建立 / FIX-16 加反例子轮] face 轨「子进程驱动的生产路径腿」
    //      （照抄 G11 结构，**不吃合成钩子**；同进程内先跑「全失败轮」再跑「干净轮」）：
    //      PersonDetector::detect → FaceRunner::probe/detect 门面 → AiSubprocessRunner 底座 →
    //      faceinfer 子进程（qputenv LA_FACE_STUB=1 走门面候选链第 1 跳桩，与 G11 的 LA_CLIP_STUB
    //      同构；桩文件头 E1 组「runner 侧强制选桩标记，T4 接线消费」，本腿即该消费点）。
    //      为什么必须存在：#70 全程 setSyntheticForQa 绕过传输、#75 的 G1..G8 只驱动底座本身——
    //      T4b 净替换掉的正是 PersonDetector 的传输段，其真实通路（逐张 REQ kind=det → RES JSON →
    //      Library::setPersons 的 person 列写回）此前无任何门禁覆盖。桩对任意 body 恒答 faces
    //      count=1（与 G3 期望体同源），故「person 列出现 defaultPerson」只可能来自一次真往返。
    //      ⚠ 与 G11 的两处 face 特有分歧（据实断言，绝不照搬 CLIP 判据）：
    //        (1) PersonDetector **无缩略图闸**：pending 过滤只认 person.isEmpty()（PersonDetector.cpp:54），
    //            每张未标注照片（含 p5 无缩略图）都发 REQ、桩都答 count=1 → 都写回。G11 的
    //            「无缩略图不发请求也不写回」在 face 轨**不成立**，故 p5 仍被写回，本腿据此**正向**
    //            断言（证明 face 轨逐张真驱动子进程、无 thumb 捷径）——这是有意据实断言，非放宽。
    //        (2) detect 返回值诚实化后与 CLIP 轨 Classifier::classify 的 F-1 契约**同型**
    //            （T12 修复波第 2 轮 FIX-14：补 spec §8「收尾汇总计数」那一列在 face 轨的落地）：
    //            逐张传输失败计 failed、载荷 ok!=true 计 failed、取消置 stopped，收尾
    //            return failed == 0 && !stopped。于是「全失败轮 false」由 LA_FACE_FORCE_ERR 直接
    //            物理注入（READY 1 ⇒ probe 仍成功 ⇒ 每 REQ ERR loadfail ⇒ 只有**汇总计数**能把这轮
    //            判成 false），不再借道 LA_FACE_FORCE_NOMODEL：NOMODEL 的归因只在 probe 层（G2/G8），
    //            拿它咬返回值会把「probe 假」误当「全失败诚实」，两条契约混成一条。本臂对未修
    //            实现（每 REQ 失败只 continue、收尾恒 return true）实测为红——那红就是缺陷本体。
    //      流程恒在、期望随臂翻转（铁律22 零分支）：ERR 子轮与干净子轮**都无条件跑**（布防的是
    //      夹具层的桩模式，不是分支），tamper 只把期望常量翻错（errRet/写回数/p5 标签/干净轮
    //      ret+标签），两臂的退出路径完全一致。
    //      两轮同进程 ⇒ 必须换血：桩的 LA_FACE_FORCE_* 只在**子进程启动时**读一次
    //      （faceinfer_stub.cpp 主循环前 envOn 五连），而底座按设计复用热子进程
    //      （AiSubprocessRunner::startIfNeeded 的 Sp::Up 复用臂）——故 ERR 轮跑完要用
    //      FaceRunner::qaRetireChildForQa() 拆掉那具带着 FORCE_ERR env 的子进程，干净轮才会
    //      冷启动一具无钩子的新子进程。这是夹具层进程寿命问题，不是被测实现的缺陷。
    //      顺序是硬约束：ERR 轮先跑、且两轮写回断言各自独立——干净轮一旦把 person 写回，
    //      「全失败轮零写回」这条判据就失去可分辨性。
    //      两臂互锁：正臂 = 全失败轮 ret false + 零写回 + p5 仍空，干净轮 ret true + kPending 张
    //      写回 defaultPerson；tamper 臂逐一翻错期望。恒真实现被 ERR 子轮咬红、恒假实现被干净轮
    //      咬红，返回值真值表与写回/盘面两路各自独立成立。----
    {
        const QString dir = QDir::tempPath() + QStringLiteral("/la_faceai_g9_%1")
                                .arg(QCoreApplication::applicationPid());
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        const QString dbPath = QDir(dir).filePath(QStringLiteral("g9.db"));
        // 名册：p0..p4 未标注（有缩略图占位）；p5 未标注但**无缩略图**（face 轨无 thumb 闸→仍写回）；
        //       p6 已标注=「老王」（守恒锚：detect 只认 person.isEmpty，绝不再发 REQ/覆盖）。
        const int kPending = 6;      // p0..p5（含无缩略图 p5）
        const int kRows    = 7;      // 含 p6 已标注
        const QString seededPerson = QStringLiteral("老王");   // 「已标注不得被覆盖」守恒锚
        const QString dp = PersonDetector::defaultPerson();    // 干净臂写回目标（与实现同源，避免两处转义劈叉）
        // 临时目录自造 4x4 合成 PNG（绝不引用户照片）；face 轨 REQ body=图像绝对路径，
        // 桩按 count=1 恒答、不读盘，但用真 PNG 满足「只用合成图」纪律。
        bool pngsOk = true;
        QStringList pendingPaths;
        for (int i = 0; i < kPending; ++i) {
            const QString png = QDir(dir).filePath(QStringLiteral("pending_%1.png").arg(i));
            QImage im(4, 4, QImage::Format_RGB32);
            im.fill(QColor(30 + i * 10, 90, 150));
            if (!im.save(png, "PNG")) { pngsOk = false; break; }
            pendingPaths << png;
        }
        note(pngsOk, QStringLiteral("G9 self-made PNG fixture staging failed under %1").arg(dir));
        bool seeded = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("la_faceai_g9_seed"));
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral(
                    "CREATE TABLE photos (path TEXT PRIMARY KEY, rel TEXT, thumb TEXT, "
                    "missing INTEGER DEFAULT 0, exif_json TEXT, category TEXT, person TEXT, "
                    "private INTEGER DEFAULT 0, rating INTEGER DEFAULT 0, mark TEXT)"));
                q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)"));
                bool allRows = true;
                for (int i = 0; i < kRows; ++i) {
                    // 列/占位符严格对齐 Library::open 的 SELECT（形参照抄 G11 的 seed 语句）。
                    q.prepare(QStringLiteral(
                        "INSERT INTO photos(path,rel,thumb,missing,exif_json,category,person,private,rating,mark) "
                        "VALUES(?,?,?,0,?,?,?,0,0,?)"));
                    const QString path = i < kPending
                        ? pendingPaths.value(i, QStringLiteral("C:/la/g9_missing_%1.jpg").arg(i))
                        : QStringLiteral("C:/la/p6_conservative.jpg");
                    q.addBindValue(path);                                                    // path
                    q.addBindValue(QStringLiteral("pending_%1.png").arg(i));                 // rel
                    q.addBindValue(i == 5 ? QString() : QStringLiteral("t%1.jpg").arg(i));   // p5 无缩略图
                    q.addBindValue(QStringLiteral("{}"));                                    // exif_json
                    q.addBindValue(QString());                                               // category（face 轨不碰）
                    q.addBindValue(i == 6 ? seededPerson : QString());                       // person：p6 已标注
                    q.addBindValue(QString());                                               // mark
                    if (!q.exec()) allRows = false;
                }
                db.close();
                seeded = allRows;
            }
            QSqlDatabase::removeDatabase(QStringLiteral("la_faceai_g9_seed"));
        }
        note(seeded, QStringLiteral("G9 temp library seed failed"));

        Library lib;
        QString operr;
        const bool opened = seeded && lib.open(dbPath, &operr);
        note(opened, QStringLiteral("G9 library open=%1").arg(operr));

        // 生产侧桩标记：布防 + 用毕显式解除（与 G3/G6/G7/G11 同式）。
        qputenv("LA_FACE_STUB", "1");
        // 期望表（固定常量；两轮物理流程恒在，tamper 只翻期望常量，流程零分支，铁律22）。
        const QString expG9Person = tamper ? QStringLiteral("错误标注") : dp;
        const bool    expG9Ret    = !tamper;   // 干净轮恒 true；反例臂把它翻成 false 即咬红
        // ERR 子轮（全失败轮）期望：诚实实现必回 false 且**零写回**（person 保持空）。
        const bool expG9ErrRet   = tamper;                          // 正臂 false / 反例臂翻成 true
        const int  expG9ErrWrote = tamper ? kPending : 0;           // 正臂 0 / 反例臂翻成 kPending
        const QString expG9ErrP5 = tamper ? expG9Person : QString();// 正臂：p5 仍空
        // 取消子轮（FIX-19 反例臂）期望：诚实实现必回 false（stopped 置位）且**零写回**。
        // 断的是 `stopped` 那半——只测 `failed` 的话，删掉 `stopped = true` 仍全绿，
        // 而「用户按了取消也报成功」这条谎就重新溜回来（CHANGELOG/指导书均已宣称被杀）。
        const bool expG9CancelRet   = tamper;                        // 正臂 false / 反例臂 true
        const int  expG9CancelWrote = tamper ? kPending : 0;         // 正臂 0 / 反例臂翻成 kPending
        // embedding 轨（①c/①d）期望：detectEmbeddings 的 false 语义此前零门禁覆盖
        //（聚类门 #70 走合成钩子，函数首行早退恒 true，真路径一次都没被压过）。
        // 同口径：全失败轮必回 false 且零 face_emb 写回；取消轮必回 false。
        const bool expG9EmbErrRet    = tamper;                       // 正臂 false / 反例臂 true
        const bool expG9EmbCancelRet = tamper;                       // 正臂 false / 反例臂 true
        const int  expG9EmbWrote     = tamper ? kPending : 0;        // 正臂 0 行 face_emb
        bool detectRet = false;
        bool errRet = false;
        bool cancelRet = true;
        int  wroteExpectedErr = 0;
        int  wroteExpectedCancel = 0;
        // 归因腿（复审 Minor「ERR 臂只靠同轮干净臂间接钉住」）：progress 的 done/total
        // 轨迹能把「逐张失败计入」与「probe 假 → 早早返回 false」区分开——后者只发
        // progress(0,total) 一次，done 永远停在 0。
        int  errDoneMax = -1;     // ERR 轮 progress 见过的最大 done
        int  errTotal = -1;       // ERR 轮 progress 报出的 total
        int  cancelDoneMax = -1;  // 取消轮 progress 见过的最大 done（应为 0：i==0 即 break）
        // embedding 轨（①c/①d）同名量
        bool embErrRet = true;
        bool embCancelRet = true;
        int  embErrDoneMax = -1;
        int  embErrTotal = -1;
        int  embCancelDoneMax = -1;
        int  embRowsAfterBadRounds = -1;
        // ①e「有脸无向量」腿（复审 Minor「PersonDetector 静默丢弃 embedding 为空的应答」）：
        // 该支改 ++failed 后若无门咬住，就等于「修了但没人能证明它还是这样」。桩用
        // LA_FACE_FORCE_NOEMB 回 ok:true + faces 非空 + embedding:[]，物理注入这条载荷。
        bool embNoEmbRet = true;
        int  embNoEmbDoneMax = -1;
        int  embNoEmbTotal = -1;
        int  embRowsAfterNoEmb = -1;
        QString p5personErr;
        if (opened && pngsOk) {
            QObject qaCtx;                       // 写回投递目标在主线程（AiWriter QueuedConnection 落点）
            AiWriter writer(&lib, &qaCtx);
            // 快照取在 QA 主线程：Library 非线程安全，生产侧 MainWindow::runPersonDetect 同款
            // （worker 只吃快照，绝不回头碰 lib）。
            const QVector<Photo> snap = lib.photos();
            // 前置条件：两轮都必须跑在非主线程（PersonDetector.h §线程 + spec §2.2：底座
            // 对主线程调用响亮拒 Busy）。用 runOffMainQa（std::thread+join，与本门 faceAiCall/
            // faceAiProbe 同款）而**不用** QtConcurrent::run——见其声明注里的实测证据。
            // ---- ① ERR 子轮（FIX-16 反例臂）：物理注入 READY 1 + 每 REQ ERR loadfail
            //      ⇒ 本轮 kPending 张**全失败、零写回**。诚实实现收尾必回 false；
            //      「每 REQ 失败只 continue、收尾恒 return true」的现实现即在此咬红。
            qputenv("LA_FACE_FORCE_ERR", "1");   // 全失败轮布防（用毕下句解除）
            runOffMainQa([&] {
                errRet = PersonDetector::detect(snap, writer, -1,
                    [&](int done, int total, const QString&) {
                        errDoneMax = qMax(errDoneMax, done);
                        errTotal = total;
                    }, [] { return false; });
            });
            qputenv("LA_FACE_FORCE_ERR", QByteArray());
            // 写回经 AiWriter 的 QueuedConnection 投递回本线程落地（#70/G11 同式）。
            QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
            // ERR 子轮读回**必须在干净轮之前**：写回断言两轮各自独立。
            for (int i = 0; i < kPending; ++i) {
                const QString& want = pendingPaths.value(i);
                for (const Photo& ph : lib.photos())
                    if (ph.path == want && ph.person == expG9Person) ++wroteExpectedErr;
            }
            if (pendingPaths.size() == kPending)
                for (const Photo& ph : lib.photos())
                    if (ph.path == pendingPaths[5]) p5personErr = ph.person;
            // ---- ①b 取消子轮（FIX-19）：cancel 谓词恒真 ⇒ 循环在 i==0 即 break，
            //      一张都不发请求、一张都不写回，收尾必回 false（stopped 那半被咬住）。
            //      位置是硬约束：必须在干净轮之前（干净轮一写回 person，这里的
            //      「零写回」断言就退化成恒真）；也必须在 FORCE_ERR 解除之后
            //      （布防时 probe 仍真、但语义会被 ERR 臂混叠，独立成一臂更干净）。
            runOffMainQa([&] {
                cancelRet = PersonDetector::detect(snap, writer, -1,
                    [&](int done, int, const QString&) {
                        cancelDoneMax = qMax(cancelDoneMax, done);
                    }, [] { return true; });
            });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
            for (int i = 0; i < kPending; ++i) {
                const QString& want = pendingPaths.value(i);
                for (const Photo& ph : lib.photos())
                    if (ph.path == want && ph.person == expG9Person) ++wroteExpectedCancel;
            }
            // ---- ①c/①d embedding 轨（detectEmbeddings）同构两臂 ----
            //   复审裁决定位：合成钩子支（setSyntheticEmbForQa）在函数首行早退，恒回 true，
            //   所以聚类轨的 false 语义此前**没有任何门禁腿**——#75 G9 只咬 detect()。
            //   这里用同一具 ERR 子进程（① 布防时冷启动那具，FORCE_ERR 只在进程启动读一次，
            //   故必须在 retire 之前跑）把 embedding 轨的真路径也压过一遍：
            //   ①c 全失败轮 ⇒ false 且零 face_emb 写回；①d 取消轮 ⇒ false 且零请求。
            runOffMainQa([&] {
                embErrRet = PersonDetector::detectEmbeddings(snap, writer, -1,
                    [&](int done, int total, const QString&) {
                        embErrDoneMax = qMax(embErrDoneMax, done);
                        embErrTotal = total;
                    }, [] { return false; });
            });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
            runOffMainQa([&] {
                embCancelRet = PersonDetector::detectEmbeddings(snap, writer, -1,
                    [&](int done, int, const QString&) {
                        embCancelDoneMax = qMax(embCancelDoneMax, done);
                    }, [] { return true; });
            });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
            embRowsAfterBadRounds = lib.getFaceEmbeddings().size();
            // 换血（夹具层进程寿命修正，非被测语义）：桩的 LA_FACE_FORCE_* 只在子进程
            // 启动时读一次，底座又复用热子进程（且 ERR 帧按 spec §5 保活不杀），不拆掉
            // 这具 ERR 子进程，干净轮会继承 FORCE_ERR 而永远拿不到 faces（见
            // FaceRunner::qaRetireChildForQa 声明注）。
            FaceRunner::qaRetireChildForQa();
            // ---- ①e 有脸无 embedding 子轮（FIX-21）：换一具 NOEMB 子进程，每 REQ 回
            //      ok:true + 一张脸 + embedding:[]。这一支在 embedding 轨的语义是
            //      「载荷残缺 = 该张失败」，不是「没脸」：诚实实现必回 false 且零 face_emb。
            //      位置同样是硬约束：必须在 retire 之后（ERR 子进程会先答 ERR，NOEMB 到不了
            //      载荷层）、干净轮之前（干净轮一写 face_emb，「零写回」就退化成恒真）。
            qputenv("LA_FACE_FORCE_NOEMB", "1");
            runOffMainQa([&] {
                embNoEmbRet = PersonDetector::detectEmbeddings(snap, writer, -1,
                    [&](int done, int total, const QString&) {
                        embNoEmbDoneMax = qMax(embNoEmbDoneMax, done);
                        embNoEmbTotal = total;
                    }, [] { return false; });
            });
            qputenv("LA_FACE_FORCE_NOEMB", QByteArray());
            QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
            embRowsAfterNoEmb = lib.getFaceEmbeddings().size();
            FaceRunner::qaRetireChildForQa();
            // ---- ② 干净子轮：无钩子冷启动的新子进程每 REQ 恒答 faces count=1 ----
            runOffMainQa([&] {
                detectRet = PersonDetector::detect(snap, writer, -1,
                    [](int, int, const QString&) {}, [] { return false; });
            });
            QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
        }
        qputenv("LA_FACE_STUB", QByteArray());

        // ---- ① ERR 子轮断言（返回值诚实 + 零写回 + 守恒）----
        note(errRet == expG9ErrRet,
             QStringLiteral("G9 ERR-round detect ret=%1 want=%2 (all-failed round must report false)")
                 .arg(int(errRet)).arg(int(expG9ErrRet)));
        note(wroteExpectedErr == expG9ErrWrote,
             QStringLiteral("G9 ERR-round writeback=%1 want=%2 (all-failed round must write nothing, label=%3)")
                 .arg(wroteExpectedErr).arg(expG9ErrWrote).arg(expG9Person));
        note(p5personErr == expG9ErrP5,
             QStringLiteral("G9 ERR-round p5 person=%1 want=%2 (no writeback under FORCE_ERR)")
                 .arg(p5personErr, expG9ErrP5));
        // 已标注张 p6 在全失败轮也不得被动（守恒）。
        QString p6personErr;
        for (const Photo& ph : lib.photos())
            if (ph.path == QStringLiteral("C:/la/p6_conservative.jpg")) p6personErr = ph.person;
        note(p6personErr == seededPerson,
             QStringLiteral("G9 ERR-round p6 overwritten=%1 want=%2").arg(p6personErr, seededPerson));
        // 归因腿（正向不变量，两臂同值 ⇒ 不随 tamper 翻）：false 必须来自「逐张计入处置」，
        // 不是 probe 假早退（那条路 progress 只发一次 done=0，这两条即咬红）。
        note(errTotal == kPending,
             QStringLiteral("G9 ERR-round progress total=%1 want=%2 (round must enumerate all pending photos)")
                 .arg(errTotal).arg(kPending));
        note(errDoneMax == kPending,
             QStringLiteral("G9 ERR-round progress done=%1 want=%2 (each failed photo must be counted toward the tally)")
                 .arg(errDoneMax).arg(kPending));

        // ---- ①b 取消子轮断言（`stopped` 那半的判别腿）----
        note(cancelRet == expG9CancelRet,
             QStringLiteral("G9 cancel-round detect ret=%1 want=%2 (cancelled round must report false)")
                 .arg(int(cancelRet)).arg(int(expG9CancelRet)));
        note(wroteExpectedCancel == expG9CancelWrote,
             QStringLiteral("G9 cancel-round writeback=%1 want=%2 (cancelled round must write nothing, label=%3)")
                 .arg(wroteExpectedCancel).arg(expG9CancelWrote).arg(expG9Person));
        // 取消轮的归因腿：progress 只能见过开场那次 done=0（i==0 即 break，一张请求都没发）。
        // 若 break 支被挪到请求之后，done 会爬到 ≥1，这条即红——把「取消=零请求」钉住。
        note(cancelDoneMax == 0,
             QStringLiteral("G9 cancel-round progress done=%1 want=0 (cancel must stop before the first request)")
                 .arg(cancelDoneMax));

        // ---- ①c/①d embedding 轨断言（聚类轨真路径的首次覆盖）----
        note(embErrRet == expG9EmbErrRet,
             QStringLiteral("G9 emb ERR-round detectEmbeddings ret=%1 want=%2 (all-failed round must report false)")
                 .arg(int(embErrRet)).arg(int(expG9EmbErrRet)));
        note(embCancelRet == expG9EmbCancelRet,
             QStringLiteral("G9 emb cancel-round detectEmbeddings ret=%1 want=%2 (cancelled round must report false)")
                 .arg(int(embCancelRet)).arg(int(expG9EmbCancelRet)));
        note(embRowsAfterBadRounds == expG9EmbWrote,
             QStringLiteral("G9 emb bad-rounds face_emb rows=%1 want=%2 (failed/cancelled rounds must write nothing)")
                 .arg(embRowsAfterBadRounds).arg(expG9EmbWrote));
        // embedding 轨归因腿（同 detect 口径的正向不变量）：false 来自逐张计入处置。
        note(embErrTotal == kPending,
             QStringLiteral("G9 emb ERR-round progress total=%1 want=%2 (round must enumerate all pending photos)")
                 .arg(embErrTotal).arg(kPending));
        note(embErrDoneMax == kPending,
             QStringLiteral("G9 emb ERR-round progress done=%1 want=%2 (each failed photo must be counted toward the tally)")
                 .arg(embErrDoneMax).arg(kPending));
        note(embCancelDoneMax == 0,
             QStringLiteral("G9 emb cancel-round progress done=%1 want=0 (cancel must stop before the first request)")
                 .arg(embCancelDoneMax));

        // ---- ①e 有脸无 embedding 断言（载荷残缺=失败；正向不变量，两臂同值不随 tamper 翻）----
        note(embNoEmbRet == false,
             QStringLiteral("G9 emb NOEMB-round detectEmbeddings ret=%1 want=0 (face without embedding is a broken payload, not a miss)")
                 .arg(int(embNoEmbRet)));
        note(embRowsAfterNoEmb == 0,
             QStringLiteral("G9 emb NOEMB-round face_emb rows=%1 want=0 (broken payload must write nothing)")
                 .arg(embRowsAfterNoEmb));
        note(embNoEmbTotal == kPending,
             QStringLiteral("G9 emb NOEMB-round progress total=%1 want=%2 (round must enumerate all pending photos)")
                 .arg(embNoEmbTotal).arg(kPending));
        note(embNoEmbDoneMax == kPending,
             QStringLiteral("G9 emb NOEMB-round progress done=%1 want=%2 (each broken-payload photo must be counted toward the tally)")
                 .arg(embNoEmbDoneMax).arg(kPending));

        // ---- ② 干净子轮断言 ----
        note(detectRet == expG9Ret,
             QStringLiteral("G9 clean-round ret=%1 want=%2 (clean round true / tamper flips)")
                 .arg(int(detectRet)).arg(int(expG9Ret)));
        // person 列写回落点（内存）：kPending 张 = 期望标签（干净=defaultPerson）。
        int wroteExpected = 0;
        for (int i = 0; i < kPending; ++i) {
            const QString& want = pendingPaths.value(i);
            for (const Photo& ph : lib.photos())
                if (ph.path == want && ph.person == expG9Person) ++wroteExpected;
        }
        note(wroteExpected == kPending,
             QStringLiteral("G9 person writeback=%1 want=%2 label=%3")
                 .arg(wroteExpected).arg(kPending).arg(expG9Person));
        // face 轨无 thumb 闸：无缩略图张 p5 也被写回（正向断言，与 G11 的 p7 恰好相反）。
        QString p5person;
        if (pendingPaths.size() == kPending)
            for (const Photo& ph : lib.photos())
                if (ph.path == pendingPaths[5]) p5person = ph.person;
        note(p5person == expG9Person,
             QStringLiteral("G9 p5 (no thumb) person=%1 want=%2 (face track has no thumb gate)")
                 .arg(p5person, expG9Person));
        // 已标注张 p6 不被覆盖（守恒）。
        QString p6person;
        for (const Photo& ph : lib.photos())
            if (ph.path == QStringLiteral("C:/la/p6_conservative.jpg")) p6person = ph.person;
        note(p6person == seededPerson,
             QStringLiteral("G9 p6 (already labeled) overwritten=%1 want=%2").arg(p6person, seededPerson));
        note(lib.photos().size() == kRows,
             QStringLiteral("G9 conserve rows=%1 want=%2").arg(lib.photos().size()).arg(kRows));
        // 落盘面（独立连接读磁盘）：内存对了不算落地，setPersons 的 UPDATE person 必须由磁盘复核。
        {
            QString dbP0 = QStringLiteral("__unreadable__");
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("la_faceai_g9_read"));
            db.setDatabaseName(dbPath);
            if (db.open()) {
                if (pendingPaths.size() == kPending) {
                    QSqlQuery q(db);
                    q.prepare(QStringLiteral("SELECT person FROM photos WHERE path=?"));
                    q.addBindValue(pendingPaths[0]);
                    if (q.exec() && q.next()) dbP0 = q.value(0).toString();
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("la_faceai_g9_read"));
            note(dbP0 == expG9Person,
                 QStringLiteral("G9 db p0 person=%1 want=%2").arg(dbP0, expG9Person));
        }
        // 汇总旗标必须覆盖**全部**子臂（复审裁：只并 ERR+干净两臂的话，取消臂与
        // embedding 臂咬红时这行仍打 ok=1，等于给新腿发了个不会变红的通行证）。
        // 正向不变量腿（progress 轨迹）一并入旗：探针实测证明「取消检查挪到请求之后」
        // 这一变异**只**红 done=0 那条（ret/写回两腿因 ERR 子进程复用而照旧为假），
        // 旗标若不含它，就恰好漏掉唯一有牙齿的那条腿。
        const bool g9Ok = errRet == expG9ErrRet && wroteExpectedErr == expG9ErrWrote
                          && p5personErr == expG9ErrP5 && p6personErr == seededPerson
                          && errTotal == kPending && errDoneMax == kPending
                          && cancelRet == expG9CancelRet && wroteExpectedCancel == expG9CancelWrote
                          && cancelDoneMax == 0
                          && embErrRet == expG9EmbErrRet && embCancelRet == expG9EmbCancelRet
                          && embRowsAfterBadRounds == expG9EmbWrote
                          && embErrTotal == kPending && embErrDoneMax == kPending
                          && embCancelDoneMax == 0
                          && embNoEmbRet == false && embRowsAfterNoEmb == 0
                          && embNoEmbTotal == kPending && embNoEmbDoneMax == kPending
                          && detectRet == expG9Ret && wroteExpected == kPending
                          && p5person == expG9Person && p6person == seededPerson
                          && lib.photos().size() == kRows;
        std::fprintf(stderr,
                     "LA_FACEAI_G G9 ok=%d ret=%d wrote=%d errRet=%d wroteErr=%d "
                     "cancelRet=%d wroteCancel=%d embErrRet=%d embCancelRet=%d embRows=%d "
                     "noEmbRet=%d embRowsNoEmb=%d\n",
                     int(g9Ok), int(detectRet), wroteExpected, int(errRet), wroteExpectedErr,
                     int(cancelRet), wroteExpectedCancel, int(embErrRet), int(embCancelRet),
                     embRowsAfterBadRounds, int(embNoEmbRet), embRowsAfterNoEmb);
        // R-06 卫生：门后必删临时目录。⚠ 必须先摘掉 Library 留下的两条全局命名连接
        // （la_rw + la_lib）——连接句柄活在全局池里，不受本腿 lib 作用域/析构影响；Windows
        // 上被打开的 sqlite 文件删不掉，于是 removeRecursively 只删得掉 png、独独留下 g9.db。
        for (const QString& conn : {QStringLiteral("la_rw"), QStringLiteral("la_lib")}) {
            if (QSqlDatabase::contains(conn)) {
                {
                    QSqlDatabase db = QSqlDatabase::database(conn);
                    db.close();
                }
                QSqlDatabase::removeDatabase(conn);
            }
        }
        QDir(dir).removeRecursively();
    }

    if (fails.isEmpty())
        std::fprintf(stderr, "LA_FACE_AI_QA PASS tamper=%d g1=1 g2=1 g3=1 g4=1 g5=1 g5p=1 g6=1 g7=1 g8=1 g9=1\n", int(tamper));
    else
        std::fprintf(stderr, "LA_FACE_AI_QA FAIL tamper=%d reason=%s\n",
                     int(tamper), qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
}

// ---------------- 第四轮 T3 第 78 门 LA_FACE_MODEL_QA：人脸模型端到端五列对表门（full 层，opt-in） ----------------
// 真 faceinfer.exe（det+rec 双会话）直起（本任务无 runner，线程机制归 T4/#75 链路，此处纯同步
// QProcess 逐 REQ 应答）；对 T2 金标注 JSON 断言 spec §3 五列全表：
//   人脸数精确相等；逐脸贪心 1:1 IoU ≥ 0.98；kps 五点欧距均值 ≤ 2.0 原图像素；
//   embedding 余弦 ≥ 0.999；|scoreΔ| ≤ 0.02。比较核与 faceinfer --face-selftest 同口径。
// 隐私红线 R-06：真人图像与 gold JSON 永不入仓——门只从 env 路径读取：
//   LA_FACE_MODEL = 模型目录（detection.onnx + recognition.onnx）；
//   LA_FACE_IMG   = 分号分隔的图像文件或目录（jpg/jpeg/png/bmp/webp，目录取 Name 序）。
// env 缺席 = qa_runner need= 机制自动 SKIP； opted-in 而前置缺失（exe/模型/图像/gold/READY≠1）
// = 诚实 FAIL（铁律34，禁静默放行）。
// 反例 LA_FACE_MODEL_QA_FORCE_BROKEN=1 只篡改期望表数据（bbox 右界 +30% 宽、emb[0] +10.0），
// 比较流程与退出路径零分支（铁律22/34）：IoU/COS 两列必红，exit 走同一 fails 判据。
// stderr 打印 READY ms 供 spec 附录A 回填。
struct FmFace
{
    float box[4] = {0.f, 0.f, 0.f, 0.f};
    float kps[5][2] = {};
    float score = 0.f;
    QVector<float> emb;
};

static QString faceModelFindExe()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/faceinfer.exe",                                        // dev：native/build 扁平产物（POST_BUILD 拷机）
        QDir::cleanPath(appDir + "/../../faceinfer/build/faceinfer.exe"),  // dev build tree 源
    };
    for (const QString& p : candidates) if (QFileInfo::exists(p)) return p;
    return QString();
}

static double fmIou(const float a[4], const float b[4])
{
    const double ix1 = std::max(double(a[0]), double(b[0]));
    const double iy1 = std::max(double(a[1]), double(b[1]));
    const double ix2 = std::min(double(a[2]), double(b[2]));
    const double iy2 = std::min(double(a[3]), double(b[3]));
    const double inter = ix2 > ix1 && iy2 > iy1 ? (ix2 - ix1) * (iy2 - iy1) : 0.0;
    const double arA = (double(a[2]) - a[0]) * (double(a[3]) - a[1]);
    const double arB = (double(b[2]) - b[0]) * (double(b[3]) - b[1]);
    const double un = arA + arB - inter;
    return un > 0.0 ? inter / un : 0.0;
}

static double fmKpsMean(const float g[5][2], const float m[5][2])
{
    double s = 0.0;
    for (int k = 0; k < 5; ++k) {
        const double dx = double(g[k][0]) - double(m[k][0]);
        const double dy = double(g[k][1]) - double(m[k][1]);
        s += std::sqrt(dx * dx + dy * dy);
    }
    return s / 5.0;
}

static double fmCos(const QVector<float>& a, const QVector<float>& b)
{
    if (a.size() != b.size() || a.size() != 512) return -2.0;   // 维数不符 = 立即红
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i = 0; i < a.size(); ++i) {
        dot += double(a[i]) * double(b[i]);
        na  += double(a[i]) * double(a[i]);
        nb  += double(b[i]) * double(b[i]);
    }
    if (na <= 0.0 || nb <= 0.0) return -2.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

// faces[] JSON → 表（RES 体与 gold JSON 同 schema：bbox/score/kps/embedding）。
static bool fmParseFaces(const QByteArray& json, QVector<FmFace>& out, QString* err)
{
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (doc.isNull() || !doc.isObject()) {
        if (err) *err = QStringLiteral("json parse failed at offset %1: %2")
            .arg(perr.offset).arg(perr.errorString());
        return false;
    }
    const QJsonArray faces = doc.object().value(QStringLiteral("faces")).toArray();
    for (const QJsonValue& v : faces) {
        const QJsonObject o = v.toObject();
        FmFace f;
        const QJsonArray bb = o.value(QStringLiteral("bbox")).toArray();
        if (bb.size() != 4) { if (err) *err = QStringLiteral("bad bbox"); return false; }
        for (int q = 0; q < 4; ++q) f.box[q] = float(bb[q].toDouble());
        const QJsonArray kk = o.value(QStringLiteral("kps")).toArray();
        if (kk.size() != 5) { if (err) *err = QStringLiteral("bad kps"); return false; }
        for (int k = 0; k < 5; ++k) {
            const QJsonArray pt = kk[k].toArray();
            if (pt.size() != 2) { if (err) *err = QStringLiteral("bad kps pt"); return false; }
            f.kps[k][0] = float(pt[0].toDouble());
            f.kps[k][1] = float(pt[1].toDouble());
        }
        f.score = float(o.value(QStringLiteral("score")).toDouble());
        const QJsonArray ee = o.value(QStringLiteral("embedding")).toArray();
        f.emb.reserve(ee.size());
        for (const QJsonValue& e : ee) f.emb.append(float(e.toDouble()));
        out.append(f);
    }
    return true;
}

// 五列断言（期望表 vs 实测表）；两臂共用同一函数，tamper 只作用于传入的期望表数据（铁律22）。
static bool fmCompareImage(const QVector<FmFace>& gold, const QVector<FmFace>& mine,
                           QStringList& fails, const QString& tag)
{
    bool allOk = true;
    if (gold.size() != mine.size()) {
        fails << QStringLiteral("%1 count gold=%2 mine=%3").arg(tag).arg(gold.size()).arg(mine.size());
        return false;
    }
    QVector<bool> used(mine.size(), false);
    for (int j = 0; j < gold.size(); ++j) {
        int best = -1; double bestIou = -1.0;
        for (int k = 0; k < mine.size(); ++k) {
            if (used[k]) continue;
            const double v = fmIou(gold[j].box, mine[k].box);
            if (v > bestIou) { bestIou = v; best = k; }
        }
        if (best < 0) {
            fails << QStringLiteral("%1 face%2 no candidate").arg(tag).arg(j);
            allOk = false;
            continue;
        }
        used[best] = true;
        const double kd = fmKpsMean(gold[j].kps, mine[best].kps);
        const double sd = std::fabs(double(gold[j].score) - double(mine[best].score));
        const double cos = fmCos(gold[j].emb, mine[best].emb);
        const bool ok = bestIou >= 0.98 && kd <= 2.0 && sd <= 0.02 && cos >= 0.999;
        if (!ok) {
            fails << QStringLiteral("%1 face%2 IOU=%3 KPS=%4 SCORE_D=%5 COS=%6")
                         .arg(tag).arg(j)
                         .arg(bestIou, 0, 'f', 4).arg(kd, 0, 'f', 3)
                         .arg(sd, 0, 'f', 5).arg(cos, 0, 'f', 6);
            allOk = false;
        }
    }
    return allOk;
}

int runFaceModelQa()
{
    const bool tamper = !qEnvironmentVariableIsEmpty("LA_FACE_MODEL_QA_FORCE_BROKEN");
    QStringList fails;

    const QString exe = faceModelFindExe();
    if (exe.isEmpty()) {
        std::fprintf(stderr, "LA_FACE_MODEL_QA FAIL tamper=%d reason=exe_missing faceinfer.exe -- build faceinfer target first\n", int(tamper));
        std::fflush(stderr);
        return 1;
    }
    const QString modelDir = QString::fromLocal8Bit(qgetenv("LA_FACE_MODEL"));
    if (modelDir.isEmpty() || !QDir(modelDir).exists()) {
        std::fprintf(stderr, "LA_FACE_MODEL_QA FAIL tamper=%d reason=model_dir_missing\n", int(tamper));
        std::fflush(stderr);
        return 1;
    }
    const QString imgSpec = QString::fromLocal8Bit(qgetenv("LA_FACE_IMG"));
    QStringList images;
    for (const QString& part : imgSpec.split(QChar(';'))) {
        const QString p = part.trimmed();
        if (p.isEmpty()) continue;
        const QFileInfo fi(p);
        if (fi.isFile()) {
            images << fi.absoluteFilePath();
        } else if (fi.isDir()) {
            QDir d(p);
            const QStringList filters = { QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                         QStringLiteral("*.png"), QStringLiteral("*.bmp"),
                                         QStringLiteral("*.webp") };
            const QFileInfoList lst = d.entryInfoList(filters, QDir::Files, QDir::Name);
            for (const QFileInfo& e : lst) images << e.absoluteFilePath();
        }
    }
    images.removeDuplicates();
    images.sort();
    if (images.isEmpty()) {
        std::fprintf(stderr, "LA_FACE_MODEL_QA FAIL tamper=%d reason=no_images (LA_FACE_IMG)\n", int(tamper));
        std::fflush(stderr);
        return 1;
    }

    // 真 faceinfer.exe 直起：子端 env 口径 = LA_FACE_MODEL_DIR（模型解析唯一 env）。
    FaceAiSession s;
    {
        QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
        e.insert(QStringLiteral("LA_FACE_MODEL_DIR"), modelDir);
        s.proc.setProcessEnvironment(e);
        s.proc.setProcessChannelMode(QProcess::SeparateChannels);
        s.proc.start(exe, {});
    }
    if (!s.proc.waitForStarted(5000)) {
        std::fprintf(stderr, "LA_FACE_MODEL_QA FAIL tamper=%d reason=spawn_failed\n", int(tamper));
        std::fflush(stderr);
        return 1;
    }
    QElapsedTimer total; total.start();
    QByteArray line;
    const bool gotReady = faceAiReadLine(s.proc, s.buf, line, 90000);   // 真会话冷启动预算
    const qint64 readyMs = total.elapsed();
    if (!gotReady || line != QByteArray("READY 1")) {                   // READY≠1 = 会话未齐，诚实红
        faceAiStop(s);
        std::fprintf(stderr, "LA_FACE_MODEL_QA FAIL tamper=%d reason=ready line=%s ms=%lld\n",
                     int(tamper), gotReady ? line.constData() : "<none>", qint64(readyMs));
        std::fflush(stderr);
        return 1;
    }
    std::fprintf(stderr, "LA_FACE_MODEL_QA READY1 ms=%lld\n", qint64(readyMs));

    int totalFaces = 0;
    quint64 reqId = 0;
    for (const QString& img : images) {
        ++reqId;
        const QFileInfo ifi(img);
        const QString tag = ifi.completeBaseName();
        const QString goldPath = ifi.absolutePath() + QStringLiteral("/")
                               + ifi.completeBaseName() + QStringLiteral(".gold.json");
        QFile gf(goldPath);
        if (!gf.open(QIODevice::ReadOnly)) {
            fails << QStringLiteral("%1 gold json missing: %2").arg(tag, goldPath);
            std::fprintf(stderr, "LA_FACE_MODEL_QA IMG %s ok=0 (gold missing)\n",
                         qPrintable(tag));
            continue;
        }
        QVector<FmFace> gold;
        QString perr;
        if (!fmParseFaces(gf.readAll(), gold, &perr)) {
            fails << QStringLiteral("%1 gold parse: %2").arg(tag, perr);
            gf.close();
            std::fprintf(stderr, "LA_FACE_MODEL_QA IMG %s ok=0 (gold parse)\n", qPrintable(tag));
            continue;
        }
        gf.close();

        // 反例臂：只篡改期望表数据本身（IoU 红 + COS 红），比较/退出零分支（铁律22）。
        if (tamper) {
            for (FmFace& f : gold) {
                f.box[2] += 0.30f * (f.box[2] - f.box[0]);
                if (!f.emb.isEmpty()) f.emb[0] += 10.0f;
            }
        }

        // REQ <id> det <len> + UTF-8 路径体（面侧 det/path 同义，canonical det 归 T4）。
        const QByteArray pathBytes = img.toUtf8();
        const QByteArray hdr = QByteArray("REQ ") + QByteArray::number(qint64(reqId))
                             + QByteArray(" det ") + QByteArray::number(pathBytes.size())
                             + QByteArray("\n");
        s.proc.write(hdr);
        s.proc.write(pathBytes);
        if (!s.proc.waitForBytesWritten(3000)) {
            fails << QStringLiteral("%1 req write failed").arg(tag);
            std::fprintf(stderr, "LA_FACE_MODEL_QA IMG %s ok=0 (req write)\n", qPrintable(tag));
            continue;
        }

        QByteArray respHdr, body;
        const bool gotHdr = faceAiReadLine(s.proc, s.buf, respHdr, 60000);
        const QList<QByteArray> tk = respHdr.split(' ');
        if (!gotHdr || tk.size() != 3 || tk[0] != QByteArray("RES")
            || tk[1] != QByteArray::number(qint64(reqId))) {
            fails << QStringLiteral("%1 frame: %2").arg(tag, QString::fromUtf8(respHdr));
            std::fprintf(stderr, "LA_FACE_MODEL_QA IMG %s ok=0 (frame)\n", qPrintable(tag));
            continue;
        }
        const qint64 len = tk[2].toLongLong();
        if (len <= 0 || len > (1 << 20) * 64) {      // 64MiB 守卫（同面侧 kMaxReqBody 口径）
            fails << QStringLiteral("%1 body len out of range: %2").arg(tag).arg(len);
            std::fprintf(stderr, "LA_FACE_MODEL_QA IMG %s ok=0 (len)\n", qPrintable(tag));
            continue;
        }
        if (!faceAiReadExact(s.proc, s.buf, body, len, 60000)) {
            fails << QStringLiteral("%1 body read failed").arg(tag);
            std::fprintf(stderr, "LA_FACE_MODEL_QA IMG %s ok=0 (body read)\n", qPrintable(tag));
            continue;
        }
        QVector<FmFace> mine;
        QString jerr;
        if (!fmParseFaces(body, mine, &jerr)) {
            fails << QStringLiteral("%1 res parse: %2").arg(tag, jerr);
            std::fprintf(stderr, "LA_FACE_MODEL_QA IMG %s ok=0 (res parse)\n", qPrintable(tag));
            continue;
        }
        totalFaces += gold.size();
        const bool ok = fmCompareImage(gold, mine, fails, tag);
        std::fprintf(stderr, "LA_FACE_MODEL_QA IMG %s gold=%d mine=%d ok=%d\n",
                     qPrintable(tag), gold.size(), mine.size(), int(ok));
    }

    faceAiStop(s);

    if (fails.isEmpty())
        std::fprintf(stderr, "LA_FACE_MODEL_QA PASS tamper=%d imgs=%d faces=%d ready_ms=%lld total_ms=%lld\n",
                     int(tamper), images.size(), totalFaces, qint64(readyMs), qint64(total.elapsed()));
    else
        std::fprintf(stderr, "LA_FACE_MODEL_QA FAIL tamper=%d reason=%s\n",
                     int(tamper), qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
}

// ---------------- 第四轮 T7 第 76 门 LA_CLIP_AI_QA：CLIP 轨 AiProto 契约门（hermetic 桩） ----------------
// 镜像 #75 组谱（G1..G8 逐腿同构，kind="jpg" CLIP 轨唯一合法词），新增两组 prompts 态腿：
//   G9 桩侧 LA_CLIP_PROMPTS_BAD=1 -> READY 0 起步 + 每 REQ 回 ERR badprompts（桩内
//      prompts 坏优先于 nomodel 的线契约复刻，保活不退出）；
//   G10 真 clipinfer.exe + 临时模型目录（1B 垃圾 clip_vit_b32_img.onnx + 合成 16436B
//      prompts.bin：魔数/N/D 全对、尾段矩阵 SHA 故意错位）——启动时序 prompts 先于 ORT
//      必须秒级 READY 0 + ERR badprompts。反例真咬（铁律34）：若时序回归成 ORT 先行
//      收 code 变 loadfail；若 prompts 校验缺失则 READY 行/ERR 码双双失配——两红路都
//      比 351MB 加载失败更快更可辨。临时目录门后必删。
// 桩按 LA_CLIP_FORCE_* env 分支；门期望表是固定常量、永不随桩分支（铁律22）。
// 反例 LA_CLIP_AI_QA_FORCE_BROKEN=1 只篡改期望常量 -> 十组同红，退出路径与正跑一致。
static QString clipAiFindStub()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/clipinfer_stub.exe",                                        // dev：native/build 扁平产物
        QDir::cleanPath(appDir + "/../../clipinfer/build/clipinfer_stub.exe"), // dev build tree 源
    };
    for (const QString& p : candidates) if (QFileInfo::exists(p)) return p;
    return QString();
}

static QString clipModelFindExe()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/clipinfer.exe",                                        // dev：native/build 扁平产物（POST_BUILD 拷机 + ORT dll 同机）
        QDir::cleanPath(appDir + "/../../clipinfer/build/clipinfer.exe"), // dev build tree 源
    };
    for (const QString& p : candidates) if (QFileInfo::exists(p)) return p;
    return QString();
}

// REQ 一行 + jpg 体（CLIP 轨 kind 唯一合法词 "jpg"，body=UTF-8 绝对路径；面侧 "path" 同构异词）。
static bool clipAiSendReq(FaceAiSession& s, quint64 id, const QByteArray& path)
{
    const QByteArray hdr = QByteArray("REQ ") + QByteArray::number(id)
                         + QByteArray(" jpg ") + QByteArray::number(path.size()) + QByteArray("\n");
    s.proc.write(hdr);
    s.proc.write(path);
    return s.proc.waitForBytesWritten(3000);
}

int runClipAiQa()
{
    const bool tamper = !qEnvironmentVariableIsEmpty("LA_CLIP_AI_QA_FORCE_BROKEN");
    QStringList fails;
    const auto note = [&fails](bool ok, const QString& w){ if (!ok) fails << w; };

    const QString stub = clipAiFindStub();
    if (stub.isEmpty()) {
        std::fprintf(stderr, "LA_CLIP_AI_QA FAIL clipinfer_stub.exe missing -- build clipinfer target first\n");
        std::fflush(stderr);
        return 1;
    }

    // 与桩 clipinfer_stub.cpp clipJson() 逐字节一致的确定性夹具（铁律29：无随机无宿主因子；
    // 中文标签 \x 转义直出 UTF-8：人像/风光/美食）。
    const QByteArray expectBody = QByteArray(
        "{\"ok\":true,\"label\":\"\xe4\xba\xba\xe5\x83\x8f\",\"score\":0.8700,"
        "\"note\":null,\"top\":[[\"\xe4\xba\xba\xe5\x83\x8f\",0.8700],"
        "[\"\xe9\xa3\x8e\xe5\x85\x89\",0.0500],"
        "[\"\xe7\xbe\x8e\xe9\xa3\x9f\",0.0400]],\"sat\":42.0}");
    const QByteArray reqPath = QByteArrayLiteral("C:/la/clip_qa_fixture.jpg");

    // 固定期望表：tamper 臂只篡改常量，流程/退出路径零分支（铁律22）。
    // （G11 的 F-1 反例臂另有一处**夹具层**布防 LA_CLIP_FORCE_ERR——改的是桩答内容与
    //   期望常量，本函数的流程与退出路径仍零分支；详见 G11 腿注释。）
    using Rst = AiSubprocessRunner::Rst;
    const QByteArray expG1Ready = tamper ? QByteArray("READY 0") : QByteArray("READY 1");
    const QByteArray expG2Ready = tamper ? QByteArray("READY 1") : QByteArray("READY 0");
    const QByteArray expG2Code  = tamper ? QByteArray("loadfail") : QByteArray("nomodel");
    const QByteArray expG4Body  = tamper ? expectBody + QByteArray("X") : expectBody;
    const QByteArray expG5Code  = tamper ? QByteArray("inferfail") : QByteArray("loadfail");
    const int expG3Rst     = tamper ? int(Rst::Err)     : int(Rst::Ok);
    const QByteArray expG3Json = tamper ? expectBody + QByteArray("X") : expectBody;
    const QByteArray expG5pWire = tamper ? QByteArray("ERR 1 badframe") : QByteArray("ERR 0 badframe");
    const int expG5pReject = tamper ? 1 : 0;
    const int expG5pAccept = tamper ? 0 : 1;
    const int expGpMain    = tamper ? 0 : 1;
    const int expGpWorker  = tamper ? 1 : 0;
    const int expG6a       = tamper ? int(Rst::Ok)      : int(Rst::Timeout);
    const int expG6b       = tamper ? int(Rst::Timeout) : int(Rst::Ok);
    const QByteArray expG6bJson = tamper ? expectBody + QByteArray("X") : expectBody;
    const int expG7a       = tamper ? int(Rst::Err)     : int(Rst::Ok);
    const int expG7b       = tamper ? int(Rst::Ok)      : int(Rst::Busy);
    const int expG8Ready   = tamper ? int(Rst::Ok)      : int(Rst::NoExe);
    const int expG8Call    = tamper ? int(Rst::Ok)      : int(Rst::NoExe);
    const QByteArray expG9Ready  = tamper ? QByteArray("READY 1") : QByteArray("READY 0");
    const QByteArray expG9Code   = tamper ? QByteArray("nomodel") : QByteArray("badprompts");
    const QByteArray expG10Ready = tamper ? QByteArray("READY 1") : QByteArray("READY 0");
    const QByteArray expG10Code  = tamper ? QByteArray("loadfail") : QByteArray("badprompts");

    // ---- G1 默认臂：READY 1 握手 ----
    {
        FaceAiSession s;
        QByteArray line;
        const bool got = faceAiSpawn(s, stub, QByteArray())
                      && faceAiReadLine(s.proc, s.buf, line, 5000);
        const bool ok = got && line == expG1Ready;
        note(ok, QStringLiteral("G1 READY1 handshake, got=%1")
                     .arg(QString::fromLatin1(got ? line : QByteArray("<no frame>"))));
        faceAiStop(s);
        std::fprintf(stderr, "LA_CLIPAI_G G1 ok=%d\n", int(ok));
    }

    // ---- G2 NOMODEL 臂：READY 0 + 每 REQ ERR id nomodel（保活不退出） ----
    {
        FaceAiSession s;
        QByteArray line, resp;
        const bool started = faceAiSpawn(s, stub, "LA_CLIP_FORCE_NOMODEL=1");
        const bool gotReady = started && faceAiReadLine(s.proc, s.buf, line, 5000);
        const bool readyOk = gotReady && line == expG2Ready;
        note(readyOk, QStringLiteral("G2 READY0 handshake, got=%1")
                          .arg(QString::fromLatin1(gotReady ? line : QByteArray("<no frame>"))));
        const bool sent = readyOk && clipAiSendReq(s, 7, reqPath);
        const bool gotResp = sent && faceAiReadLine(s.proc, s.buf, resp, 5000);
        const QList<QByteArray> tk = resp.split(' ');
        const bool frameOk = gotResp && tk.size() == 3
                          && tk[0] == QByteArray("ERR") && tk[1] == QByteArray("7")
                          && tk[2] == expG2Code;
        note(frameOk, QStringLiteral("G2 nomodel ERR frame, line=%1").arg(QString::fromLatin1(resp)));
        faceAiStop(s);
        std::fprintf(stderr, "LA_CLIPAI_G G2 ok=%d\n", int(readyOk && frameOk));
    }

    // ---- G3 真底座全量 roundtrip：REQ(jpg)->RES JSON 逐字节 + 解析后 deep-equal。
    //      FRAG 钩子经 runner qaHookEnvName 一次性注入首 spawn 子进程（判例继承 #75）。----
    {
        qputenv("LA_CLIP_FORCE_FRAG", "1");    // 布防：底座 spawn 时注入子 env 并一次性消耗（分支只在桩侧，铁律22）
        AiSubprocessRunner r;
        r.configure({stub}, {}, 5000, QStringLiteral("LA_CLIP_FORCE_FRAG"));
        const auto rp = faceAiCall(r, QStringLiteral("jpg"), 21, reqPath, 8000);
        const bool rstOk = int(rp.rst) == expG3Rst;
        const bool bytesOk = rp.json == expG3Json;
        QJsonParseError pe1, pe2;
        const QJsonDocument dGot = QJsonDocument::fromJson(rp.json, &pe1);
        const QJsonDocument dExp = QJsonDocument::fromJson(expG3Json, &pe2);
        const bool deepOk = pe1.error == QJsonParseError::NoError
                         && pe2.error == QJsonParseError::NoError && dGot == dExp;
        note(rstOk, QStringLiteral("G3 runner roundtrip rst=%1 exp=%2")
                         .arg(int(rp.rst)).arg(expG3Rst));
        note(bytesOk, QStringLiteral("G3 runner roundtrip body bytes=%1 exp=%2")
                          .arg(rp.json.size()).arg(expG3Json.size()));
        note(deepOk, QStringLiteral("G3 RES json deep-equal (parsed)"));
        std::fprintf(stderr, "LA_CLIPAI_G G3 ok=%d json_bytes=%d\n",
                     int(rstOk && bytesOk && deepOk), int(rp.json.size()));
    }
    qputenv("LA_CLIP_FORCE_FRAG", QByteArray());   // 复位纪律：每次布防必显式解除

    // ---- G4 FRAG 臂：RES 体 64B 分片必读满 ----
    {
        FaceAiSession s;
        QByteArray line, hdr, body;
        const bool started = faceAiSpawn(s, stub, "LA_CLIP_FORCE_FRAG=1");
        const bool gotReady = started && faceAiReadLine(s.proc, s.buf, line, 5000);
        const bool readyOk = gotReady && line == QByteArray("READY 1");   // FRAG 不改握手语义（固定期望）
        note(readyOk, QStringLiteral("G4 READY1 handshake under FRAG, got=%1")
                          .arg(QString::fromLatin1(gotReady ? line : QByteArray("<no frame>"))));
        const bool sent = readyOk && clipAiSendReq(s, 9, reqPath);
        const bool gotHdr = sent && faceAiReadLine(s.proc, s.buf, hdr, 5000);
        const QList<QByteArray> tk = hdr.split(' ');
        const bool hdrOk = gotHdr && tk.size() == 3 && tk[0] == QByteArray("RES")
                        && tk[1] == QByteArray("9") && tk[2].toLongLong() == expectBody.size();
        note(hdrOk, QStringLiteral("G4 RES header id/len, line=%1").arg(QString::fromLatin1(hdr)));
        const bool bodyOk = hdrOk
                         && faceAiReadExact(s.proc, s.buf, body, expectBody.size(), 5000)
                         && body == expG4Body;
        note(bodyOk, QStringLiteral("G4 fragmented body read-full, bytes=%1 exp=%2")
                         .arg(body.size()).arg(expG4Body.size()));
        faceAiStop(s);
        std::fprintf(stderr, "LA_CLIPAI_G G4 ok=%d body_bytes=%d\n", int(readyOk && hdrOk && bodyOk), int(body.size()));
    }

    // ---- G5 ERR 臂：ERR 帧结构化（kind/id/code 三字段，id 严格匹配） ----
    {
        FaceAiSession s;
        QByteArray line, resp;
        const bool started = faceAiSpawn(s, stub, "LA_CLIP_FORCE_ERR=1");
        const bool gotReady = started && faceAiReadLine(s.proc, s.buf, line, 5000);
        const bool readyOk = gotReady && line == QByteArray("READY 1");
        note(readyOk, QStringLiteral("G5 READY1 handshake under ERR arm, got=%1")
                          .arg(QString::fromLatin1(gotReady ? line : QByteArray("<no frame>"))));
        const bool sent = readyOk && clipAiSendReq(s, 11, reqPath);
        const bool gotResp = sent && faceAiReadLine(s.proc, s.buf, resp, 5000);
        const QList<QByteArray> tk = resp.split(' ');
        const bool frameOk = gotResp && tk.size() == 3
                          && tk[0] == QByteArray("ERR") && tk[1] == QByteArray("11")
                          && tk[2] == expG5Code;
        note(frameOk, QStringLiteral("G5 structured ERR frame, line=%1").arg(QString::fromLatin1(resp)));
        faceAiStop(s);
        std::fprintf(stderr, "LA_CLIPAI_G G5 ok=%d\n", int(readyOk && frameOk));
    }

    // ---- G5p AiProto 首词全词比对回归腿（#75 同构）：parseReq 直调拒 "REQX" 收 "REQ"；
    //   线腿喂 REQX 行必收 ERR badframe；谓词腿 sIsMainThread 主真 worker 假。----
    {
        aiproto::Header hh;
        const QByteArray badLine  = QByteArrayLiteral("REQX 5 jpg 4");
        const QByteArray goodLine = QByteArrayLiteral("REQ 5 jpg 4");
        const int gotReject = aiproto::parseReq(badLine.constData(), size_t(badLine.size()), hh) ? 1 : 0;
        const int gotAccept = aiproto::parseReq(goodLine.constData(), size_t(goodLine.size()), hh) ? 1 : 0;
        const bool rejOk = gotReject == expG5pReject;
        const bool accOk = gotAccept == expG5pAccept;
        note(rejOk, QStringLiteral("G5p parseReq rejects REQX prefix (got=%1 exp=%2)")
                        .arg(gotReject).arg(expG5pReject));
        note(accOk, QStringLiteral("G5p parseReq still accepts well-formed REQ (got=%1 exp=%2)")
                        .arg(gotAccept).arg(expG5pAccept));
        bool wireOk = false;
        {
            FaceAiSession s;
            QByteArray line, resp;
            const bool started = faceAiSpawn(s, stub, QByteArray());
            const bool readyOk = started && faceAiReadLine(s.proc, s.buf, line, 5000)
                              && line == QByteArray("READY 1");
            s.proc.write(QByteArrayLiteral("REQX 5 jpg 4\ntest"));
            const bool wrote = s.proc.waitForBytesWritten(3000);
            const bool got = readyOk && wrote && faceAiReadLine(s.proc, s.buf, resp, 5000);
            wireOk = got && resp == expG5pWire;
            note(wireOk, QStringLiteral("G5p stub wire REQX answered badframe, line=%1")
                             .arg(QString::fromLatin1(got ? resp : QByteArray("<none>"))));
            faceAiStop(s);
        }
        const int gotMain = AiSubprocessRunner::sIsMainThread() ? 1 : 0;
        int gotWorker = -1;
        { std::thread th([&] { gotWorker = AiSubprocessRunner::sIsMainThread() ? 1 : 0; }); th.join(); }
        const bool predOk = gotMain == expGpMain && gotWorker == expGpWorker;
        note(predOk, QStringLiteral("G5p sIsMainThread predicate main=%1 worker=%2")
                         .arg(gotMain).arg(gotWorker));
        std::fprintf(stderr, "LA_CLIPAI_G G5p ok=%d\n", int(rejOk && accOk && wireOk && predOk));
    }

    // ---- G6 HANG -> Timeout -> 自动重启一次 -> 复绿（一次性钩子，第二 call 干净应答）。----
    {
        qputenv("LA_CLIP_FORCE_HANG", "1");
        AiSubprocessRunner r;
        r.configure({stub}, {}, 5000, QStringLiteral("LA_CLIP_FORCE_HANG"));
        const auto rp1 = faceAiCall(r, QStringLiteral("jpg"), 31, reqPath, 1500);
        const bool toOk = int(rp1.rst) == expG6a;
        note(toOk, QStringLiteral("G6 hung call rst=%1 exp=%2").arg(int(rp1.rst)).arg(expG6a));
        const auto rp2 = faceAiCall(r, QStringLiteral("jpg"), 32, reqPath, 8000);
        const bool greenOk = int(rp2.rst) == expG6b && rp2.json == expG6bJson;
        note(greenOk, QStringLiteral("G6 restarted call green rst=%1 bytes=%2")
                          .arg(int(rp2.rst)).arg(rp2.json.size()));
        std::fprintf(stderr, "LA_CLIPAI_G G6 ok=%d\n", int(toOk && greenOk));
    }
    qputenv("LA_CLIP_FORCE_HANG", QByteArray());

    // ---- G7 并发单飞拒绝：SLOW 3s 在飞窗口内第二调用者即收 Busy（不排队不死锁）。----
    {
        qputenv("LA_CLIP_FORCE_SLOW", "1");
        AiSubprocessRunner r;
        r.configure({stub}, {}, 5000, QStringLiteral("LA_CLIP_FORCE_SLOW"));
        AiSubprocessRunner::Reply rpA;
        std::atomic<bool> aStarted{false};
        std::thread aTh([&] { aStarted.store(true); rpA = r.call(QStringLiteral("jpg"), 41, reqPath, 12000); });
        while (!aStarted.load()) QThread::msleep(5);
        QThread::msleep(300);
        const auto rpB = faceAiCall(r, QStringLiteral("jpg"), 42, reqPath, 5000);
        aTh.join();
        const bool busyOk = int(rpB.rst) == expG7b;
        const bool aOk = int(rpA.rst) == expG7a && rpA.json == expectBody;
        note(busyOk, QStringLiteral("G7 second concurrent caller rst=%1 exp=%2")
                         .arg(int(rpB.rst)).arg(expG7b));
        note(aOk, QStringLiteral("G7 first caller completes rst=%1").arg(int(rpA.rst)));
        std::fprintf(stderr, "LA_CLIPAI_G G7 ok=%d\n", int(busyOk && aOk));
    }
    qputenv("LA_CLIP_FORCE_SLOW", QByteArray());

    // ---- G8 exe 候选全缺 -> NoExe（probe 与 call 一致，绝不 spawn）。----
    {
        AiSubprocessRunner r;
        r.configure({stub + QStringLiteral(".missing_a.exe"),
                     stub + QStringLiteral(".missing_b.exe")}, {}, 5000, QString());
        Rst st = Rst::Err;
        const bool pOk = faceAiProbe(r, &st);
        const bool readyNoExe = !pOk && int(st) == expG8Ready;
        note(readyNoExe, QStringLiteral("G8 ensureReady exe_missing ok=%1 rst=%2 exp=%3")
                             .arg(int(pOk)).arg(int(st)).arg(expG8Ready));
        const auto rp = faceAiCall(r, QStringLiteral("jpg"), 51, reqPath, 3000);
        const bool callNoExe = int(rp.rst) == expG8Call;
        note(callNoExe, QStringLiteral("G8 call exe_missing rst=%1 exp=%2")
                            .arg(int(rp.rst)).arg(expG8Call));
        std::fprintf(stderr, "LA_CLIPAI_G G8 ok=%d\n", int(readyNoExe && callNoExe));
    }

    // ---- G9 桩侧 PROMPTS_BAD 臂：READY 0 + 每 REQ ERR badprompts（prompts 坏优先于
    //      nomodel 的启动时序契约复刻，G10 真壳腿的 hermetic 兄弟）。----
    {
        FaceAiSession s;
        QByteArray line, resp;
        const bool started = faceAiSpawn(s, stub, "LA_CLIP_PROMPTS_BAD=1");
        const bool gotReady = started && faceAiReadLine(s.proc, s.buf, line, 5000);
        const bool readyOk = gotReady && line == expG9Ready;
        note(readyOk, QStringLiteral("G9 READY0 handshake under PROMPTS_BAD, got=%1")
                          .arg(QString::fromLatin1(gotReady ? line : QByteArray("<no frame>"))));
        const bool sent = readyOk && clipAiSendReq(s, 13, reqPath);
        const bool gotResp = sent && faceAiReadLine(s.proc, s.buf, resp, 5000);
        const QList<QByteArray> tk = resp.split(' ');
        const bool frameOk = gotResp && tk.size() == 3
                          && tk[0] == QByteArray("ERR") && tk[1] == QByteArray("13")
                          && tk[2] == expG9Code;
        note(frameOk, QStringLiteral("G9 badprompts ERR frame, line=%1").arg(QString::fromLatin1(resp)));
        faceAiStop(s);
        std::fprintf(stderr, "LA_CLIPAI_G G9 ok=%d\n", int(readyOk && frameOk));
    }

    // ---- G10 真 clipinfer.exe prompts 反例：临时模型目录 = 1B 垃圾 onnx + 合成
    //      prompts.bin（16436B、魔数/N/D 全对、尾段 SHA 错位）。启动时序 prompts 先于
    //      ORT => 秒级 READY 0 + ERR badprompts（垃圾 onnx 永不被触碰；若时序倒置
    //      则 351MB 级失败信号 loadfail——本腿以其为红）。门后必删临时目录（R-06 卫生）。----
    {
        bool groupOk = false;
        const QString exe = clipModelFindExe();
        if (exe.isEmpty()) {
            note(false, QStringLiteral("G10 clipinfer.exe missing -- build clipinfer target first"));
        } else {
            const QString dir = QDir::tempPath() + QStringLiteral("/la_clip_g10_%1")
                                    .arg(QCoreApplication::applicationPid());
            QDir().mkpath(dir);
            bool staged = false;
            QFile ox(dir + QStringLiteral("/clip_vit_b32_img.onnx"));
            if (ox.open(QIODevice::WriteOnly)) { ox.write(QByteArray(1, '\0')); ox.close(); staged = true; }
            QFile pr(dir + QStringLiteral("/prompts.bin"));
            if (staged && pr.open(QIODevice::WriteOnly)) {
                QByteArray t(16436, '\0');                       // 头部全对：只有尾段 SHA 是错的
                t.replace(0, 10, QByteArray("LAPROMPTv1"));      // 魔数
                t[12] = char(8);                                 // N=8 int32LE
                t[17] = char(2);                                 // D=512 int32LE（0x0200）
                // bytes[16404..16436) 尾段保持全零 => 矩阵 SHA 必失配（正 SHA 是随机 32B）
                pr.write(t); pr.close();
            } else staged = false;
            if (staged) {
                FaceAiSession s;
                QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
                e.insert(QStringLiteral("LA_CLIP_MODEL"), QDir::toNativeSeparators(dir));
                s.proc.setProcessEnvironment(e);
                s.proc.setProcessChannelMode(QProcess::SeparateChannels);
                s.proc.start(exe, {});
                QByteArray line, resp;
                const bool started = s.proc.waitForStarted(5000);
                const bool gotReady = started && faceAiReadLine(s.proc, s.buf, line, 10000);
                const bool readyOk = gotReady && line == expG10Ready;
                note(readyOk, QStringLiteral("G10 real exe READY0(prompts-first), got=%1")
                                  .arg(QString::fromLatin1(gotReady ? line : QByteArray("<no frame>"))));
                const bool sent = readyOk && clipAiSendReq(s, 15, reqPath);
                const bool gotResp = sent && faceAiReadLine(s.proc, s.buf, resp, 5000);
                const QList<QByteArray> tk = resp.split(' ');
                const bool frameOk = gotResp && tk.size() == 3
                                  && tk[0] == QByteArray("ERR") && tk[1] == QByteArray("15")
                                  && tk[2] == expG10Code;
                note(frameOk, QStringLiteral("G10 badprompts ERR frame, line=%1")
                                  .arg(QString::fromLatin1(resp)));
                faceAiStop(s);
                groupOk = readyOk && frameOk;
            } else {
                note(false, QStringLiteral("G10 temp model dir staging failed: %1").arg(dir));
            }
            QFile::remove(dir + QStringLiteral("/clip_vit_b32_img.onnx"));
            QFile::remove(dir + QStringLiteral("/prompts.bin"));
            QDir().rmdir(dir);
        }
        std::fprintf(stderr, "LA_CLIPAI_G G10 ok=%d\n", int(groupOk));
    }

    // ---- G11 [T8] 生产路径腿：Classifier::classify（**不吃合成钩子**）→ ClipRunner 门面
    //      → AiSubprocessRunner 底座 → 子进程（LA_CLIP_STUB=1 走门面候选链第 1 跳的桩，
    //      与 FaceRunner 的 LA_FACE_STUB 同构；桩文件头钩子表明写「runner 侧强制选桩
    //      标记，QA 接线消费」，本腿即该消费点）。
    //      为什么这条腿必须存在：T8 净替换掉的正是 Classifier 的传输段，而 #70 全程用
    //      setSyntheticForQa 绕过传输，#76 的 G1..G10 只驱动底座本身——净替换后的真实
    //      通路（逐张 REQ jpg → RES JSON → normalizeLabel → AiWriter 写回）此前无任何
    //      门禁覆盖。桩答固定 label=人像/score=0.87（与 G3 期望体同源），故「DB+内存同时
    //      出现人像」只可能来自一次真 REQ/RES 往返，不是本地伪造。
    //      另断 R-80 第 1 条的分片钳制可观察面：7 张待分类 + batch=32 ⇒ 进度增量必须是 5+2
    //      （kClipMaxBatch=5 的降语义连续分片；若钳制被「顺手」删回 32 则 7>5 立刻红）。
    //      守恒面：已分类张不被覆盖、无缩略图张不发请求也不写回。
    //      T8 修复波第 1 轮（F-1）返回值语义面：classify() 的返回值 = 「本轮是否全部干净
    //      写回」，不是「是否跑过」——正例臂（桩干净应答）必须 true；tamper 臂物理注入
    //      F-1 的真实缺陷形态（LA_CLIP_FORCE_ERR：READY 1 但每 REQ 回 ERR ⇒ 7/7 全失败、
    //      零写回）必须 false。布防装置与 #78/#77 的 `if (tamper)` 反例注入同源：缺陷只注入
    //      反例臂、期望恒钉规格值——恒真实现被 tamper 臂咬红、恒假实现被正例臂咬红，两臂互锁。
    //      期望表只随 tamper 变（铁律22）：LA_CLIP_STUB / tamper 臂的 LA_CLIP_FORCE_ERR 均属
    //      **夹具层**（选哪个 exe、该 exe 答什么），不是期望分叉。----
    {
        const QString dir = QDir::tempPath() + QStringLiteral("/la_clipai_g11_%1")
                                .arg(QCoreApplication::applicationPid());
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        const QString dbPath = QDir(dir).filePath(QStringLiteral("g11.db"));
        // 名册：p0..p6 未分类且各有缩略图（7 张 = 待分类全集，触达 5+2 两片）；
        //       p7 未分类但**无缩略图**（thumb 列为空）；p8 已分类=夜景·星空（不得被覆盖）。
        const int kPending = 7;
        const int kRows = 9;
        // p8 的预置分类（=「已分类不得被覆盖」的守恒锚；断言侧复用同一常量，避免两处转义劈叉）。
        const QString seededCat = QStringLiteral("夜景·星空");
        bool thumbsOk = true;
        for (int i = 0; i < kPending; ++i) {
            QFile tf(QDir(dir).filePath(QStringLiteral("t%1.jpg").arg(i)));
            if (!tf.open(QIODevice::WriteOnly)) { thumbsOk = false; break; }
            tf.write("x", 1);
            tf.close();
        }
        note(thumbsOk, QStringLiteral("G11 thumb fixture staging failed under %1").arg(dir));
        bool seeded = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("la_clipai_g11_seed"));
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery q(db);
                q.exec(QStringLiteral(
                    "CREATE TABLE photos (path TEXT PRIMARY KEY, rel TEXT, thumb TEXT, "
                    "missing INTEGER DEFAULT 0, exif_json TEXT, category TEXT, person TEXT, "
                    "private INTEGER DEFAULT 0, rating INTEGER DEFAULT 0, mark TEXT)"));
                q.exec(QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)"));
                bool allRows = true;
                for (int i = 0; i < kRows; ++i) {
                    // 列/占位符严格对齐 Library::open 的 SELECT（形参照抄 #70 的 seed 语句）。
                    q.prepare(QStringLiteral(
                        "INSERT INTO photos(path,rel,thumb,missing,exif_json,category,person,private,rating,mark) "
                        "VALUES(?,?,?,0,?,?,?,0,0,?)"));
                    q.addBindValue(QStringLiteral("C:/la/p%1.jpg").arg(i));
                    q.addBindValue(QStringLiteral("p%1.jpg").arg(i));
                    q.addBindValue(i < kPending
                        ? QStringLiteral("t%1.jpg").arg(i) : QString());   // p7 未分类且无缩略图
                    q.addBindValue(QStringLiteral("{}"));                  // exif_json
                    q.addBindValue(i == 8 ? seededCat : QString());        // 其余 category 空=待分类
                    q.addBindValue(QString());                             // person
                    q.addBindValue(QString());                             // mark
                    if (!q.exec()) allRows = false;
                }
                db.close();
                seeded = allRows;
            }
            QSqlDatabase::removeDatabase(QStringLiteral("la_clipai_g11_seed"));
        }
        note(seeded, QStringLiteral("G11 temp library seed failed"));

        Library lib;
        QString operr;
        const bool opened = seeded && lib.open(dbPath, &operr);
        note(opened, QStringLiteral("G11 library open=%1").arg(operr));

        // 生产侧的桩标记：布防 + 用毕显式解除（与 G3/G6/G7 的 FORCE_* 复位纪律同式）。
        qputenv("LA_CLIP_STUB", "1");
        // 期望表（固定常量；tamper 只改期望、不改流程，铁律22）。
        const QString expG11Label = tamper ? QStringLiteral("黑白") : QStringLiteral("人像");
        const QString expG11Prog  = tamper ? QStringLiteral("0,7") : QStringLiteral("0,5,7");
        // F-1（T8 修复波第 1 轮）：返回值期望——干净轮 true / 全失败轮 false。tamper 臂
        // 用 LA_CLIP_FORCE_ERR 物理注入「引擎逐帧 ERR ⇒ 7/7 failed、零写回」的缺陷形态，
        // 与 #78/#77 的 `if (tamper)` 注入装置同源；实现若「全失败仍 return true」即红。
        const bool expG11Ret = !tamper;
        QVector<int> progMarks;
        QObject qaCtx;
        bool classifyRet = false;
        if (opened && thumbsOk) {
            AiWriter writer(&lib, &qaCtx);
            // 快照取在 QA 主线程：Library 非线程安全，生产侧 MainWindow::runClassify 同款
            // （worker 只吃快照，绝不回头碰 lib）。
            const QVector<Photo> snap = lib.photos();
            // 底座禁 GUI 线程调用（此处 = QA 主线程）：生产接线要求 worker 线程，
            // 本腿同时是 R-20「探测移入 worker」的同型断言——主线程直调会拿 Busy →
            // probe false → classify 返回 false → 红。
            if (tamper) qputenv("LA_CLIP_FORCE_ERR", "1");   // F-1 反例布防（用毕下句解除）
            QtConcurrent::run([&] {
                classifyRet = Classifier::classify(snap, writer, dir, 32, -1,
                    [&progMarks](int done, int, const QString&) { progMarks << done; },
                    [] { return false; });
            }).waitForFinished();
            if (tamper) qputenv("LA_CLIP_FORCE_ERR", QByteArray());
            // 写回经 AiWriter 的 QueuedConnection 投递回本线程落地（#70 同式）。
            QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
        }
        qputenv("LA_CLIP_STUB", QByteArray());

        note(classifyRet == expG11Ret,
             QStringLiteral("G11 classify ret=%1 want=%2 (clean round true / all-failed round false)")
                 .arg(int(classifyRet)).arg(int(expG11Ret)));
        // 写回落点（内存）：7 张 = 桩答标签，且经 normalizeLabel 恒等映射。
        int wroteExpected = 0;
        for (int i = 0; i < kPending; ++i) {
            for (const Photo& ph : lib.photos())
                if (ph.path == QStringLiteral("C:/la/p%1.jpg").arg(i) && ph.category == expG11Label)
                    ++wroteExpected;
        }
        note(wroteExpected == kPending,
             QStringLiteral("G11 written=%1 want=%2 label=%3")
                 .arg(wroteExpected).arg(kPending).arg(expG11Label));
        // 无缩略图张不写回（跳过不发请求），已分类张不被覆盖（守恒）。
        QString p7cat, p8cat;
        for (const Photo& ph : lib.photos()) {
            if (ph.path == QStringLiteral("C:/la/p7.jpg")) p7cat = ph.category;
            if (ph.path == QStringLiteral("C:/la/p8.jpg")) p8cat = ph.category;
        }
        note(p7cat.isEmpty(), QStringLiteral("G11 p7 (no thumb) got category=%1").arg(p7cat));
        note(p8cat == seededCat,
             QStringLiteral("G11 p8 overwritten=%1").arg(p8cat));
        note(lib.photos().size() == kRows,
             QStringLiteral("G11 conserve rows=%1 want=%2").arg(lib.photos().size()).arg(kRows));
        // 落盘面（独立连接读磁盘）：内存对了不算落地，AiWriter→Library::setCategories 的
        // SQL 写回必须由磁盘侧复核（#70 的 dbCat 同式，只复核一张即足以证明真写库）。
        {
            QString dbP0 = QStringLiteral("__unreadable__");
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("la_clipai_g11_read"));
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery q(db);
                q.prepare(QStringLiteral("SELECT category FROM photos WHERE path=?"));
                q.addBindValue(QStringLiteral("C:/la/p0.jpg"));
                if (q.exec() && q.next()) dbP0 = q.value(0).toString();
                db.close();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("la_clipai_g11_read"));
            note(dbP0 == expG11Label,
                 QStringLiteral("G11 db p0=%1 want=%2").arg(dbP0, expG11Label));
        }
        // 分片钳制可观察面：进度序列（首帧 0 起算）逐片增量 <= 上限且累计到 total。
        const int expG11ChunkCap = tamper ? 32 : 5;    // tamper 臂只放宽上限期望 ⇒ 真 5+2 序列照样红
        QStringList seq;
        int prev = 0;
        bool deltaOk = true;
        for (const int m : progMarks) {
            seq << QString::number(m);
            if (m - prev > expG11ChunkCap) deltaOk = false;
            prev = m;
        }
        const QString gotSeq = seq.join(QLatin1Char(','));
        note(gotSeq == expG11Prog,
             QStringLiteral("G11 progress seq=%1 want=%2").arg(gotSeq, expG11Prog));
        note(deltaOk, QStringLiteral("G11 chunk delta exceeds maxBatch cap"));
        const bool g11Ok = classifyRet && wroteExpected == kPending && p7cat.isEmpty()
                           && p8cat == seededCat && gotSeq == expG11Prog && deltaOk;
        std::fprintf(stderr, "LA_CLIPAI_G G11 ok=%d written=%d prog=%s\n",
                     int(g11Ok), wroteExpected, qPrintable(gotSeq));
        // R-06 卫生：门后必删临时目录。⚠ 必须先摘掉 Library 留下的两条**全局命名连接**
        // （la_lib 只读 + la_rw 惰性读写）——连接句柄活在全局池里，不受本腿 lib 的
        // 作用域/析构影响；Windows 上被打开的 sqlite 文件删不掉，于是 removeRecursively
        // 只删得掉 t*.jpg、独独留下 g11.db（实测：每次跑泄漏一个目录，内容只有 g11.db）。
        // 本腿此后不再触碰 lib，摘连接安全（QSqlDatabase 局部对象先出作用域再 remove）。
        for (const QString& conn : {QStringLiteral("la_rw"), QStringLiteral("la_lib")}) {
            if (QSqlDatabase::contains(conn)) {
                {
                    QSqlDatabase db = QSqlDatabase::database(conn);
                    db.close();
                }
                QSqlDatabase::removeDatabase(conn);
            }
        }
        QDir(dir).removeRecursively();
    }

    if (fails.isEmpty())
        std::fprintf(stderr, "LA_CLIP_AI_QA PASS tamper=%d g1=1 g2=1 g3=1 g4=1 g5=1 g5p=1 g6=1 g7=1 g8=1 g9=1 g10=1 g11=1\n", int(tamper));
    else
        std::fprintf(stderr, "LA_CLIP_AI_QA FAIL tamper=%d reason=%s\n",
                     int(tamper), qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
}

// ---------------- 第四轮 T7 第 77 门 LA_CLIP_MODEL_QA：CLIP 模型端到端五列对表门（full 层，opt-in） ----------------
// 真 clipinfer.exe（图像塔 ORT 会话 + 判定三段）直起（同 #78：本任务无 runner，纯同步
// QProcess 逐 REQ 应答）；对 T7 金标 JSON 断言 spec §4 五列全表：
//   label verbatim 全等；note verbatim 全等（null 对齐）；|score-Δ| <= 0.005；
//   top3 逐对 label 全等 + |prob-Δ| <= 0.02；|sat-Δ| <= 1.0。
// R-14 裁定记录：sat 路径 = PIL convert('RGB').resize((48,48)) 默认 bicubic(Catmull-Rom)
// 的 libImaging 定点核 verbatim 移植（brief「双线性」作废；dispatch「reducing_gap=3.0
// 预滤波」经 Pillow 12.3.0 源码核实不存在——resize 默认 reducing_gap=None）。实测
// 13/13 夹具：worst score Δ 0.0026（IMG_9387 gold 0.8588 vs mine 0.8614，score Δ 非零
// 者 5/13、其余 8 条 Δ=0.0000）；worst sat Δ 0.1（量化末位）；label/note 全表逐字相等。
// 严格带保留不放宽（与 gate_matrix.ps1 #77 注册注释逐字同数，T12 机械核对项）。
// 隐私红线 R-06：真人图像与 gold JSON 永不入仓——门只从 env 路径读取：
//   LA_CLIP_MODEL = 模型目录（clip_vit_b32_img.onnx + prompts.bin）；
//   LA_CLIP_GOLD_DIR = gold JSON 目录（<完整basename>.gold.json 平铺，I-3：金名含扩展名，
//                      与生产者 basename+".gold.json" 同契约，同干异扩展不撞金）；
//   LA_CLIP_IMGS = 逗号分隔图像文件或目录（jpg/jpeg/png/bmp/webp，目录取 Name 序）。
// env 缺席 = qa_runner need= 机制自动 SKIP；opted-in 而前置缺失（exe/模型/gold/READY!=1）
// = 诚实 FAIL（铁律34，禁静默放行）。夹具↔金份数交叉校验：imgs 与 goldDir 内
// *.gold.json 计数不等即红（防「少样本、计数自洽」的隐蔽降级，轴 10 判据 3 同族）。
// 反例 LA_CLIP_MODEL_QA_FORCE_BROKEN=1 只篡改期望表数据，比较流程与退出路径零分支
// （铁律22/34），两臂并列同笔生效：
//   ① cqTamperMonoOff：MONO_SAT_THRESHOLD=0.0 仿真——low_saturation 金标的黑白快捷路径
//     禁用、回落普通分支 => IMG_8409 LABEL/NOTE/SCORE 三列红；
//   ② cqTamperOtherOff：OTHER_SCORE_THRESHOLD 侧永不触发仿真——low_confidence 金标改
//     「top1 类名 + top1 prob + note=null」期望镜像 => synthetic_lowconf LABEL/NOTE 红
//     （score 镜像同值故 SCORE_D=0，正是红意所在：分支本身必须被咬）。
struct ClipQaVerdict
{
    bool hasOk = false;      // RES schema 首位字段 ok（gold JSON 无此键，仅应答侧解析）
    bool okValue = true;
    QString label;
    bool hasNote = false;
    QString note;
    double score = 0.0;
    double sat = 0.0;
    bool hasScreenGap = false;   // gold.screen.gap_top1_top2（夹具选点留量，RES 无此键）
    double screenGap = 0.0;
    QVector<QPair<QString, double>> top;    // [label, prob] 序 = RES/gold top 数组序
};

// RES 体与 gold JSON 同 schema 子集（gold 附加键 image/oracle/model/thresholds/screen 忽略）。
static bool cqParseVerdict(const QByteArray& json, ClipQaVerdict& v, QString* err)
{
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (doc.isNull() || !doc.isObject()) {
        if (err) *err = QStringLiteral("json parse failed at offset %1: %2")
            .arg(perr.offset).arg(perr.errorString());
        return false;
    }
    const QJsonObject o = doc.object();
    const QJsonValue ov = o.value(QStringLiteral("ok"));
    v.hasOk = ov.type() == QJsonValue::Bool;
    v.okValue = !v.hasOk || ov.toBool();
    v.label = o.value(QStringLiteral("label")).toString();
    const QJsonValue nv = o.value(QStringLiteral("note"));
    v.hasNote = !nv.isNull();
    v.note = v.hasNote ? nv.toString() : QString();
    v.score = o.value(QStringLiteral("score")).toDouble();
    v.sat = o.value(QStringLiteral("sat")).toDouble();
    const QJsonObject scr = o.value(QStringLiteral("screen")).toObject();
    const QJsonValue gv = scr.value(QStringLiteral("gap_top1_top2"));
    v.hasScreenGap = gv.type() == QJsonValue::Double;
    v.screenGap = v.hasScreenGap ? gv.toDouble() : 0.0;
    v.top.clear();
    const QJsonArray ta = o.value(QStringLiteral("top")).toArray();
    for (const QJsonValue& tv : ta) {
        const QJsonArray pa = tv.toArray();
        if (pa.size() != 2) { if (err) *err = QStringLiteral("bad top pair"); return false; }
        v.top.append(qMakePair(pa[0].toString(), pa[1].toDouble()));
    }
    return true;
}

// 五列断言（期望表 vs 实测表）；两臂共用同一函数，tamper 只作用于传入的期望表数据（铁律22）。
static bool cqCompareVerdict(const ClipQaVerdict& g, const ClipQaVerdict& m,
                             QStringList& fails, const QString& tag)
{
    bool ok = true;
    if (g.label != m.label) {
        fails << QStringLiteral("%1 LABEL gold=%2 mine=%3").arg(tag, g.label, m.label);
        ok = false;
    }
    if (g.hasNote != m.hasNote || (g.hasNote && g.note != m.note)) {
        fails << QStringLiteral("%2 NOTE gold=%3 mine=%4")
                     .arg(tag,
                          g.hasNote ? g.note : QStringLiteral("<null>"),
                          m.hasNote ? m.note : QStringLiteral("<null>"));
        ok = false;
    }
    const double sd = std::fabs(g.score - m.score);
    if (sd > 0.005) {
        fails << QStringLiteral("%1 SCORE_D=%2").arg(tag).arg(sd, 0, 'f', 5);
        ok = false;
    }
    const double satd = std::fabs(g.sat - m.sat);
    if (satd > 1.0) {
        fails << QStringLiteral("%1 SAT_D=%2").arg(tag).arg(satd, 0, 'f', 2);
        ok = false;
    }
    if (g.top.size() != m.top.size() || g.top.size() < 3) {
        fails << QStringLiteral("%1 TOP size gold=%2 mine=%3").arg(tag).arg(g.top.size()).arg(m.top.size());
        ok = false;
    } else {
        for (int i = 0; i < 3; ++i) {
            const double d = std::fabs(g.top[i].second - m.top[i].second);
            if (g.top[i].first != m.top[i].first || d > 0.02) {
                fails << QStringLiteral("%1 TOP%2 gold=(%3,%4) mine=(%5,%6) D=%7")
                             .arg(tag).arg(i)
                             .arg(g.top[i].first).arg(g.top[i].second, 0, 'f', 4)
                             .arg(m.top[i].first).arg(m.top[i].second, 0, 'f', 4)
                             .arg(d, 0, 'f', 5);
                ok = false;
            }
        }
    }
    return ok;
}

// 反例篡改器：MONO_SAT_THRESHOLD=0.0 语义仿真——黑白快捷路径永久禁用，low_saturation
// 金标回落普通分支（top1<0.20 -> 其他/low_confidence；否则 top1 名 + score=top1 prob +
// note=null；sat<=18 恒不触发 mono_guard(>45)）。真壳仍答 0.99/low_saturation => 红。
static void cqTamperMonoOff(ClipQaVerdict& g)
{
    if (!g.hasNote || !g.note.startsWith(QStringLiteral("low_saturation"))) return;
    if (g.top.isEmpty()) return;
    const QString t1 = g.top[0].first;
    const double p1 = g.top[0].second;
    if (p1 < 0.20) {
        g.label = QStringLiteral("\xe5\x85\xb6\xe4\xbb\x96");   // 其他
        g.score = p1;
        g.note = QStringLiteral("low_confidence");
    } else {
        g.label = t1;
        g.score = p1;
        g.note = QString();
        g.hasNote = false;
    }
}

// 反例篡改器②（轴 12 判据 3 的反向证明）：OTHER_SCORE_THRESHOLD 侧「永不触发」语义仿真
// ——low_confidence 金标回落普通分支：期望改为 (top1 类名, top1 prob, note=null)。
// 真壳仍答 其他/low_confidence => LABEL+NOTE 两列红（SCORE_D=0.00000 恰证红来自分支
// 判定本身而非数值扰动：期望 prob 与应答 prob 同源）。只作用于期望表（铁律22）。
static void cqTamperOtherOff(ClipQaVerdict& g)
{
    if (!g.hasNote || g.note != QStringLiteral("low_confidence")) return;
    if (g.top.isEmpty()) return;
    g.label = g.top[0].first;
    g.score = g.top[0].second;
    g.note = QString();
    g.hasNote = false;
}

int runClipModelQa()
{
    const bool tamper = !qEnvironmentVariableIsEmpty("LA_CLIP_MODEL_QA_FORCE_BROKEN");
    QStringList fails;

    const QString exe = clipModelFindExe();
    if (exe.isEmpty()) {
        std::fprintf(stderr, "LA_CLIP_MODEL_QA FAIL tamper=%d reason=exe_missing clipinfer.exe -- build clipinfer target first\n", int(tamper));
        std::fflush(stderr);
        return 1;
    }
    const QString modelDir = QString::fromLocal8Bit(qgetenv("LA_CLIP_MODEL"));
    if (modelDir.isEmpty() || !QDir(modelDir).exists()) {
        std::fprintf(stderr, "LA_CLIP_MODEL_QA FAIL tamper=%d reason=model_dir_missing\n", int(tamper));
        std::fflush(stderr);
        return 1;
    }
    const QString goldDir = QString::fromLocal8Bit(qgetenv("LA_CLIP_GOLD_DIR"));
    if (goldDir.isEmpty() || !QDir(goldDir).exists()) {
        std::fprintf(stderr, "LA_CLIP_MODEL_QA FAIL tamper=%d reason=gold_dir_missing\n", int(tamper));
        std::fflush(stderr);
        return 1;
    }
    const QString imgSpec = QString::fromLocal8Bit(qgetenv("LA_CLIP_IMGS"));
    QStringList images;
    for (const QString& part : imgSpec.split(QChar(','))) {
        const QString p = part.trimmed();
        if (p.isEmpty()) continue;
        const QFileInfo fi(p);
        if (fi.isFile()) {
            images << fi.absoluteFilePath();
        } else if (fi.isDir()) {
            QDir d(p);
            const QStringList filters = { QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                         QStringLiteral("*.png"), QStringLiteral("*.bmp"),
                                         QStringLiteral("*.webp") };
            const QFileInfoList lst = d.entryInfoList(filters, QDir::Files, QDir::Name);
            for (const QFileInfo& e : lst) images << e.absoluteFilePath();
        }
    }
    images.removeDuplicates();
    images.sort();
    if (images.isEmpty()) {
        std::fprintf(stderr, "LA_CLIP_MODEL_QA FAIL tamper=%d reason=no_images (LA_CLIP_IMGS)\n", int(tamper));
        std::fflush(stderr);
        return 1;
    }
    // I-4 交叉校验：枚举到的图像数必须与 goldDir 内 *.gold.json 份数相等——任何枚举漏样
    // （大小写敏感挂载点、名单外格式如 .TIF、非 fsentry 命中）都会让「imgs=N ok=N」假绿，
    // 正是轴 10 定义的隐蔽降级形态。计数进 PASS 行留档（imgs=13 golds=13 ok=13）。
    const int goldCount = QDir(goldDir).entryList({ QStringLiteral("*.gold.json") },
                                                  QDir::Files, QDir::Name).size();
    if (goldCount != images.size())
        fails << QStringLiteral("fixture/gold count mismatch imgs=%1 golds=%2")
                     .arg(images.size()).arg(goldCount);

    // 真 clipinfer.exe 直起：子端 env 口径 = LA_CLIP_MODEL（模型目录唯一 env，R-15 无 Qt 自管）。
    FaceAiSession s;
    {
        QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
        e.insert(QStringLiteral("LA_CLIP_MODEL"), modelDir);
        s.proc.setProcessEnvironment(e);
        s.proc.setProcessChannelMode(QProcess::SeparateChannels);
        s.proc.start(exe, {});
    }
    if (!s.proc.waitForStarted(5000)) {
        std::fprintf(stderr, "LA_CLIP_MODEL_QA FAIL tamper=%d reason=spawn_failed\n", int(tamper));
        std::fflush(stderr);
        return 1;
    }
    QElapsedTimer total; total.start();
    QByteArray line;
    const bool gotReady = faceAiReadLine(s.proc, s.buf, line, 90000);   // 真塔冷启动预算（#78 同式）
    const qint64 readyMs = total.elapsed();
    if (!gotReady || line != QByteArray("READY 1")) {                   // READY!=1 = 会话未齐，诚实红
        faceAiStop(s);
        std::fprintf(stderr, "LA_CLIP_MODEL_QA FAIL tamper=%d reason=ready line=%s ms=%lld\n",
                     int(tamper), gotReady ? line.constData() : "<none>", qint64(readyMs));
        std::fflush(stderr);
        return 1;
    }
    std::fprintf(stderr, "LA_CLIP_MODEL_QA READY1 ms=%lld\n", qint64(readyMs));

    int okCount = 0;
    quint64 reqId = 0;
    for (const QString& img : images) {
        ++reqId;
        // 金侧配对名 = 图像文件完整 basename（含扩展名）：生产者 gen_clip_fixtures.py
        // 与本波 gen_clip_synthetic_lowconf.py 均写 basename(dst)+".gold.json"，消费侧
        // 用 fileName() 拼同一名字（I-3/T6-M3：同干不同扩展 a.png/a.JPG 的金不再撞名互
        // 覆盖——「干名撞车」在契约层不可能）。tag 仍取 completeBaseName()，仅作显示归因，
        // 绝不参与配金；禁止回退成手切 stem（那会重新引入撞名路径）。
        const QString tag = QFileInfo(img).completeBaseName();
        const QString goldPath = goldDir + QStringLiteral("/")
                               + QFileInfo(img).fileName() + QStringLiteral(".gold.json");
        QFile gf(goldPath);
        if (!gf.open(QIODevice::ReadOnly)) {
            fails << QStringLiteral("%1 gold json missing: %2").arg(tag, goldPath);
            std::fprintf(stderr, "LA_CLIP_MODEL_QA IMG %s ok=0 (gold missing)\n", qPrintable(tag));
            continue;
        }
        ClipQaVerdict gold;
        QString perr;
        if (!cqParseVerdict(gf.readAll(), gold, &perr)) {
            fails << QStringLiteral("%1 gold parse: %2").arg(tag, perr);
            gf.close();
            std::fprintf(stderr, "LA_CLIP_MODEL_QA IMG %s ok=0 (gold parse)\n", qPrintable(tag));
            continue;
        }
        gf.close();

        // I-5 记档例外护栏：低置信族对生产者 GAP_MIN=0.10 走文档化例外（8 类 softmax 下
        // top1<0.20 数学上限 gap ≤ (8·top1−1)/7 < 0.0857，真实退化图不可能同时满足，
        // 见 ai_export/gen_clip_synthetic_lowconf.py 与 clip-gold-margins.txt），但对
        // 2Δ=0.04（top3 逐对容差×2 的翻序保序下限）仍须留量：金内 screen.gap_top1_top2
        // 缺字段或 ≤0.04 即响亮 FAIL。判据/容差/档位零改动，只是把潜伏脆弱性收进断言面。
        if (gold.hasNote && gold.note == QLatin1String("low_confidence")
            && (!gold.hasScreenGap || !(gold.screenGap > 0.04))) {
            fails << QStringLiteral("%1 low_confidence gold screen.gap_top1_top2 missing or <=0.04 (gap=%2)")
                         .arg(tag)
                         .arg(gold.hasScreenGap ? QString::number(gold.screenGap, 'f', 4)
                                                : QStringLiteral("<absent>"));
        }

        // 反例臂：只篡改期望表数据本身，比较/退出零分支（铁律22）。两臂并列：
        // OtherOff 先跑（咬 low_confidence 面）、MonoOff 咬 low_saturation 面，命中集互斥。
        if (tamper) { cqTamperOtherOff(gold); cqTamperMonoOff(gold); }

        // REQ <id> jpg <len> + UTF-8 路径体（CLIP 轨唯一合法 kind）。
        const QByteArray pathBytes = img.toUtf8();
        const QByteArray hdr = QByteArray("REQ ") + QByteArray::number(qint64(reqId))
                             + QByteArray(" jpg ") + QByteArray::number(pathBytes.size())
                             + QByteArray("\n");
        s.proc.write(hdr);
        s.proc.write(pathBytes);
        if (!s.proc.waitForBytesWritten(3000)) {
            fails << QStringLiteral("%1 req write failed").arg(tag);
            std::fprintf(stderr, "LA_CLIP_MODEL_QA IMG %s ok=0 (req write)\n", qPrintable(tag));
            continue;
        }

        QByteArray respHdr, body;
        const bool gotHdr = faceAiReadLine(s.proc, s.buf, respHdr, 60000);
        const QList<QByteArray> tk = respHdr.split(' ');
        if (!gotHdr || tk.size() != 3 || tk[0] != QByteArray("RES")
            || tk[1] != QByteArray::number(qint64(reqId))) {
            fails << QStringLiteral("%1 frame: %2").arg(tag, QString::fromUtf8(respHdr));
            std::fprintf(stderr, "LA_CLIP_MODEL_QA IMG %s ok=0 (frame)\n", qPrintable(tag));
            continue;
        }
        const qint64 len = tk[2].toLongLong();
        if (len <= 0 || len > (1 << 20) * 64) {      // 64MiB 守卫（同真壳 kMaxReqBody 口径）
            fails << QStringLiteral("%1 body len out of range: %2").arg(tag).arg(len);
            std::fprintf(stderr, "LA_CLIP_MODEL_QA IMG %s ok=0 (len)\n", qPrintable(tag));
            continue;
        }
        if (!faceAiReadExact(s.proc, s.buf, body, len, 60000)) {
            fails << QStringLiteral("%1 body read failed").arg(tag);
            std::fprintf(stderr, "LA_CLIP_MODEL_QA IMG %s ok=0 (body read)\n", qPrintable(tag));
            continue;
        }
        ClipQaVerdict mine;
        QString jerr;
        if (!cqParseVerdict(body, mine, &jerr)) {
            fails << QStringLiteral("%1 res parse: %2").arg(tag, jerr);
            std::fprintf(stderr, "LA_CLIP_MODEL_QA IMG %s ok=0 (res parse)\n", qPrintable(tag));
            continue;
        }
        // M-k：RES schema 首位字段 ok 必须显式为真（brief Step 1）；缺键宽容、false 响亮。
        if (mine.hasOk && !mine.okValue)
            fails << QStringLiteral("%1 ok=false response").arg(tag);
        const bool ok = cqCompareVerdict(gold, mine, fails, tag);
        if (ok) ++okCount;
        std::fprintf(stderr, "LA_CLIP_MODEL_QA IMG %s gold=(%s,%.4f) mine=(%s,%.4f) sat=%.1f vs %.1f ok=%d\n",
                     qPrintable(tag), qPrintable(gold.label), gold.score,
                     qPrintable(mine.label), mine.score, gold.sat, mine.sat, int(ok));
    }

    faceAiStop(s);

    if (fails.isEmpty())
        std::fprintf(stderr, "LA_CLIP_MODEL_QA PASS tamper=%d imgs=%d golds=%d ok=%d ready_ms=%lld total_ms=%lld\n",
                     int(tamper), images.size(), goldCount, okCount, qint64(readyMs), qint64(total.elapsed()));
    else
        std::fprintf(stderr, "LA_CLIP_MODEL_QA FAIL tamper=%d reason=%s\n",
                     int(tamper), qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
}

// ---------------- 2.38.0 R2：HEIC/WebP EXIF 容器提取门（full 层） ----------------
// 合成夹具字节确定（铁律29）：(a) WebP RIFF+EXIF chunk（直 TIFF / 带 "Exif\0\0" 前缀 两变体）
// (b) 最小 HEIC ftyp+meta(iinf/infe "Exif"+iloc v0)+mdat（4 字节 BE 偏移 + 同款 TIFF blob）。
// 反例恒开：越界盒尺寸 / 自指循环子盒 -> 不崩 + 走 mtime 回退（== 文件自身 mtime 日期）。
// 真样本臂为进程内 env 判定（LA_HEIC_SAMPLE）：无值打印 skipped（本门无 need=，合成段恒跑）。
static QByteArray exifBe16(quint16 v)
{
    QByteArray b; b.append(char(v >> 8)); b.append(char(v & 0xFF)); return b;
}
static QByteArray exifBe32(quint32 v)
{
    QByteArray b;
    for (int i = 3; i >= 0; --i) b.append(char((v >> (8 * i)) & 0xFF));
    return b;
}
static QByteArray exifBox(const char* ty, const QByteArray& payload)
{
    return exifBe32(quint32(8 + payload.size())) + QByteArray(ty, 4) + payload;
}
// 最小 TIFF（LE）：IFD0{0x8769->ExifIFD, 0x8825->GPS} / ExifIFD{0x9003="2019:07:04 12:34:56"} /
// GPS{1="N", 2=(30/1,15/1,0/1), 3="E", 4=(121/1,30/1,0/1)}  —— 固定布局常量偏移。
static QByteArray exifTiffFixture()
{
    QByteArray b;
    auto u16 = [&](quint16 v){ b.append(char(v & 0xFF)); b.append(char(v >> 8)); };
    auto u32 = [&](quint32 v){ for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xFF)); };
    auto entry = [&](quint16 tag, quint16 ty, quint32 cnt, quint32 val){
        u16(tag); u16(ty); u32(cnt); u32(val);
    };
    b.append("II", 2); u16(42); u32(8);                       // 头：IFD0 @ 8
    u16(2);                                                   // IFD0：2 条目
    entry(0x8769, 4, 1, 38);                                  // ExifIFD @ 38
    entry(0x8825, 4, 1, 76);                                  // GPS IFD @ 76
    u32(0);                                                   // next IFD
    u16(1);                                                   // ExifIFD：1 条目
    entry(0x9003, 2, 20, 56);                                 // DateTimeOriginal @ 56
    u32(0);
    b.append("2019:07:04 12:34:56\0", 20);                    // 56..75
    u16(4);                                                   // GPS IFD：4 条目
    entry(1, 2, 2, quint32('N'));                             // 内联 "N\0"
    entry(2, 5, 3, 130);                                      // lat RATIONAL x3 @ 130
    entry(3, 2, 2, quint32('E'));                             // 内联 "E\0"
    entry(4, 5, 3, 154);                                      // lon RATIONAL x3 @ 154
    u32(0);
    const quint32 lats[6] = { 30, 1, 15, 1, 0, 1 };
    for (quint32 v : lats) u32(v);                            // 130..153
    const quint32 lons[6] = { 121, 1, 30, 1, 0, 1 };
    for (quint32 v : lons) u32(v);                            // 154..177
    return b;                                                 // 全长 178
}
static QByteArray exifWebpFixture(bool withPrefix)
{
    QByteArray payload = withPrefix ? QByteArray("Exif\0\0", 6) : QByteArray();
    payload += exifTiffFixture();
    QByteArray chunk = QByteArray("EXIF", 4);
    const quint32 len = quint32(payload.size());
    for (int i = 0; i < 4; ++i) chunk.append(char((len >> (8 * i)) & 0xFF));   // RIFF 小端
    chunk += payload;
    if (len & 1u) chunk.append('\0');                         // chunk 2 字节对齐
    QByteArray body = QByteArray("WEBP", 4) + chunk;
    QByteArray f = QByteArray("RIFF", 4);
    const quint32 sz = quint32(body.size());
    for (int i = 0; i < 4; ++i) f.append(char((sz >> (8 * i)) & 0xFF));
    f += body;
    return f;
}
static QByteArray exifHeicFixture()
{
    const QByteArray tiff = exifTiffFixture();
    QByteArray mdatPayload(4, '\0');                          // 前 4 字节 BE：到 TIFF 头偏移 = 0
    mdatPayload += tiff;
    QByteArray infeBody;                                      // infe v2: item_ID=1, type "Exif"
    infeBody.append(char(2)).append(3, '\0');
    infeBody += exifBe16(1); infeBody += exifBe16(0);
    infeBody.append("Exif", 4); infeBody.append('\0');
    QByteArray iinfBody; iinfBody.append(4, '\0'); iinfBody += exifBe16(1);
    iinfBody += exifBox("infe", infeBody);
    QByteArray ilocBody; ilocBody.append(4, '\0');            // iloc v0
    ilocBody.append(char(0x44)).append(char(0x00));           // 4/4 尺寸；base 0 / index 0
    ilocBody += exifBe16(1);                                  // 1 item
    ilocBody += exifBe16(1); ilocBody += exifBe16(0); ilocBody += exifBe16(1);  // id/dref/extCnt
    const int offFieldAt = ilocBody.size();                   // extent offset 占位位置（体内）
    ilocBody += exifBe32(0);                                  // 占位：绝对偏移后补
    ilocBody += exifBe32(quint32(mdatPayload.size()));        // extent length
    QByteArray metaBody; metaBody.append(4, '\0');
    metaBody += exifBox("iinf", iinfBody);
    metaBody += exifBox("iloc", ilocBody);
    QByteArray ftypBody; ftypBody.append("heic", 4);
    ftypBody.append(4, '\0'); ftypBody.append("mif1", 4);
    const QByteArray ftyp = exifBox("ftyp", ftypBody);
    const QByteArray meta = exifBox("meta", metaBody);
    const QByteArray mdat = exifBox("mdat", mdatPayload);
    QByteArray file = ftyp + meta + mdat;
    const quint32 abs = quint32(ftyp.size() + meta.size() + 8);   // mdat payload 绝对偏移
    const int absInFile = ftyp.size() + 8 + 4 + exifBox("iinf", iinfBody).size() + 8 + offFieldAt;
    for (int i = 0; i < 4; ++i) file[absInFile + i] = char((abs >> (8 * (3 - i))) & 0xFF);
    return file;
}
static bool exifWriteFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    return f.write(data) == data.size();
}

int runExifContainerQa()
{
    QStringList fails;
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::fprintf(stderr, "LA_EXIF_CONTAINER_QA FAIL tmpdir\n"); return 1; }
    const auto check = [&](bool ok, const QString& what) {
        if (!ok) fails << what;
    };
    // 简报原文 "const QString url = [&]..." 无法通过类型检查（lambda 不能初始化 QString），
    // 按意图取 const auto，其余逐字照抄。
    const auto url = [&](const QString& name){ return tmp.filePath(name); };

    // ---- 正例：WebP 两变体 + HEIC（期望值 = 夹具写死值，铁律22：不随任何开关变）----
    const QString f1 = url(QStringLiteral("a.webp"));
    const QString f2 = url(QStringLiteral("b.webp"));
    const QString f3 = url(QStringLiteral("c.heic"));
    if (!exifWriteFile(f1, exifWebpFixture(false))
        || !exifWriteFile(f2, exifWebpFixture(true))
        || !exifWriteFile(f3, exifHeicFixture())) {
        std::fprintf(stderr, "LA_EXIF_CONTAINER_QA FAIL fixture write\n");
        return 1;
    }
    for (const QString& p : { f1, f2, f3 }) {
        const ExifData e = ExifReader::readExif(p);
        check(e.d == QStringLiteral("2019-07-04"), QStringLiteral("date %1 got=%2").arg(p, e.d));
        check(e.tm == QStringLiteral("12:34"), QStringLiteral("time %1 got=%2").arg(p, e.tm));
        check(e.hasGps, QStringLiteral("gps %1").arg(p));
        check(qAbs(e.la - 30.25) < 1e-4, QStringLiteral("lat %1 got=%2").arg(p).arg(e.la));
        check(qAbs(e.lo - 121.5) < 1e-4, QStringLiteral("lon %1 got=%2").arg(p).arg(e.lo));
    }

    // ---- 反例（恒开）：越界盒尺寸 / 自指循环子盒 -> 不崩 + mtime 回退 ----
    {
        QByteArray broken = exifHeicFixture();
        for (int i = 0; i < 4; ++i) broken[i] = char(0x7F);          // 顶层盒尺寸 0x7FFFFFFF 越界
        const QString bp = url(QStringLiteral("bad1.heic"));
        exifWriteFile(bp, broken);
        const ExifData e = ExifReader::readExif(bp);
        const QString mdate = QFileInfo(bp).lastModified().date().toString(QStringLiteral("yyyy-MM-dd"));
        check(e.d == mdate, QStringLiteral("counter out-of-range: fallback got=%1 expect=%2").arg(e.d, mdate));
    }
    {
        QByteArray loop = exifHeicFixture();
        // iinf 盒位于 meta 内：ftyp + meta 头 8 + FullBox 4 之后；把它的 size 改为覆盖 meta 外
        const int iinfSizeAt = exifBox("ftyp", QByteArray("heic",4) + QByteArray(4,'\0') + QByteArray("mif1",4)).size() + 8 + 4;
        for (int i = 0; i < 4; ++i) loop[iinfSizeAt + i] = char(0x7F);  // 子盒越界/自指 -> 放弃
        const QString bp = url(QStringLiteral("bad2.heic"));
        exifWriteFile(bp, loop);
        const ExifData e = ExifReader::readExif(bp);
        const QString mdate = QFileInfo(bp).lastModified().date().toString(QStringLiteral("yyyy-MM-dd"));
        check(e.d == mdate, QStringLiteral("counter self-loop: fallback got=%1 expect=%2").arg(e.d, mdate));
    }

    // ---- 真样本臂：进程内 env 判定（无 need=；本地路径值不入仓）----
    QString realLine = QStringLiteral("skipped");
    const QByteArray sample = qgetenv("LA_HEIC_SAMPLE");
    if (!sample.isEmpty()) {
        const ExifData sd = ExifReader::readExif(QString::fromLocal8Bit(sample));
        if (sd.d.isEmpty()) { fails << QStringLiteral("realSample no date parsed"); realLine = QStringLiteral("fail"); }
        else realLine = QStringLiteral("ok");
    }

    if (fails.isEmpty())
        std::fprintf(stderr, "LA_EXIF_CONTAINER_QA PASS webp=2 heic=1 counter=2 realSample=%s\n",
                     qPrintable(realLine));
    else
        std::fprintf(stderr, "LA_EXIF_CONTAINER_QA FAIL reason=%s\n", qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
}

// 2.35.0 真模型质量门（opt-in）。前提由名册 need=@("LA_LAMA_MODEL") 把关：变量缺失时
// qa_runner 直接 [SKIP]，不启动本门。启动即意味着用户声称模型已 fetch：
// 此时若执行器仍不存在、或解析到的是 stub，都必须诚实 FAIL（绝不空心放行，铁律34）。
// M-A mask 外（1px 膨胀容差外）逐字节守恒；M-B mask 内确有改动且填充非随机垃圾
// （平滑度判据，见下方 2026-10-11 实测注）；M-C 同输入两次提交逐通道 ±1 幂等。
// 反例 LA_ERASE_MODEL_QA_FORCE_BROKEN=1 → 全部转红。
int runEraseModelQa()
{
    const bool broken = !qEnvironmentVariableIsEmpty("LA_ERASE_MODEL_QA_FORCE_BROKEN");
    QStringList fails;
    const auto note = [&fails](bool ok, const QString& w){ if (!ok) fails << w; };

    EraseRunner runner;
    if (!runner.ready()) {
        std::fprintf(stderr, "LA_ERASE_MODEL_QA FAIL executor unavailable though LA_LAMA_MODEL set\n");
        std::fflush(stderr); return 1;
    }
    if (!runner.exePath().endsWith(QStringLiteral("inpaint.exe"))) {
        std::fprintf(stderr, "LA_ERASE_MODEL_QA FAIL resolved executor is stub, not real inpaint.exe: %s\n",
                     qPrintable(runner.exePath()));
        std::fflush(stderr); return 1;
    }
    if (broken) EraseRunner::qaInjectBadSize(true);   // 首次提交前打开

    const int w = 256, h = 256;
    QImage base(w, h, QImage::Format_RGB32);          // 确定性渐变（宿主无关，铁律29）
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
        base.setPixel(x, y, qRgb((x + y) & 0xff, (x * 2) & 0xff, (y * 3) & 0xff));
    QImage mask(w, h, QImage::Format_ARGB32);
    mask.fill(qRgba(0, 0, 0, 0));
    for (int y = 96; y <= 160; ++y) for (int x = 96; x <= 160; ++x)
        mask.setPixel(x, y, qRgba(0, 0, 0, 255));     // 65x65 实心方块

    QImage out1; QString err;
    if (!runner.run(1, base, mask, out1, &err) || out1.isNull() || out1.size() != base.size()) {
        std::fprintf(stderr, "LA_ERASE_MODEL_QA FAIL first run: %s\n", qPrintable(err));
        std::fflush(stderr);
        EraseRunner::qaInjectBadSize(false);
        return 1;
    }
    QImage out2;
    const bool ok2 = runner.run(2, base, mask, out2, &err);

    // mask 3x3 膨胀（真模型羽化可能轻推紧邻像素；膨胀环带之外必须逐字节相同）
    auto dilated = [](const QImage& m, int x, int y) {
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= m.width() || yy >= m.height()) continue;
            if (qAlpha(m.pixel(xx, yy)) == 255) return true;
        }
        return false;
    };
    int farDiff = 0, inDiff = 0, inTot = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const QRgb a = base.pixel(x, y), b = out1.pixel(x, y);
        const bool inside = qAlpha(mask.pixel(x, y)) == 255;
        if (inside) {
            ++inTot;
            if (a != b) ++inDiff;
        } else if (!dilated(mask, x, y) && a != b) {
            ++farDiff;
        }
    }
    // M-B 第二判据=填充平滑度（mask 内水平相邻对的平均通道绝对差）。
    // 校准史（2026-10-11 真模型首测）：原写法是"mask 内与底图平均距离<40"，但那对
    // inpainting 认识论上就错——mask 内原内容已被抹除、无 ground truth，真模型对合成
    // 渐变底的合理填充距底图实测 71（随机噪声也只有 85，40 阈值毫无判别力）。平滑度
    // 方向才成立：真模型合理填充相邻差实测 3.9（该底图自身 3.6），随机垃圾 84.8，
    // 同一数值 40 变为有 10x+ 裕度的"非乱填"守卫。反例臂不受影响（BADSIZE 首跑即败）。
    double fillSmooth = 0.0; int smoothCnt = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x + 1 < w; ++x) {
        if (qAlpha(mask.pixel(x, y)) != 255 || qAlpha(mask.pixel(x + 1, y)) != 255) continue;
        const QRgb l = out1.pixel(x, y), r = out1.pixel(x + 1, y);
        fillSmooth += (qAbs(qRed(l) - qRed(r)) + qAbs(qGreen(l) - qGreen(r)) + qAbs(qBlue(l) - qBlue(r))) / 3.0;
        ++smoothCnt;
    }
    note(farDiff == 0, QStringLiteral("M-A conservation outside dilated mask, diffs=%1").arg(farDiff));
    note(inTot > 0 && double(inDiff) / inTot >= 0.6,
         QStringLiteral("M-B mask-in change ratio=%1").arg(inTot ? double(inDiff) / inTot : -1.0));
    note(smoothCnt > 0 && fillSmooth / smoothCnt < 40.0,
         QStringLiteral("M-B fill smoothness mean-abs-neighbor-delta=%1 (wild fill?)")
             .arg(smoothCnt ? fillSmooth / smoothCnt : -1.0));

    // M-C 幂等：同输入两次，逐通道 ±1
    int maxCh = 0;
    if (ok2 && !out2.isNull() && out2.size() == out1.size()) {
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const QRgb p = out1.pixel(x, y), q = out2.pixel(x, y);
            maxCh = qMax(maxCh, qAbs(qRed(p) - qRed(q)));
            maxCh = qMax(maxCh, qAbs(qGreen(p) - qGreen(q)));
            maxCh = qMax(maxCh, qAbs(qBlue(p) - qBlue(q)));
        }
        note(maxCh <= 1, QStringLiteral("M-C idempotence max channel delta=%1").arg(maxCh));
    } else {
        note(false, QStringLiteral("M-C second run unavailable: %1").arg(err));
    }

    EraseRunner::qaInjectBadSize(false);
    if (fails.isEmpty())
        std::fprintf(stderr, "LA_ERASE_MODEL_QA PASS broken=%d conserve=1 fill=1 idempotent=1\n", int(broken));
    else
        std::fprintf(stderr, "LA_ERASE_MODEL_QA FAIL broken=%d reason=%s\n",
                     int(broken), qPrintable(fails.join(QChar(';'))));
    std::fflush(stderr);
    return fails.isEmpty() ? 0 : 1;
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

    // ② 跑检测（合成钩子 → 绕过网络，经 AiWriter 投递 setPersons 写回）。
    //   门在主线程：processEvents 让 QueuedConnection 写回落地。
    QObject qaCtx;
    AiWriter writer(&lib, &qaCtx);
    const bool ok = PersonDetector::detect(lib.photos(), writer);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    // 钩子支契约腿（诚实定位，复审 Minor「合成钩子下恒真」）：setSyntheticForQa 在函数
    // 首行早退并 return true，这条**不**证明真路径的返回值语义——真路径的 false 由
    // #75 G9 的 ERR/取消/干净三臂物理咬住。留着只为归因：钩子支哪天改成 false，
    // 后续读回断言会成片红，这条先把结论钉在「钩子本身坏了」上。
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
    // ---- R3 决定性子断言：串行（LA_LUMEN_THREADS=1）vs 默认并行，逐字节 memcmp 全等。
    // 强于 MAD：任何跨块写竞态/误并行归约即使 MAD 仍在容差内也必红（铁律34）。
    // 反例：LA_LUMEN_SMOKE_FORCE_DIFF=1 只损坏串行基准一字节；期望表冻结"必须相等"，绝不随开关变（铁律22）。
    {
        const QImage dIn(QDir(dir).filePath(QStringLiteral("in_0.png")));
        if (!dIn.isNull()) {
            qputenv("LA_LUMEN_THREADS", "1");
            const QImage sLens = LumenEngine::lensCorrect(dIn);
            const QImage sVig  = LumenEngine::vignetteCorrect(dIn);
            const QImage sGlow = LumenEngine::glowStyle(dIn);
            const QImage sHdr  = LumenEngine::toneMapHdr(dIn);
            const QImage sDeh  = LumenEngine::dehaze(dIn);
            const QImage sPor  = LumenEngine::portraitBeautify(dIn);
            qunsetenv("LA_LUMEN_THREADS");
            const QImage pLens = LumenEngine::lensCorrect(dIn);
            const QImage pVig  = LumenEngine::vignetteCorrect(dIn);
            const QImage pGlow = LumenEngine::glowStyle(dIn);
            const QImage pHdr  = LumenEngine::toneMapHdr(dIn);
            const QImage pDeh  = LumenEngine::dehaze(dIn);
            const QImage pPor  = LumenEngine::portraitBeautify(dIn);
            auto cmp = [&](const char* nm, QImage a, const QImage& b) {
                if (qEnvironmentVariableIsSet("LA_LUMEN_SMOKE_FORCE_DIFF") && !a.isNull())
                    a.scanLine(0)[0] ^= uchar(0x01);   // a 为按值句柄，scanLine 触发私有 detach，不污染 s* 原图
                const bool eq = a.size() == b.size() && a.bytesPerLine() == b.bytesPerLine()
                    && a.sizeInBytes() == b.sizeInBytes()
                    && std::memcmp(a.constBits(), b.constBits(), size_t(a.sizeInBytes())) == 0;
                std::fprintf(stderr, "LA_LUMEN_DET %s equal=%d\n", nm, eq ? 1 : 0);
                if (!eq) pass = false;
            };
            cmp("lens", sLens, pLens);
            cmp("vignette", sVig, pVig);
            cmp("glow", sGlow, pGlow);
            cmp("hdr", sHdr, pHdr);
            cmp("dehaze", sDeh, pDeh);
            cmp("portrait", sPor, pPor);
        } else {
            // in_0.png 缺失时整段 DET 空跑：显式留痕，防门 PASS 却无逐字节证据（空心）
            std::fprintf(stderr, "LA_LUMEN_DET skipped reason=no_in_0\n");
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
        int checked = 0;
        const auto i18nCheck = [&](const char* src, const QString& expect) {
            ++checked;
            const QString got = QCoreApplication::translate("LightAlbum", src);
            // 三态判别：① ==期望英文 ② !=源码（缺译时 Qt 回落源码）③ 二者同时成立才算命中。
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
        // 2.33.0 补全的 CJK 串各抽一枚代表做命中断言（缺译→回落源码→变红），
        // 覆盖新入册的方向 C / 故事 / 选片去重 / 视频 四条线。
        i18nCheck("识别人物", QStringLiteral("Detect People"));
        i18nCheck("智能聚类", QStringLiteral("Smart Clustering"));
        i18nCheck("全部故事 %1", QStringLiteral("All Stories %1"));
        i18nCheck("已取消相似照片分析", QStringLiteral("Similar-photo analysis cancelled"));
        i18nCheck("正在查找相似照片（%1 张）…", QStringLiteral("Finding similar photos (%1)…"));
        i18nCheck("只看视频", QStringLiteral("Videos Only"));
        i18nCheck("重命名人物", QStringLiteral("Rename Person"));
        // 2.40.0 T4b：人脸 HTTP 桥净替换后旧串「人脸服务未就绪」退役，
        //   换代为新「AI 引擎未就绪」长文案代表断言（缺译→回落源码→变红）。
        i18nCheck("AI 引擎未就绪（引擎或模型缺失，或引擎启动失败），再点「识别人物」。照片仅在本机处理，绝不外传。",
                  QStringLiteral("AI engine not ready (engine or model missing, or the engine failed to start), then click \u201cDetect People\u201d. Photos are processed only on this machine and never sent out."));
        // 2.40.0 T8：分类外部服务桥净替换后旧标题串（陈述「服务未就绪」）与旧正文串
        //   （引导用户去启动外部分类服务，含 .bat 路径）双双退役，换代为第三条
        //   「AI 引擎未就绪」双胞胎（clipinfer 侧）代表断言（缺译→回落源码→变红；
        //   标题改指已译功能名「AI 分类」）。
        i18nCheck("AI 引擎未就绪（引擎或模型缺失，或引擎启动失败），再点「AI 分类」。照片仅在本机处理，绝不外传。",
                  QStringLiteral("AI engine not ready (engine or model missing, or the engine failed to start), then click \u201cAI Classify\u201d. Photos are processed only on this machine and never sent out."));
        // 2.40.0 T12 修复波 FIX-7：「智能聚类」版引擎串此前无孪生断言，补第 14 条，
        //   与 MainWindow.cpp / LightAlbum_en.ts 的三条「AI 引擎未就绪」逐字对齐（
        //   缺译→回落源码→变红）。
        i18nCheck("AI 引擎未就绪（引擎或模型缺失，或引擎启动失败），再点「智能聚类」。照片仅在本机处理，绝不外传。",
                  QStringLiteral("AI engine not ready (engine or model missing, or the engine failed to start), then click \u201cSmart Clustering\u201d. Photos are processed only on this machine and never sent out."));
        // 2.40.0 T12 修复波第 2 轮 FIX-17：这两条「中断」串是 T4b 净替换时被删代码路
        //   留下的化石（`.ts`/`.qm` 里有条目有译文、代码零消费者）——即「UI 少了一条支路」
        //   的物证。三态恢复后重新接线，各补一条孪生断言钉住（第 15/16 条）。
        i18nCheck("识别过程中断（可能已部分写入）。",
                  QStringLiteral("Detection interrupted (partial results may have been written)."));
        i18nCheck("聚类过程中断（可能已部分写入）。",
                  QStringLiteral("Clustering interrupted (partial results may have been written)."));
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
            std::fprintf(stderr, "LA_I18N_QA PASS entries=%d\n", checked);
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
            {"hint Lumen::MUTED on Bg",    Lumen::MUTED, Dark::Bg,     4.5},
            {"lightbox meta #9db2a5 on bottom", QColor(0x9d,0xb2,0xa5), QColor(0x04,0x06,0x04), 4.5},
            // 缺陷②回归：导入菜单逐实例暗色 QSS 配色必须过 WCAG（防止白底白字回潮）
            {"importMenu item #d9e4dc on #0e130f",   QColor(0xd9,0xe4,0xdc), QColor(0x0e,0x13,0x0f), 4.5},
            {"importMenu selected #4fc08a on #1d2a22",QColor(0x4f,0xc0,0x8a), QColor(0x1d,0x2a,0x22), 4.5},
            {"importMenu disabled MUTED on #0e130f",Lumen::MUTED, QColor(0x0e,0x13,0x0f), 4.5},
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

    // 2.38.0 R1：一键增强 13 算子接线门（静态扫描 src/ui；接线前必红）
    if (qEnvironmentVariableIsSet("LA_LUMEN_OPS_UI_QA"))
        return runLumenOpsUiQa();

    // 2.38.0 R2：HEIC/WebP EXIF 容器提取门（合成夹具 + 恒开反例；实现前必红）
    if (qEnvironmentVariableIsSet("LA_EXIF_CONTAINER_QA"))
        return runExifContainerQa();

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

    // 2.33.0 隐私门：灯箱翻图 / 选片页 / 去重对话框三处锁态契约（offscreen 直调）
    if (qEnvironmentVariableIsSet("LA_PRIVACY_QA"))
        return runPrivacyQa();

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

    // 2.33.0 线程安全：AI 异步写回的单写者线程归属门禁（从真实 worker 线程经 AiWriter 投递）
    if (qEnvironmentVariableIsSet("LA_GUI_THREAD_QA"))
        return runGuiThreadQa();

    // 人物故事方向·B：自动故事相册（地点 + 时间窗口聚合）→ 守恒契约 + 检索契约门禁
    if (qEnvironmentVariableIsSet("LA_STORY_QA"))
        return runStoryQa();

    // 2.22.0 客户端体验：骨架屏纯函数 + 像素判别 + 灯箱原图解码契约
    if (qEnvironmentVariableIsSet("LA_TRANSITION_QA"))
        return runTransitionQa();

    // 2.34.0 动效引擎：曲线/错峰/预设/回归段/粒子像素契约（纯函数同源）
    if (qEnvironmentVariableIsSet("LA_MOTION_QA"))
        return runMotionQa();

    if (qEnvironmentVariableIsSet("LA_ERASE_QA"))
        return runEraseQa();

    if (qEnvironmentVariableIsSet("LA_ERASE_MODEL_QA"))
        return runEraseModelQa();

    // 第四轮 T1 第 75 门：AiProto 帧协议骨架门（QProcess 直起 faceinfer_stub，G1/G2/G4/G5）
    if (qEnvironmentVariableIsSet("LA_FACE_AI_QA"))
        return runFaceAiQa();

    // 第四轮 T3 第 78 门：人脸模型端到端五列对表门（真 faceinfer.exe + T2 金标注，opt-in）
    if (qEnvironmentVariableIsSet("LA_FACE_MODEL_QA"))
        return runFaceModelQa();

    // 第四轮 T7 第 76 门：CLIP 轨 AiProto 契约门（clipinfer_stub 直起 + 真壳 prompts 反例 G1-G10）
    if (qEnvironmentVariableIsSet("LA_CLIP_AI_QA"))
        return runClipAiQa();

    // 第四轮 T7 第 77 门：CLIP 模型端到端五列对表门（真 clipinfer.exe + T7 金标，opt-in）
    if (qEnvironmentVariableIsSet("LA_CLIP_MODEL_QA"))
        return runClipModelQa();

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
        && !qEnvironmentVariableIsSet("LA_MAP_SMOKE")
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
    // 2.33 屏幕适配：默认 1440×940，但**绝不超出屏幕可用区**。HiDPI 缩放
    // （125%/150%）与小屏下逻辑可用区常不足 1440×940，原实现硬按默认尺寸
    // 居中会把窗口底部/右侧顶出屏幕，用户必须手动最大化才能看清（实测反馈）。
    // 规则：窗口 clamp 到可用区（留 16px 呼吸边）；可用区不足默认 3/4 时
    // 视为小屏，放弃展开动画直接最大化启动（几何动画会把 showMaximized
    // 拉回固定 rect，两者不能混用，故 tiny 屏不走动画路径）。
    const QSize want(1440, 940);
    const bool tinyScreen = avail.width()  < want.width()  * 3 / 4
                         || avail.height() < want.height() * 3 / 4;
    const QSize fit(qMin(want.width(),  qMax(320, avail.width()  - 16)),
                    qMin(want.height(), qMax(240, avail.height() - 16)));
    QRect fullRect((avail.width() - fit.width()) / 2,
                   (avail.height() - fit.height()) / 2,
                   fit.width(), fit.height());

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

    if (animOk && !tinyScreen) {
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

    // 非动画（冒烟/截图/小屏）：直接显示并保留原有门控
    if (tinyScreen) {
        win.showMaximized();   // 小屏：直接最大化，绝不让窗口出屏
    } else {
        win.setGeometry(fullRect);
        win.show();
    }

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

    // 结构烟雾：地理数据装载断言在 MapView::loadGeoJson 内已打印（带 PASS/FAIL 判定），
    // 这里只负责收口自退 —— 此前它打印完继续进 app.exec() 永久空转，所以 2.2.1 把它从
    // runner 里摘了；现在两条缺陷（无判定 + 不自退）都已补齐，重新入门禁名册。
    if (!qEnvironmentVariableIsEmpty("LA_MAP_SMOKE")) {
        std::fprintf(stderr, "LA_MAP_SMOKE_DONE\n");
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

    // T11 弹层打开态首次取证：LA_UI_SHOT_POPUP=<dir> → offscreen 合成去重对话框/视频灯箱，
    // 同实例双抓（E2 生效 vs setEnabled(false)）逐像素差分，机械裁决「阴影是否真的进像素」
    // （诚实律：wired≠painted）。草稿区专用、只在显式 env 组合触发，绝不入库。
    if (qEnvironmentVariableIsSet("LA_UI_SHOT_POPUP")) {
        const QString dir = qEnvironmentVariable("LA_UI_SHOT_POPUP");
        QDir().mkpath(dir);
        auto probe = [&dir](QWidget* w, const QString& name) {
            w->show();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 300);
            QImage on = w->grab().toImage();
            if (QGraphicsEffect* fx = w->graphicsEffect()) fx->setEnabled(false);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            const QImage off = w->grab().toImage();
            int diff = 0;
            for (int y = 0; y < on.height(); ++y)
                for (int x = 0; x < on.width(); ++x)
                    diff += on.pixelColor(x, y) != off.pixelColor(x, y);
            on.save(QDir(dir).filePath(QStringLiteral("shot-e2-%1.png").arg(name)));
            off.save(QDir(dir).filePath(QStringLiteral("shot-e2-%1-off.png").arg(name)));
            std::fprintf(stderr, "LA_E2_PROBE %s size=%dx%d diffPx=%d/%d\n",
                         qPrintable(name), on.width(), on.height(), diff,
                         on.width() * on.height());
            std::fflush(stderr);
            if (QGraphicsEffect* fx = w->graphicsEffect()) fx->setEnabled(true);
        };
        QTemporaryDir td;
        QImage im(64, 64, QImage::Format_ARGB32);
        im.fill(QColor(200, 200, 200));
        QVector<Photo> ps;
        for (int i = 1; i <= 4; ++i) {   // 4 张字节相同合成缩略图 → 必出 1 组重复
            const QString k = QStringLiteral("e2%1.png").arg(i);
            im.save(QDir(td.path()).filePath(k));
            Photo p;
            p.path = QStringLiteral("synth/e2/%1.jpg").arg(i);
            p.thumbFile = k;
            p.dim = QStringLiteral("800x600");
            ps.append(p);
        }
        { DedupDialog dlg(&ps, td.path(), true, &win); dlg.qaWaitFinished(8000); probe(&dlg, QStringLiteral("dedup")); }
        { VideoLightbox lb(&win); probe(&lb, QStringLiteral("lightbox")); }
        if (qEnvironmentVariableIsSet("LA_UI_SHOT_POPUP_MENU")) {   // 单独探针：QMenu offscreen 光栅化历史挂死（缺陷②注记）
            QMenu m(&win);
            m.addAction(QString::fromUtf8("\xE7\x9B\xB8\xE4\xBC\xBC\xE7\x85\xA7\xE7\x89\x87"));   // 探针专用非译串，不进 i18n
            m.addAction(QString::fromUtf8("\xE5\x85\xB3\xE9\x97\xAD"));
            Lumen::Elev::apply(&m, Lumen::Elev::E2);
            probe(&m, QStringLiteral("menu"));
        }
        return 0;
    }

    // 冒烟探针门控：载入真库 + 页面栈就位后自动退出（offscreen/CI 可用）。
    // 选片自测（LA_CULL_SMOKE）/ 修图自测（LA_RETOUCH_SMOKE）
    // 由各自页面自行驱动退出，这里不抢跑。
    if (qEnvironmentVariableIsSet("LA_SMOKE") && qEnvironmentVariableIsEmpty("LA_CULL_SMOKE")
        && qEnvironmentVariableIsEmpty("LA_RETOUCH_SMOKE")) {
        // P29-H：首页（零 chrome 纯沉浸）应隐藏窗口控制簇——同步打印，避开 600ms 自退定时器可能的挂起
        const bool winCtlHiddenOnHome = !win.isWinCtlVisible();
        // 缺陷②回归：导入下拉菜单是独立顶层弹窗，不继承应用暗色 QSS → 曾经白底白字。
        // 断言每个 QMenu 都带暗色弹层底：T7 token 化后导入菜单走 §1.7 回家映射
        //（0e130f→PANEL，运行时解析为 #0c100d）；Theme.h 芯片菜单工厂（棘轮豁免文件）
        // 仍携旧 literal #0e130f。两枚底色任一命中即算带暗色 QSS，门禁语义不变：
        // 任一菜单回退系统浅色原生底 → 门红。
        int menuTotal = 0, menuDark = 0;
        const QString menuBgToken = Lumen::hex(Lumen::PANEL).mid(1);  // "0c100d"
        for (QMenu* m : win.findChildren<QMenu*>()) {
            ++menuTotal;
            const QString ss = m->styleSheet();
            if (ss.contains(QStringLiteral("0e130f")) || ss.contains(menuBgToken)) ++menuDark;
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
