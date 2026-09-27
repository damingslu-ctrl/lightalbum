#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include "MapView.h"

class MapPage : public QWidget {
    Q_OBJECT
public:
    explicit MapPage(QWidget* parent = nullptr);

private:
    MapView* view_ = nullptr;
    QLabel*  crumb_ = nullptr;
    QPushButton* back_ = nullptr;
};
