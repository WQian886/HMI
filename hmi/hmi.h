#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_hmi.h"

class hmi : public QMainWindow
{
    Q_OBJECT

public:
    hmi(QWidget *parent = nullptr);
    ~hmi();

private:
    Ui::hmiClass ui;
};

