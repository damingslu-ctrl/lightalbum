#include "CmdKPanel.h"

#include <QCoreApplication>
#include <QDate>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// 连接词：人物捕捉时向左吞掉（只记入消费掩码，不进 chip 文案、不进兜底 text）。
bool isConnector(QChar c)
{
    switch (c.unicode()) {
    case 0x548C:  // 和
    case 0x4E0E:  // 与
    case 0x8DDF:  // 跟
    case 0x3001:  // 、
    case 0x53CA:  // 及
        return true;
    default:
        return false;
    }
}

struct Span { int start = 0; int len = 0; };

struct Parsed {
    CmdFilter f;
    QStringList chips;
};

// 月份中文词表：三字（十一/十二）优先，随后 一月..十月（长优先以免「十一月」被「一月」截胡）。
const QStringList& monthTokens()
{
    static const QStringList t = {
        QString::fromUtf8("十一月"), QString::fromUtf8("十二月"),
        QString::fromUtf8("一月"), QString::fromUtf8("二月"), QString::fromUtf8("三月"),
        QString::fromUtf8("四月"), QString::fromUtf8("五月"), QString::fromUtf8("六月"),
        QString::fromUtf8("七月"), QString::fromUtf8("八月"), QString::fromUtf8("九月"),
        QString::fromUtf8("十月"),
    };
    return t;
}
int monthOfToken(int idx) { return idx == 0 ? 11 : idx == 1 ? 12 : idx - 1; }

const QStringList& seasonChars()
{
    static const QStringList t = {
        QString::fromUtf8("春"), QString::fromUtf8("夏"),
        QString::fromUtf8("秋"), QString::fromUtf8("冬"),
    };
    return t;
}

// 风格别名表（长字面优先）；值为 Classifier::normalizeLabel 的规范字面（8 类融合）。
const QVector<QPair<QString, QString>>& catAliases()
{
    static const QVector<QPair<QString, QString>> t = {
        { QString::fromUtf8("夜景·星空"), QString::fromUtf8("夜景·星空") },
        { QString::fromUtf8("城市·建筑"), QString::fromUtf8("城市·建筑") },
        { QString::fromUtf8("微距·花卉"), QString::fromUtf8("微距·花卉") },
        { QString::fromUtf8("夜景"), QString::fromUtf8("夜景·星空") },
        { QString::fromUtf8("星空"), QString::fromUtf8("夜景·星空") },
        { QString::fromUtf8("城市"), QString::fromUtf8("城市·建筑") },
        { QString::fromUtf8("建筑"), QString::fromUtf8("城市·建筑") },
        { QString::fromUtf8("微距"), QString::fromUtf8("微距·花卉") },
        { QString::fromUtf8("花卉"), QString::fromUtf8("微距·花卉") },
        { QString::fromUtf8("人像"), QString::fromUtf8("人像") },
        { QString::fromUtf8("风光"), QString::fromUtf8("风光") },
        { QString::fromUtf8("动物"), QString::fromUtf8("动物") },
        { QString::fromUtf8("美食"), QString::fromUtf8("美食") },
        { QString::fromUtf8("黑白"), QString::fromUtf8("黑白") },
    };
    return t;
}

QString chipCss(const QColor& accent)
{
    // chip 族本地单源（token 化；圆角走 Radius::Pill 插值，不落字面量）。
    return QStringLiteral(
        "QPushButton{background:%1;border:1px solid %2;border-radius:%3;"
        "padding:3px 12px;color:%4;}"
        "QPushButton:hover{border-color:%5;}")
        .arg(Lumen::hex(Lumen::inset()), Lumen::hex(Lumen::line()),
             Lumen::Radius::Pill, Lumen::hex(accent), Lumen::hex(Lumen::gold()));
}

