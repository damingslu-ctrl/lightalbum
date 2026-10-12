// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#include "DedupDialog.h"
#include "../core/PhotoDedup.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QPixmap>
#include <QDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <memory>

static void parseDim(const QString& dim, int& w, int& h)
{
    w = h = 0;
    const int x = dim.indexOf(QLatin1Char('x'));
    if (x < 0) return;
    w = dim.left(x).toInt();
    h = dim.mid(x + 1).toInt();
}

static QPixmap loadThumb(const QString& thumbsDir, const QString& thumbFile, int sz)
{
    QPixmap pm(QDir(thumbsDir).filePath(thumbFile));
    if (pm.isNull()) return QPixmap();
    return pm.scaled(sz, sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

// QA 反例钩子：令注入点隐私过滤失效（私密照照常入分析集）→ 门禁隐私断言变红。
static bool g_forcePrivacyLeak = false;
void DedupDialog::setForcePrivacyLeakForQa(bool on) { g_forcePrivacyLeak = on; }

DedupDialog::DedupDialog(const QVector<Photo>* photos, const QString& thumbsDir,
                         bool hidePrivate, QWidget* parent)
    : QDialog(parent), photos_(photos), thumbsDir_(thumbsDir), hidePrivate_(hidePrivate)
{
    setWindowTitle(QCoreApplication::translate("LightAlbum", "相似照片"));
    resize(760, 580);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    summary_ = new QLabel(this);
    summary_->setStyleSheet(QString::fromUtf8("color:%1;%2")
        .arg(Lumen::hex(Lumen::MUTED))
        .arg(Lumen::Type::css(Lumen::Type::Tier::Body)));   // T8：13px=T4 原位
    root->addWidget(summary_);

    auto* sa = new QScrollArea(this);
    sa->setWidgetResizable(true);
    // §1.7：滚动区盘底（原值 060806） 即 BG token 值字面量→归引用；私有描边 1c2620→LINE（回家，微亮授权内）
    sa->setStyleSheet(QString::fromUtf8(
        "background:%1;border:1px solid %2;border-radius:10px;")
        .arg(Lumen::hex(Lumen::BG), Lumen::hex(Lumen::LINE)));
    auto* list = new QWidget(sa);
    listLay_ = new QVBoxLayout(list);
    listLay_->setContentsMargins(12, 12, 12, 12);
    listLay_->setSpacing(12);
    sa->setWidget(list);
    root->addWidget(sa, 1);

    // 注入点过滤（铁律6：过滤只做一次、做在数据进来之处）：私密照片在锁定态下
    // 根本不进 inputs_，也不进 thumbOf_ → 后续分组/渲染/标记全链路无从泄漏。
    if (photos_) {
        inputs_.reserve(photos_->size());
        for (const Photo& p : *photos_) {
            if (hidePrivate_ && p.priv && !g_forcePrivacyLeak) continue;
            DedupPhoto d;
            d.path      = p.path;
            d.thumbFile = p.thumbFile;
            parseDim(p.dim, d.origW, d.origH);
            inputs_.append(d);
            thumbOf_.insert(p.path, p.thumbFile);
        }
    }
    if (inputs_.isEmpty()) {
        summary_->setText(QCoreApplication::translate("LightAlbum", "未找到相似照片"));
        analysisDone_ = true;      // 无输入不起线程，直接算落地（QA 不必等待）
        return;
    }
    startScan();
}

QStringList DedupDialog::qaInputPaths() const
{
    QStringList out;
    out.reserve(inputs_.size());
    for (const DedupPhoto& d : inputs_) out.append(d.path);
    return out;
}

bool DedupDialog::qaWaitFinished(int timeoutMs) const
{
    QElapsedTimer et;
    et.start();
    while (!analysisDone_ && et.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return analysisDone_;
}

// 后台分析：重活（逐张解码缩略图 + 算 MD5/pHash）交给 worker 线程，
// 分组与文案留在 GUI 线程的 finished 回调里做。
// 线程边界：worker 只读 inputs_ 的拷贝、只写两个原子量，任何 QWidget 指针都不进 worker。
void DedupDialog::startScan()
{
    const int total = inputs_.size();
    summary_->setText(QCoreApplication::translate(
        "LightAlbum", "正在查找相似照片（%1 张）…").arg(total));

    auto* prog = new QProgressDialog(
        QCoreApplication::translate("LightAlbum", "正在查找相似照片…"),
        QCoreApplication::translate("LightAlbum", "取消"), 0, total, this);
    prog->setWindowModality(Qt::WindowModal);
    prog->setMinimumDuration(0);
    // 关掉自删：由本对话框统一收尾（prog 是 this 的子对象，this 存活期即其存活期）
    prog->setAutoClose(false);

    auto done = std::make_shared<std::atomic<int>>(0);
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    connect(prog, &QProgressDialog::canceled, this,
            [cancelled] { cancelled->store(true); });

    auto* poll = new QTimer(this);
    poll->setInterval(60);
    connect(poll, &QTimer::timeout, prog, [done, prog] { prog->setValue(done->load()); });
    poll->start();

    auto* watcher = new QFutureWatcher<QVector<DedupDesc>>(this);
    connect(watcher, &QFutureWatcher<QVector<DedupDesc>>::finished, this,
            [this, prog, poll, watcher, done, cancelled, total] {
                poll->stop();
                watcher->deleteLater();
                prog->deleteLater();
                // 取消 → 结果是残缺集，残缺集分组会造假组，整份丢弃（数据诚实铁律）
                if (cancelled->load() || done->load() < total) {
                    summary_->setText(
                        QCoreApplication::translate("LightAlbum", "已取消相似照片分析"));
                } else {
                    groups_ = PhotoDedup::group(watcher->result());
                    int dupTotal = 0;
                    for (const DupGroup& g : groups_) {
                        dupTotal += g.dupPaths.size();
                        buildGroup(g, listLay_);
                    }
                    summary_->setText(groups_.isEmpty()
                        ? QCoreApplication::translate("LightAlbum", "未找到相似照片")
                        : QCoreApplication::translate(
                              "LightAlbum", "发现 %1 组相似照片，共 %2 张可清理")
                              .arg(groups_.size()).arg(dupTotal));
                }
                analysisDone_ = true;
            });

    const QVector<DedupPhoto> inputs = inputs_;    // 拷进 worker，GUI 线程不再触碰
    const QString thumbsDir = thumbsDir_;
    watcher->setFuture(QtConcurrent::run([inputs, thumbsDir, done, cancelled] {
        return PhotoDedup::describe(
            inputs, thumbsDir, true,
            [done](int d) { done->store(d); },
            [cancelled] { return cancelled->load(); });
    }));
}

void DedupDialog::buildGroup(const DupGroup& g, QVBoxLayout* parent)
{
    auto* w = new QWidget(this);
    // §1.7：组卡内嵌底（原值 0e1410） 即 INSET token 值字面量→归引用；描边 1c2620→LINE（回家）
    w->setStyleSheet(QString::fromUtf8(
        "background:%1;border:1px solid %2;border-radius:10px;")
        .arg(Lumen::hex(Lumen::INSET), Lumen::hex(Lumen::LINE)));
    auto* lay = new QHBoxLayout(w);
    lay->setContentsMargins(Lumen::Space::M, Lumen::Space::M, Lumen::Space::M, Lumen::Space::M);
    lay->setSpacing(12);

    // 保留者缩略图
    auto* kept = new QLabel(w);
    kept->setPixmap(loadThumb(thumbsDir_, thumbOf_.value(g.keptPath), 96));
    lay->addWidget(kept);

    // 右侧：原因 + 重复缩略图行 + 动作
    auto* col = new QVBoxLayout;
    col->setSpacing(Lumen::Space::S);
    auto* reason = new QLabel(g.reason, w);
    reason->setStyleSheet(QStringLiteral("color:%1;%2")
        .arg(Lumen::hex(Lumen::INK))   // §1.7：抄写盘文字（原值 d9e4dc） → INK（归并，色微亮）
        .arg(Lumen::Type::css(Lumen::Type::Tier::Small)));   // T8：12px=T5 原位
    col->addWidget(reason);

    auto* dups = new QWidget(w);
    auto* dlay = new QHBoxLayout(dups);
    dlay->setContentsMargins(0, 0, 0, 0);
    dlay->setSpacing(4);
    for (const QString& dp : g.dupPaths) {
        auto* lab = new QLabel(dups);
        lab->setPixmap(loadThumb(thumbsDir_, thumbOf_.value(dp), 48));
        dlay->addWidget(lab);
    }
    col->addWidget(dups);

    auto* markBtn = new QPushButton(
        QCoreApplication::translate("LightAlbum", "标记其余为待删"), w);
    markBtn->setCursor(Qt::PointingHandCursor);
    // CONTROLLER RULING（本任务一并做，一页一过）：「标记其余为待删」= 破坏性动作，
    // 幽灵克隆（MUTED 静息 + 翡翠 hover，语义错位）→ §4.1/§4.2 指定 btnOutline(CORAL)。
    // 归厂偏差记录：padding 3px10px→6px14px、原 12px 字号回落应用字阶、hover/disabled 制式化。
    markBtn->setStyleSheet(Lumen::btnOutline(Lumen::CORAL));
    const QStringList dupsCopy = g.dupPaths;
    connect(markBtn, &QPushButton::clicked, this, [this, dupsCopy, w]() {
        onMarkGroup(dupsCopy, w);
    });
    col->addWidget(markBtn);
    lay->addLayout(col, 1);

    parent->addWidget(w);
}

void DedupDialog::onMarkGroup(const QStringList& dupPaths, QWidget* w)
{
    emit markRequested(dupPaths);
    w->deleteLater();   // 标记后从列表移除该组（已处理）
}
