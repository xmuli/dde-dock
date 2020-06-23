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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "xcb/xcb_misc.h"
#include "dbus/sni/statusnotifierwatcher_interface.h"
#include "panel/mainpanelcontrol.h"
#include "util/multiscreenworker.h"

#include <com_deepin_api_xeventmonitor.h>

#include <DPlatformWindowHandle>
#include <DWindowManagerHelper>
#include <DBlurEffectWidget>
#include <DGuiApplicationHelper>

#include <QWidget>

DWIDGET_USE_NAMESPACE

using XEventMonitor = ::com::deepin::api::XEventMonitor;

class DockSettings;
class DragWidget;
class MainPanel;
class MainPanelControl;
class QTimer;
class MainWindow : public DBlurEffectWidget, public MainPanelDelegate
{
    Q_OBJECT

    enum Flag{
        Motion = 1 << 0,
        Button = 1 << 1,
        Key    = 1 << 2
    };

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void setEffectEnabled(const bool enabled);
    void setComposite(const bool hasComposite);

    friend class MainPanel;
    friend class MainPanelControl;

    QWidget *panel(){return m_mainPanel;}

public slots:
    void launch();
    // 第一次显示
    void initShow();

private:
    using QWidget::show;
    void showEvent(QShowEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void enterEvent(QEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void moveEvent(QMoveEvent *event) override;

    void initSNIHost();
    void initComponents();
    void initConnections();
    void resizeMainWindow();
    void resizeMainPanelWindow();

    bool appIsOnDock(const QString &appDesktop) override;
//    void onRegionMonitorChanged(int x, int y, const QString &key);
    void getTrayVisableItemCount();

signals:
    void panelGeometryChanged();

private slots:
    void compositeChanged();
    void internalMove(const QPoint &p);
    void updateDisplayMode();

    void adjustShadowMask();

    void onDbusNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void onMainWindowSizeChanged(QPoint offset);
    void onDragFinished();
    void themeTypeChanged(DGuiApplicationHelper::ColorType themeType);

private:
    bool m_launched;
    MainPanelControl *m_mainPanel;

    DPlatformWindowHandle m_platformWindowHandle;
    DWindowManagerHelper *m_wmHelper;
    MultiScreenWorker *m_multiScreenWorker;
    XEventMonitor *m_eventInter;
    QString m_registerKey;
    QStringList m_registerKeys;

    QTimer *m_positionUpdateTimer;
    QTimer *m_expandDelayTimer;
    QTimer *m_leaveDelayTimer;
    QTimer *m_shadowMaskOptimizeTimer;
    QVariantAnimation *m_panelShowAni;
    QVariantAnimation *m_panelHideAni;

    QPropertyAnimation *m_showAni;
    QPropertyAnimation *m_hideAni;

//    XcbMisc *m_xcbMisc;
    DockSettings *m_settings;

    QDBusConnectionInterface *m_dbusDaemonInterface;
    org::kde::StatusNotifierWatcher *m_sniWatcher;
    QString m_sniHostService;
    QSize m_size;
    DragWidget *m_dragWidget;
    Position m_dockPosition;
    bool m_mouseCauseDock;

    //　任务栏当前所在屏幕
    QString m_dockCurrentScreen;

};

#endif // MAINWINDOW_H