Parsed parseImpl(const QString& sentence, const QStringList& personDict,
                 const QStringList& placeDict)
{
    // 反例臂：month↔season 表互换（LA_CMDK_QA_FORCE_BROKEN=1）。
    const bool broken = !qEnvironmentVariableIsEmpty("LA_CMDK_QA_FORCE_BROKEN");
    Parsed out;
    const QString s = sentence;
    const int n = s.size();
    if (n == 0) return out;

    QVector<bool> used(n, false);
    QVector<Span> spans;

    auto freeRange = [&](int start, int len) {
        for (int i = start; i < start + len && i < n; ++i)
            if (used[i]) return false;
        return true;
    };
    auto mark = [&](int start, int len) {
        for (int i = start; i < start + len && i < n; ++i) used[i] = true;
        spans << Span{start, len};
    };
    auto findTok = [&](const QString& tok) -> int {
        if (tok.isEmpty()) return -1;
        int i = s.indexOf(tok, 0);
        while (i >= 0) {
            if (freeRange(i, tok.size())) return i;
            i = s.indexOf(tok, i + 1);
        }
        return -1;
    };
    // 表内捕捉：列表序（长优先）优先，同词取句中最先位置。
    auto findInTable = [&](const QStringList& toks, int& idxOut) -> int {
        for (int k = 0; k < toks.size(); ++k) {
            const int i = findTok(toks[k]);
            if (i >= 0) { idxOut = k; return i; }
        }
        idxOut = -1;
        return -1;
    };

    const QDate today = QDate::currentDate();

    // ① 时间·年份词
    {
        static const QString kQian = QString::fromUtf8("前年");
        static const QString kQu   = QString::fromUtf8("去年");
        static const QString kJin  = QString::fromUtf8("今年");
        int i = findTok(kQian);
        if (i >= 0) { out.f.yearMin = out.f.yearMax = today.year() - 2; mark(i, kQian.size()); }
        else if ((i = findTok(kQu)) >= 0) { out.f.yearMin = out.f.yearMax = today.year() - 1; mark(i, kQu.size()); }
        else if ((i = findTok(kJin)) >= 0) { out.f.yearMin = out.f.yearMax = today.year(); mark(i, kJin.size()); }
    }

    // ①b 时间·四位年份 XXXX年（1970..2100）
    if (out.f.yearMin == 0) {
        static const QRegularExpression re(QStringLiteral("(\\d{4})年"));
        auto m = re.match(s);
        while (m.hasMatch()) {
            const int i = m.capturedStart();
            const int len = m.capturedLength();
            if (freeRange(i, len)) {
                const int y = m.captured(1).toInt();
                if (y >= 1970 && y <= 2100) { out.f.yearMin = out.f.yearMax = y; mark(i, len); }
                break;
            }
            m = re.match(s, i + 1);
        }
    }

    // ② 季节（春/夏/秋/冬，可带 天/季 后缀）
    {
        if (!broken) {
            const QStringList& sea = seasonChars();
            for (int i = 0; i < n; ++i) {
                if (used[i]) continue;
                const int k = sea.indexOf(QString(s[i]));
                if (k < 0) continue;
                int len = 1;
                if (i + 1 < n && !used[i + 1]
                    && (s[i + 1] == QChar(0x5929) || s[i + 1] == QChar(0x5B63)))
                    len = 2;
                out.f.season = sea[k];
                mark(i, len);
                break;
            }
        } else {
            int idx = -1;
            const int i = findInTable(monthTokens(), idx);
            if (i >= 0) { out.f.season = monthTokens()[idx]; mark(i, monthTokens()[idx].size()); }
        }
    }

    // ③ 月份（中文月表；阿拉伯 N月）
    {
        if (!broken) {
            int idx = -1;
            const int i = findInTable(monthTokens(), idx);
            if (i >= 0) {
                out.f.month = monthOfToken(idx); mark(i, monthTokens()[idx].size());
            } else {
                static const QRegularExpression re(QStringLiteral("(\\d{1,2})月"));
                auto m = re.match(s);
                while (m.hasMatch()) {
                    const int p = m.capturedStart();
                    const int len = m.capturedLength();
                    if (freeRange(p, len)) {
                        const int mm = m.captured(1).toInt();
                        if (mm >= 1 && mm <= 12) { out.f.month = mm; mark(p, len); }
                        break;
                    }
                    m = re.match(s, p + 1);
                }
            }
        } else {
            int idx = -1;
            const QStringList& sea = seasonChars();
            const int i = findInTable(sea, idx);
            if (i >= 0) { out.f.month = idx + 1; mark(i, sea[idx].size()); }
        }
    }

    // ④ 日（N日 / N号）
    {
        static const QRegularExpression re(QStringLiteral("(\\d{1,2})(日|号)"));
        auto m = re.match(s);
        while (m.hasMatch()) {
            const int i = m.capturedStart();
            const int len = m.capturedLength();
            if (freeRange(i, len)) {
                const int dd = m.captured(1).toInt();
                if (dd >= 1 && dd <= 31) { out.f.day = dd; mark(i, len); }
                break;
            }
            m = re.match(s, i + 1);
        }
    }

    // ⑤ 风格（8 类别名，长优先）
    {
        for (const auto& a : catAliases()) {
            const int i = findTok(a.first);
            if (i < 0) continue;
            out.f.category = a.second;
            mark(i, a.first.size());
            break;
        }
    }

    // ⑥ 人物（personDict 首个命中；向左吞连接词但不计入 chip 文案）
    {
        for (const QString& raw : personDict) {
            const QString nm = raw.trimmed();
            if (nm.isEmpty()) continue;
            const int i = findTok(nm);
            if (i < 0) continue;
            if (i > 0 && !used[i - 1] && isConnector(s[i - 1]))
                used[i - 1] = true;
            mark(i, nm.size());
            out.f.person = nm;
            break;
        }
    }

    // ⑦ 地点（placeDict）
    {
        for (const QString& raw : placeDict) {
            const QString nm = raw.trimmed();
            if (nm.isEmpty()) continue;
            const int i = findTok(nm);
            if (i < 0) continue;
            mark(i, nm.size());
            out.f.tokenPlace = nm;
            break;
        }
    }

    // 兜底 text = 未消费字符（去首尾连接词后 trim）
    {
        QString rest;
        rest.reserve(n);
        for (int i = 0; i < n; ++i) if (!used[i]) rest += s[i];
        rest = rest.trimmed();
        while (!rest.isEmpty() && isConnector(rest.front())) rest.remove(0, 1);
        while (!rest.isEmpty() && isConnector(rest.back())) rest.chop(1);
        out.f.text = rest.trimmed();
    }

    // chips = 捕获原文（相邻 span 合并）+ 兜底 chip（text 非空且 tokenPlace 空）
    {
        std::stable_sort(spans.begin(), spans.end(),
                         [](const Span& a, const Span& b) { return a.start < b.start; });
        int curStart = -1, curEnd = -1;
        auto flush = [&]() {
            if (curStart >= 0) out.chips << s.mid(curStart, curEnd - curStart);
            curStart = curEnd = -1;
        };
        for (const Span& sp : spans) {
            if (curStart >= 0 && sp.start == curEnd) { curEnd += sp.len; continue; }
            flush();
            curStart = sp.start; curEnd = sp.start + sp.len;
        }
        flush();
        if (!out.f.text.isEmpty() && out.f.tokenPlace.isEmpty())
            out.chips << out.f.text;
    }

    return out;
}

} // namespace

