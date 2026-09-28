// P9 LumenAgent 实现 —— brain.py（LocalLLMBrain / LocalBrain）的 C++ 移植
#include "LumenAgent.h"
#include "LumenEngine.h"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QTimer>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// 13 参数白名单（brain.editplan.PARAM_KEYS；与 retouchWithPlan 契约一致）
const char* const kParamKeys[] = {
    "brightness", "contrast", "saturation", "warmth", "sharpness", "denoise",
    "highlights", "shadows", "whites", "blacks", "vibrance", "clarity", "tint",
};
constexpr int kParamN = int(sizeof(kParamKeys) / sizeof(kParamKeys[0]));

double clamp1(double v) { return std::max(-1.0, std::min(1.0, v)); }

// 零云端铁律：只允许回环地址（brain.is_local_url 同款）
bool isLocalUrl(const QString& url)
{
    const QString host = QUrl(url).host().toLower();
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost")
        || host == QLatin1String("::1") || host == QLatin1String("0.0.0.0");
}

// 裸 socket 快速探端口（brain._port_open 同款：未监听秒退，不白等 HTTP timeout）
bool portOpen(const QUrl& url, int ms = 350)
{
    QTcpSocket s;
    s.connectToHost(url.host(), url.port(80));
    return s.waitForConnected(ms);
}

QString llmUrl()    { return qEnvironmentVariable("LA_LLM_URL", QStringLiteral("http://127.0.0.1:11434")); }
QString llmModel()  { return qEnvironmentVariable("LA_LLM_MODEL", QStringLiteral("qwen3.5:4b")); }

// LLM_SYSTEM（brain.py 同语义）+ 明确 13 键与量纲
QString systemPrompt()
{
    return QStringLiteral(
        "你是照片修图的参数决策器。只输出一个 JSON 对象，不要解释、不要 markdown 代码块。"
        "JSON 的键只能取自：brightness、contrast、saturation、warmth、sharpness、denoise、"
        "highlights、shadows、whites、blacks、vibrance、clarity、tint。"
        "值是 -1.0 到 1.0 的浮点数，0 表示不改，正值=提亮/加对比/加饱和/加暖。"
        "可以额外给一个 \"_intent\" 字段（中文字符串）说明意图。");
}

QString userPrompt(const AgentContext& c)
{
    return QStringLiteral(
        "画面客观测量：亮度 %1（0 全黑 / 1 全白）· 离散 %2 · 平均饱和 %3 · "
        "高光占比 %4 · 阴影占比 %5 · 色偏指数 %6（正=偏暖/偏红，负=偏冷/偏蓝）· "
        "雾度 %7 · 噪声 %8 · ISO %9 · 快门 %10s\n"
        "用户目标：%11\n\n"
        "请只输出参数增量 JSON。数值方向要与测量一致：偏暗就正 brightness/shadows，"
        "偏亮就负 highlights/whites，发灰就正 contrast/clarity，色偏用 warmth/tint 反向校正。"
        "示例：{\"brightness\": 0.2, \"shadows\": 0.3, \"_intent\": \"补光提亮\"}")
        .arg(c.lum, 0, 'f', 3).arg(c.lum_std, 0, 'f', 3).arg(c.sat, 0, 'f', 3)
        .arg(c.hi, 0, 'f', 3).arg(c.lo, 0, 'f', 3)
        .arg(c.cast, 0, 'f', 3).arg(c.haze, 0, 'f', 3).arg(c.noise, 0, 'f', 3)
        .arg(c.iso).arg(c.shutter, 0, 'f', 3)
        .arg(c.goal.trimmed().isEmpty() ? QStringLiteral("（未指定，按画面问题自行判断）")
                                        : c.goal.trimmed());
}

