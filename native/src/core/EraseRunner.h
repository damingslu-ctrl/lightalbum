// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 LightAlbum Contributors
#pragma once
#include <QObject>
#include <QProcess>
#include <QImage>
#include <QString>

class EraseRunner : public QObject {
    Q_OBJECT
public:
    explicit EraseRunner(QObject* parent = nullptr);
    ~EraseRunner() override;

    bool ready();                                        // spawn + await READY
    bool run(quint32 id, const QImage& base, const QImage& mask, QImage& out, QString* err);
    bool processId(qint64& pid) const;
    void qaKillChild();
    static void qaInjectBadSize(bool on);                // QA 反例钩子（Task 5/9 用）
    QString exePath() const { return exePath_; }

private:
    bool ensureStarted();
    QProcess* proc_ = nullptr;
    QString exePath_;
    bool ready_ = false;
};