bool CmdFilter::empty() const
{
    return yearMin == 0 && yearMax == 0 && month == 0 && day == 0
        && season.isEmpty() && person.isEmpty() && tokenPlace.isEmpty()
        && category.isEmpty() && text.isEmpty();
}

CmdFilter CmdKPanel::parse(const QString& sentence, const QStringList& personDict,
                           const QStringList& placeDict)
{
    return parseImpl(sentence, personDict, placeDict).f;
}

QStringList CmdKPanel::parseChips(const QString& sentence, const QStringList& personDict,
                                  const QStringList& placeDict)
{
    return parseImpl(sentence, personDict, placeDict).chips;
}

CmdKPanel::CmdKPanel(const QVector<Photo>* photos, QWidget* parent)
    : QWidget(parent), photos_(photos)
{
    setObjectName(QStringLiteral("cmdkPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(Lumen::Space::Xl, Lumen::Space::L,
                             Lumen::Space::Xl, Lumen::Space::L);
    root->setSpacing(Lumen::Space::M);

    // --mat 装裱单元（card() 面 + E2 投影）；E4 开馆层 = 金描边（仅本面板与前厅厅心卡）。
    auto* mat = new QFrame(this);
    mat->setObjectName(QStringLiteral("cmdkMat"));
    mat->setStyleSheet(QStringLiteral(
        "QFrame#cmdkMat{background:%1;border:1px solid %2;border-radius:%3;}")
        .arg(Lumen::hex(Lumen::card()), Lumen::hex(Lumen::gold()))
        .arg(Lumen::Radius::Feature));
    Lumen::Elev::apply(mat, Lumen::Elev::E2);
    root->addWidget(mat);

    auto* mv = new QVBoxLayout(mat);
    mv->setContentsMargins(Lumen::Space::L, Lumen::Space::M,
                           Lumen::Space::L, Lumen::Space::M);
    mv->setSpacing(Lumen::Space::S);

    auto* title = new QLabel(
        QCoreApplication::translate("LightAlbum", "检索馆藏"), mat);
    title->setFont(Lumen::Type::label());
    title->setStyleSheet(
        QStringLiteral("color:%1;background:transparent;border:none;").arg(Lumen::hex(Lumen::ink())));
    mv->addWidget(title);

    input_ = new QLineEdit(mat);
    input_->setFont(Lumen::Type::body());
    input_->setStyleSheet(Lumen::input());
    input_->setPlaceholderText(QCoreApplication::translate(
        "LightAlbum", "试试：去年秋天和小雨在公园的黑白照片"));
    mv->addWidget(input_);

    hintRow_ = new QWidget(mat);
    {
        auto* h = new QHBoxLayout(hintRow_);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(Lumen::Space::S);
    }
    mv->addWidget(hintRow_);

    chipRow_ = new QWidget(mat);
    {
        auto* h = new QHBoxLayout(chipRow_);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(Lumen::Space::S);
    }
    mv->addWidget(chipRow_);

    auto* actions = new QWidget(mat);
    auto* ah = new QHBoxLayout(actions);
    ah->setContentsMargins(0, 0, 0, 0);
    ah->setSpacing(Lumen::Space::S);
    auto* run = new QPushButton(QCoreApplication::translate("LightAlbum", "执行"), actions);
    run->setFont(Lumen::Type::body());
    run->setStyleSheet(Lumen::btnGold());
    run->setCursor(Qt::PointingHandCursor);
    ah->addWidget(run);
    auto* stash = new QPushButton(QCoreApplication::translate("LightAlbum", "暂存为合集"), actions);
    stash->setFont(Lumen::Type::body());
    stash->setStyleSheet(Lumen::btnGhost());
    stash->setEnabled(false);
    stash->setToolTip(QCoreApplication::translate("LightAlbum", "下轮上线"));
    ah->addWidget(stash);
    ah->addStretch(1);
    auto* key = new QLabel(QStringLiteral("Ctrl+K"), actions);
    key->setFont(Lumen::Type::monoMeta());
    key->setStyleSheet(QStringLiteral("color:%1;background:transparent;border:none;")
                           .arg(Lumen::hex(Lumen::faint())));
    ah->addWidget(key);
    mv->addWidget(actions);

    connect(input_, &QLineEdit::textChanged, this, [this](const QString&) { refreshChips(); });
    connect(input_, &QLineEdit::returnPressed, this, &CmdKPanel::onSubmit);
    connect(run, &QPushButton::clicked, this, &CmdKPanel::onSubmit);

    rebuildHints();
    refreshChips();
}

void CmdKPanel::openFor(Lumen::Hall s)
{
    space_ = s;
    rebuildHints();
}

void CmdKPanel::setHidePrivate(bool hide)
{
    hidePrivate_ = hide;
    rebuildHints();
    refreshChips();
}

QStringList CmdKPanel::personDict() const
{
    QStringList out;
    QSet<QString> seen;
    if (!photos_) return out;
    for (const Photo& p : *photos_) {
        if (p.missing || p.thumbFile.isEmpty()) continue;
        if (p.priv && hidePrivate_) continue;      // 锁定态词典不含私密照人物名（spec §3 红线）
        const QStringList names = p.person.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString& raw : names) {
            const QString n = raw.trimmed();
            if (n.isEmpty() || seen.contains(n)) continue;
            seen.insert(n);
            out << n;
        }
    }
    return out;
}

