/*
 * Copyright (C) 2011 ~ 2018 Deepin Technology Co., Ltd.
 *
 * Author:     sbw <sbw@sbw.so>
 *
 * Maintainer: sbw <sbw@sbw.so>
 *             zhaolong <zhaolong@uniontech.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DOCKSETTINGS_H
#define DOCKSETTINGS_H

#include "constants.h"
#include "monitor.h"

#include <com_deepin_dde_daemon_dock.h>
#include <com_deepin_daemon_display.h>

#include <QAction>
#include <QMenu>

#include <QObject>
#include <QSize>

using namespace Dock;
using DBusDock = com::deepin::dde::daemon::Dock;
using DisplayInter = com::deepin::daemon::Display;

class DockItemManager;
class DockSettings : public QObject
{
    Q_OBJECT
    friend class MultiScreenWorker;

public:
    static DockSettings &Instance();

//    inline DisplayMode displayMode() const { return m_displayMode; }
//    inline HideMode hideMode() const { return m_hideMode; }
//    inline HideState hideState() const { return m_hideState; }
//    inline Position position() const { return m_position; }
//    inline bool autoHide() const { return m_autoHide; }
//    inline quint8 Opacity() const { return quint8(m_opacity * 255); }
//    inline bool menuEnable() const { return m_menuEnable; }

//    void showDockSettingsMenu();

signals:
//    void autoHideChanged(const bool autoHide) const;
//    void windowVisibleChanged() const;
//    void trayCountChanged() const;

public slots:
//    void updateGeometry();
//    void setAutoHide(const bool autoHide);

private slots:
//    void menuActionClicked(QAction *action);
//    void onGSettingsChanged(const QString &key);
    // TODO 是否还有其他的插件未处理其gsettings配置,另外,这里后面可以优化全自动的,而非一个一个单独处理
//    void onTrashGSettingsChanged(const QString &key);

//    void onPositionChanged();
//    void onDisplayModeChanged();
//    void hideModeChanged();
//    void hideStateChanged();

//    void trayVisableCountChanged(const int &count);

private:
    explicit DockSettings(QWidget *parent = nullptr);
    DockSettings(DockSettings const &) = delete;
    DockSettings operator =(DockSettings const &) = delete;

//    void gtkIconThemeChanged();

private:
//    DBusDock *m_dockInter;
//    DisplayInter *m_displayInter;

//    bool m_autoHide;
//    double m_opacity;
//    int m_dockMargin;
//    Position m_position;
//    HideMode m_hideMode;
//    HideState m_hideState;
//    DisplayMode m_displayMode;

    // menu
//    QMenu m_settingsMenu;
//    QMenu *m_hideSubMenu;
//    QAction m_fashionModeAct;
//    QAction m_efficientModeAct;
//    QAction m_topPosAct;
//    QAction m_bottomPosAct;
//    QAction m_leftPosAct;
//    QAction m_rightPosAct;
//    QAction m_keepShownAct;
//    QAction m_keepHiddenAct;
//    QAction m_smartHideAct;
//    bool m_menuEnable;

//    DockItemManager *m_itemManager;
//    bool m_trashPluginShow;
};

#endif // DOCKSETTINGS_H
