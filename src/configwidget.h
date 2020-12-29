#pragma once
#include <QWidget>
#include "ui_configwidget.h"

namespace XWindowSwitcher {
    class ConfigWidget final : public QWidget {
        Q_OBJECT
        public:
            explicit ConfigWidget(QWidget *parent = nullptr);
            ~ConfigWidget();
            Ui::ConfigWidget ui;
    };
}