QStringList CmdKPanel::placeDict() const
{
    QStringList out;
    QSet<QString> seen;
    if (!photos_) return out;
    for (const Photo& p : *photos_) {
        const QString raw[3] = { p.region, p.prov, p.city };
        for (const QString& r : raw) {
            const QString n = r.trimmed();
            if (n.isEmpty() || seen.contains(n)) continue;
            seen.insert(n);
            out << n;
        }
    }
    return out;
}

QStringList CmdKPanel::candidatesFor(const QString& chip, const CmdFilter& f) const
{
    if (!f.person.isEmpty() && chip == f.person) return personDict();
    if (!f.tokenPlace.isEmpty() && chip == f.tokenPlace) return placeDict();
    if (!f.category.isEmpty() && chip == f.category) {
        QStringList canon;
        for (const auto& a : catAliases())
            if (!canon.contains(a.second)) canon << a.second;
        return canon;
    }
    return {};
}

void CmdKPanel::cycleChip(const QString& chip)
{
    if (!input_) return;
    const CmdFilter f = parse(input_->text().trimmed(), personDict(), placeDict());
    const QStringList cands = candidatesFor(chip, f);
    if (cands.isEmpty()) return;
    const int next = (cycleIdx_.value(chip, -1) + 1) % cands.size();
    cycleIdx_.insert(chip, next);
    chipOverride_.insert(chip, cands[next]);
    refreshChips();
}

