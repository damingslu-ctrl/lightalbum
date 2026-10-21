#pragma once
// 波4 R1：典藏（Hall::Collection）—— 合集 / 展览史 / 安静下来的选题。
// 只读 Library；唤醒意图以信号上交 MainWindow（本类不写库，GUI 单写者不变）。
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QVector>
#include "../core/Library.h"

class QListWidget;
class QTabWidget;
class ThumbCache;

class CollectionPage : public QWidget {
    Q_OBJECT
public:
    CollectionPage(Library* lib, const QString& thumbsDir, QWidget* parent = nullptr);
    void setHidePrivate(bool hide);
    void refresh();

    // QA 探针：只回可数的结构事实，不做视觉判断。
    int qaTabCount() const;
    int qaAlbumRowCount() const;
    int qaExhibitRowCount() const;
    int qaMuteRowCount() const;

signals:
    void openAlbum(const QStringList& paths);
    void openExhibit(const QStringList& paths, const QString& title, const QString& blurb);
    void unmuteRequested(const QString& tmpl);

private:
    void rebuildAlbums();
    void rebuildExhibits();
    void rebuildMutes();
    QWidget* emptyGuide(const QString& title, const QString& hint);
    QWidget* makeAlbumRow(int i);
    QWidget* makeExhibitRow(int i);
    QWidget* makeMuteRow(int i);

    Library* lib_ = nullptr;
    QString thumbsDir_;
    bool hidePrivate_ = true;
    ThumbCache* cache_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QListWidget* albums_ = nullptr;
    QListWidget* exhibits_ = nullptr;
    QListWidget* mutes_ = nullptr;
    QWidget* albumsEmpty_ = nullptr;
    QWidget* exhibitsEmpty_ = nullptr;
    QWidget* mutesEmpty_ = nullptr;
    QVector<AlbumRow> albumRows_;
    QVector<ExhibitRow> exhibitRows_;
    QVector<Library::MuteRow> muteRows_;
};