// Ollama /api/chat 调用（本地；think:false 关思考，format:json 强制 JSON）
AgentPlan decideOllama(const AgentContext& ctx, const QUrl& base, const QString& model)
{
    AgentPlan plan;
    QUrl url = base;
    url.setPath(QStringLiteral("/api/chat"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("stream"), false);
    body.insert(QStringLiteral("think"), false);      // 思考型模型吃光预算（实测 qwen3.5:4b）
    body.insert(QStringLiteral("format"), QStringLiteral("json"));
    QJsonObject opts;
    opts.insert(QStringLiteral("temperature"), 0);
    opts.insert(QStringLiteral("num_predict"), 400);
    body.insert(QStringLiteral("options"), opts);
    QJsonArray msgs;
    QJsonObject sys; sys.insert(QStringLiteral("role"), QStringLiteral("system"));
    sys.insert(QStringLiteral("content"), systemPrompt());
    QJsonObject usr; usr.insert(QStringLiteral("role"), QStringLiteral("user"));
    usr.insert(QStringLiteral("content"), userPrompt(ctx));
    msgs.append(sys); msgs.append(usr);
    body.insert(QStringLiteral("messages"), msgs);

    QElapsedTimer timer; timer.start();
    QNetworkAccessManager nam;
    QEventLoop loop;
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);   // LLM 冷加载可能很慢
    QNetworkReply* rep = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (rep->error() != QNetworkReply::NoError) {
        plan.reason = QStringLiteral("Ollama 调用失败：%1").arg(rep->errorString());
        rep->deleteLater();
        return plan;
    }
    const QByteArray raw = rep->readAll();
    rep->deleteLater();
    const QJsonObject root = QJsonDocument::fromJson(raw).object();
    const QString content = root.value(QStringLiteral("message"))
                                .toObject().value(QStringLiteral("content")).toString();
    const QJsonObject obj = QJsonDocument::fromJson(content.toUtf8()).object();
    if (obj.isEmpty()) {
        plan.reason = QStringLiteral("LLM 输出不是合法 JSON 对象");
        return plan;
    }
    QMap<QString, double> p;
    for (int i = 0; i < kParamN; ++i) {
        const QString k = QLatin1String(kParamKeys[i]);
        if (obj.contains(k)) p.insert(k, obj.value(k).toDouble());
    }
    plan.params = LumenAgent::clampPlan(p);
    plan.intent = obj.value(QStringLiteral("_intent")).toString().left(40);
    if (plan.intent.isEmpty()) plan.intent = QStringLiteral("本地 LLM 建议");
    plan.reason = QStringLiteral("本地 LLM（%1，%2ms）依据画面测量给出参数")
                      .arg(model).arg(timer.elapsed());
    plan.source = QStringLiteral("ollama");
    plan.model = model;
    plan.ok = true;
    return plan;
}

} // namespace

double LumenAgent::parseShutterSeconds(const QString& sh)
{
    const QString t = sh.trimmed();
    if (t.isEmpty()) return 0;
    bool okDen = false;
    if (t.startsWith(QLatin1Char('1')) && t.size() >= 3 && t[1] == QLatin1Char('/')) {
        const double den = t.mid(2).toDouble(&okDen);
        return (okDen && den > 0) ? 1.0 / den : 0;
    }
    if (t.endsWith(QLatin1Char('s'))) {
        const double v = t.left(t.size() - 1).toDouble(&okDen);
        return okDen ? v : 0;
    }
    const double v = t.toDouble(&okDen);
    return okDen ? v : 0;
}

AgentContext LumenAgent::contextFrom(const QImage& rgbIn, int iso, double shutter,
                                     const QString& goal)
{
    AgentContext c;
    c.iso = iso;
    c.shutter = shutter;
    c.goal = goal;
    if (rgbIn.isNull()) return c;
    QImage rgb = LumenEngine::boxDownscale(rgbIn, 256);
    const LumenStats st = LumenEngine::measureStats(rgb);
    const LumenMeasure mx = LumenEngine::measureExtra(rgb);
    c.lum = st.lum; c.lum_std = st.lum_std; c.sat = st.sat;
    c.hi = st.hi;   c.lo = st.lo;
    c.cast = mx.cast; c.haze = mx.haze; c.noise = mx.noise;
    return c;
}