void CmdKPanel::refreshChips()
{
    if (!chipRow_) return;
    if (auto* l = chipRow_->layout()) {
        QLayoutItem* it = nullptr;
        while ((it = l->takeAt(0)) != nullptr) {
            if (QWidget* w = it->widget()) w->deleteLater();
            delete it;
        }
    }
    auto* h = qobject_cast<QHBoxLayout*>(chipRow_->layout());
    if (!h || !input_) return;

    const QString text = input_->text().trimmed();
    if (!text.isEmpty()) {
        const CmdFilter f = parse(text, personDict(), placeDict());
        for (const QString& chip : parseChips(text, personDict(), placeDict())) {
            if (removedChips_.contains(chip)) continue;
            auto* b = new QPushButton(chipOverride_.value(chip, chip), chipRow_);
            b->setFont(Lumen::Type::smallText());
            b->setStyleSheet(chipCss(Lumen::emerald()));
            b->setCursor(Qt::PointingHandCursor);
            const bool hasCands = !candidatesFor(chip, f).isEmpty();
            b->setToolTip(hasCands
                ? QCoreApplication::translate("LightAlbum", "点击切换候选，右键移除")
                : QCoreApplication::translate("LightAlbum", "右键移除此条"));
            connect(b, &QPushButton::clicked, this, [this, chip]() { cycleChip(chip); });
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QWidget::customContextMenuRequested, this, [this, chip](const QPoint&) {
                removedChips_ << chip;
                refreshChips();
            });
            h->addWidget(b);
        }
    }
    h->addStretch(1);
}

