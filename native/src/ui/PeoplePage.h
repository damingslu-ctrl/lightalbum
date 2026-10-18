#pragma once
// 2.41.0 spec §2.3 人物馆（People）——一人一展室。
//
// 「展室」= 一个人的全部入藏：姓名（展签）、年跨度（馆务元信息）、色环（稳定哈希选色）、
// 房内照片（日期升序）。展开一间展室即见该人的横串缩略图。
//
// 铁律 20（同源）：buildRooms 是**纯函数**（无 UI、无状态、确定性），门禁 LA_PEOPLE_QA 直调，
// 产品 UI 也直调——绝不复刻第二份聚合逻辑。
#include <QWidget>
#include <QVector>
#include <QColor>
#include <QString>
#include "../core/Library.h"

class ThumbCache;

struct Room {
    QString name;
    int count = 0;
    int yearMin = 0, yearMax = 0;
    QColor ring;                          // 稳定哈希色环（Lumen::personRing 单源）
    QVector<const Photo*> photos;         // 日期升序（date 串升序 stable_sort）
};

class PeoplePage : public QWidget {
    Q_OBJECT
public:
    PeoplePage(const QVector<Photo>* photos, const QString& thumbsDir, QWidget* parent = nullptr);

    void setHidePrivate(bool hide);
    void refresh();

    // 单次扫库聚合：每照 person.split(',') 入对应房（同人多照片 / 一照多人）。
    // usable 谓词同前厅（missing / 空 thumbFile / 私密且要求隐藏 → 不入展）+ person 非空；
    // 房内日期升序；count == photos.size()；yearMin/Max 由房内年份聚合；
    // 输出按 count 降序、同级 name 升序。
    static QVector<Room> buildRooms(const QVector<Photo>& in, bool hidePrivate);

signals:
    void openGaze(const QString& person, const QString& startPath);
    void filterGallery(const QString& person);
    void clusteringRequested();

private:
    void rebuild();
    QWidget* makeRoomBlock(int idx);
    QWidget* makeThumbRow(const Room& r);
    QWidget* makeEmptyCard();

    const QVector<Photo>* photos_ = nullptr;
    QString thumbsDir_;
    bool hidePrivate_ = true;
    ThumbCache* cache_ = nullptr;
    QWidget* content_ = nullptr;
    QVector<Room> rooms_;
    int expanded_ = -1;                   // 展开的展室下标（-1 = 全收起）
};