QMap<QString, double> LumenAgent::clampPlan(const QMap<QString, double>& in)
{
    QMap<QString, double> out;
    for (int i = 0; i < kParamN; ++i) {
        const QString k = QLatin1String(kParamKeys[i]);
        if (in.contains(k)) out.insert(k, clamp1(in.value(k)));
    }
    return out;
}

AgentPlan LumenAgent::decideLocal(const AgentContext& c)
{
    // 忠实移植 brain.LocalBrain.decide（规则兜底，永远可用）
    QMap<QString, double> p;
    for (int i = 0; i < kParamN; ++i) p.insert(QLatin1String(kParamKeys[i]), 0.0);
    QStringList why;

    auto cl = [](double v, double lo2, double hi2) { return std::max(lo2, std::min(hi2, v)); };

    if (c.lum < 0.32) {
        const double d = cl((0.32 - c.lum) * 1.8, 0.0, 0.50);
        p["brightness"] = d;
        p["shadows"] = cl(0.10 + d * 0.4, 0.0, 0.35);
        p["blacks"] = cl(0.05 + d * 0.2, 0.0, 0.20);
        why << QStringLiteral("偏暗（亮度 %1）→ 提亮 +%2").arg(c.lum, 0, 'f', 2).arg(d, 0, 'f', 2);
    } else if (c.lum > 0.68) {
        const double d = cl((c.lum - 0.68) * 1.6, 0.0, 0.45);
        p["highlights"] = -d;
        p["whites"] = -cl(d * 0.6, 0.0, 0.30);
        why << QStringLiteral("偏亮（亮度 %1）→ 压高光 %2").arg(c.lum, 0, 'f', 2).arg(d, 0, 'f', 2);
    }
    if (c.lum_std < 0.13) {
        const double d = cl((0.19 - c.lum_std) * 2.2, 0.0, 0.45);
        p["contrast"] = d;
        p["clarity"] = cl(0.10 + d * 0.3, 0.0, 0.30);
        why << QStringLiteral("发灰（离散 %1）→ 对比 +%2").arg(c.lum_std, 0, 'f', 2).arg(d, 0, 'f', 2);
    } else if (c.lum_std > 0.30) {
        p["contrast"] = -cl((c.lum_std - 0.30) * 1.2, 0.0, 0.20);
        why << QStringLiteral("反差过强 → 略收对比");
    }
    if (c.sat < 0.18) {
        const double d = cl((0.24 - c.sat) * 1.8, 0.0, 0.40);
        p["saturation"] = d;
        p["vibrance"] = cl(0.12 + d * 0.3, 0.0, 0.30);
        why << QStringLiteral("色彩偏淡（%1）→ 饱和 +%2").arg(c.sat, 0, 'f', 2).arg(d, 0, 'f', 2);
    } else if (c.sat > 0.45) {
        p["saturation"] = -cl((c.sat - 0.45) * 1.0, 0.0, 0.25);
        why << QStringLiteral("色彩过浓 → 略降饱和");
    }
    // 色偏校正（本地增益：cast 正=偏暖 → 负 warmth 拉回）
    if (c.cast > 0.012) {
        p["warmth"] = -cl((c.cast - 0.012) * 6.0, 0.0, 0.30);
        why << QStringLiteral("偏暖（色偏 %1）→ 色温回调 %2").arg(c.cast, 0, 'f', 3).arg(p["warmth"], 0, 'f', 2);
    } else if (c.cast < -0.012) {
        p["warmth"] = cl((-c.cast - 0.012) * 6.0, 0.0, 0.30);
        why << QStringLiteral("偏冷（色偏 %1）→ 色温回调 +%2").arg(c.cast, 0, 'f', 3).arg(p["warmth"], 0, 'f', 2);
    }
    // 噪点（EXIF 感知；无 EXIF 时由 Lumen noise 估计补位）
    const int iso = c.iso;
    if (iso >= 6400)      { p["denoise"] = 0.50; why << QStringLiteral("ISO %1 极高感 → 强降噪").arg(iso); }
    else if (iso >= 3200) { p["denoise"] = 0.30; why << QStringLiteral("ISO %1 → 中降噪").arg(iso); }
    else if (iso >= 1600) { p["denoise"] = 0.12; why << QStringLiteral("ISO %1 → 轻降噪").arg(iso); }
    else if (iso <= 0 && c.noise >= 0.070) { p["denoise"] = 0.30; why << QStringLiteral("画面噪声偏高（%1）→ 中降噪").arg(c.noise, 0, 'f', 3); }
    else if (iso <= 0 && c.noise >= 0.045) { p["denoise"] = 0.12; why << QStringLiteral("画面有噪声（%1）→ 轻降噪").arg(c.noise, 0, 'f', 3); }
    if (c.shutter >= 4.0) {
        p["denoise"] = clamp1(p["denoise"] + 0.20);
        why << QStringLiteral("长曝 %1s → 热噪降噪").arg(c.shutter, 0, 'f', 0);
    }
    p["sharpness"] = 0.15;   // 出图锐化（轻微，恒定）

    QString intent = QStringLiteral("风光通透");
    const QString goal = c.goal;
    const QString gl = goal.toLower();
    if (goal.contains(QStringLiteral("胶片")) || gl.contains("film") || goal.contains(QStringLiteral("复古"))) {
        intent = QStringLiteral("胶片风格");
        p["warmth"] = clamp1(p["warmth"] + 0.12);
        p["contrast"] = clamp1(p["contrast"] + 0.06);
        p["saturation"] = clamp1(p["saturation"] - 0.05);
        why << QStringLiteral("目标「胶片感」→ 暖调 + 柔对比");
    } else if (goal.contains(QStringLiteral("黑白")) || gl.contains("mono") || goal.contains(QStringLiteral("去色"))) {
        intent = QStringLiteral("黑白");
        p["saturation"] = -1.0;
        p["clarity"] = clamp1(p["clarity"] + 0.15);
        why << QStringLiteral("目标「黑白」→ 去饱和 + 提清晰度");
    } else if (goal.contains(QStringLiteral("人像")) || goal.contains(QStringLiteral("磨皮")) || goal.contains(QStringLiteral("皮肤"))) {
        intent = QStringLiteral("人像美化");
        p["clarity"] = clamp1(p["clarity"] - 0.08);
        why << QStringLiteral("目标「人像」→ 降清晰度（皮肤柔和）");
    } else if (gl.contains(QStringLiteral("清透")) || gl.contains(QStringLiteral("通透")) || gl.contains(QStringLiteral("去灰"))) {
        p["contrast"] = clamp1(p["contrast"] + 0.10);
        p["clarity"] = clamp1(p["clarity"] + 0.12);
        why << QStringLiteral("目标「通透」→ 对比 + 清晰度");
    }

    AgentPlan plan;
    plan.params = p;
    plan.intent = intent;
    plan.reason = why.isEmpty() ? QStringLiteral("各项指标正常，仅做出图锐化")
                                : why.join(QStringLiteral("；"));
    plan.source = QStringLiteral("local");
    plan.ok = true;
    return plan;
}

AgentPlan LumenAgent::decide(const AgentContext& ctx)
{
    const QUrl base(llmUrl());
    if (isLocalUrl(llmUrl()) && portOpen(base)) {
        AgentPlan p = decideOllama(ctx, base, llmModel());
        if (p.ok && !p.params.isEmpty()) return p;
        // Ollama 在线但输出不可用 → 落兜底（reason 保留失败原因）
        AgentPlan fb = decideLocal(ctx);
        fb.reason = QStringLiteral("[LLM 兜底] %1 → %2").arg(p.reason, fb.reason);
        return fb;
    }
    AgentPlan fb = decideLocal(ctx);
    fb.reason = QStringLiteral("[Ollama 未连接] %1").arg(fb.reason);
    return fb;
}