void CmdKPanel::onSubmit()
{
    if (!input_) return;
    const QString text = input_->text().trimmed();
    if (text.isEmpty()) return;

    if (text == QString::fromUtf8("今天")) {
        emit lookBackToday(QDate::currentDate());
        return;
    }

    const QStringList pd = personDict();
    const QStringList pl = placeDict();
    CmdFilter f = parse(text, pd, pl);

    // 纯 UI 态校正：chip 循环候选 / 右键移除
    for (auto it = chipOverride_.constBegin(); it != chipOverride_.constEnd(); ++it) {
        if (it.key() == f.person) f.person = it.value();
        else if (it.key() == f.tokenPlace) f.tokenPlace = it.value();
        else if (it.key() == f.category) f.category = it.value();
        else if (it.key() == f.text) f.text = it.value();
    }
    for (const QString& c : removedChips_) {
        if (c == f.person) f.person.clear();
        else if (c == f.tokenPlace) f.tokenPlace.clear();
        else if (c == f.category) f.category.clear();
        else if (c == f.text) f.text.clear();
    }
    if (f.empty()) return;

    // 路由（互斥）：纯人物 → 开房；纯风格 → 直达凝视；其余 → 应用筛选。
    const bool personOnly = !f.person.isEmpty() && f.category.isEmpty()
        && f.text.isEmpty() && f.tokenPlace.isEmpty() && f.season.isEmpty()
        && f.yearMin == 0 && f.yearMax == 0 && f.month == 0 && f.day == 0;
    const bool styleOnly = f.person.isEmpty() && !f.category.isEmpty()
        && f.text.isEmpty() && f.tokenPlace.isEmpty() && f.season.isEmpty()
        && f.yearMin == 0 && f.yearMax == 0 && f.month == 0 && f.day == 0;
    if (personOnly) emit openRoom(f.person);
    else if (styleOnly) emit openGazeFiltered(f);
    else emit applyFilter(f);

    refreshChips();
}

void CmdKPanel::rebuildHints()
{
    if (!hintRow_) return;
    if (auto* l = hintRow_->layout()) {
        QLayoutItem* it = nullptr;
        while ((it = l->takeAt(0)) != nullptr) {
            if (QWidget* w = it->widget()) w->deleteLater();
            delete it;
        }
    }
    auto* h = qobject_cast<QHBoxLayout*>(hintRow_->layout());
    if (!h) return;

    const QString timeWords = QCoreApplication::translate(
        "LightAlbum", "去年 · 今年 · 春天 · 六月");
    const QStringList pd = personDict();
    const QString people = pd.isEmpty()
        ? QCoreApplication::translate("LightAlbum", "尚未识别")
        : pd.mid(0, 3).join(QString::fromUtf8(" · "));
    const QStringList pl = placeDict();
    const QString places = pl.isEmpty()
        ? QCoreApplication::translate("LightAlbum", "无地点元数据")
        : pl.mid(0, 3).join(QString::fromUtf8(" · "));
    const QString styles = QCoreApplication::translate(
        "LightAlbum", "人像 · 风光 · 夜景·星空 · 黑白");

    const struct { QString prefix; QString sample; QColor tone; } groups[4] = {
        { QCoreApplication::translate("LightAlbum", "时间"), timeWords, Lumen::emerald() },
        { QCoreApplication::translate("LightAlbum", "人物"), people,    Lumen::CAT1 },
        { QCoreApplication::translate("LightAlbum", "地点"), places,    Lumen::CAT2 },
        { QCoreApplication::translate("LightAlbum", "风格"), styles,    Lumen::CAT3 },
    };
    for (const auto& g : groups) {
        auto* cap = new QLabel(QStringLiteral("%1  %2").arg(g.prefix, g.sample), hintRow_);
        cap->setFont(Lumen::Type::smallText());
        cap->setStyleSheet(Lumen::pill(g.tone));
        h->addWidget(cap);
    }
    h->addStretch(1);
}
