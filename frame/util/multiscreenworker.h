/*
 * Copyright (C) 2018 ~ 2028 Deepin Technology Co., Ltd.
 *
 * Author:     fanpengcheng <fanpengcheng_cm@deepin.com>
 *
 * Maintainer: fanpengcheng <fanpengcheng_cm@deepin.com>
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

#ifndef MULTISCREENWORKER_H
#define MULTISCREENWORKER_H
#include "constants.h"
#include "monitor.h"
#include "utils.h"
#include "item/dockitem.h"

#include "xcb/xcb_misc.h"

#include <com_deepin_dde_daemon_dock.h>
#include <com_deepin_daemon_display.h>
#include <com_deepin_daemon_display_monitor.h>
#include <com_deepin_api_xeventmonitor.h>

#include <DWindowManagerHelper>

#include <QObject>

#define WINDOWMARGIN ((m_displayMode == Dock::Efficient) ? 0 : 10)

DGUI_USE_NAMESPACE

using DBusDock = com::deepin::dde::daemon::Dock;
using DisplayInter = com::deepin::daemon::Display;
using MonitorInter = com::deepin::daemon::display::Monitor;
using XEventMonitor = ::com::deepin::api::XEventMonitor;

using namespace Dock;
class QVariantAnimation;
class QWidget;
class QTimer;
class MainWindow;
class MultiScreenWorker : public QObject
{
    Q_OBJECT
public:
    enum Flag{
        Motion = 1 << 0,
        Button = 1 << 1,
        Key    = 1 << 2
    };

    MultiScreenWorker(QWidget *parent, DWindowManagerHelper *helper);
    ~MultiScreenWorker();

    void initMembers();
    void initConnection();
    //　任务栏第一次启动时被调用
    void initShow();

    void showAni(const QString &screen);
    void hideAni(const QString &screen);

    inline const QString &fromScreen() {return m_fromScreen;}
    inline const QString &toScreen() {return m_toScreen;}
    inline const Position &position() {return m_position;}
    inline const DisplayMode &displayMode() {return m_displayMode;}
    inline const HideMode &hideMode() {return m_hideMode;}
    inline const HideState &hideState() {return m_hideState;}
    inline bool dockVisible() {return m_dockVisible;}
    void updateDockVisible(bool visible){m_dockVisible = visible;}

    // 适用于切换到另外一个位置
    void changeDockPosition(QString fromScreen,QString toScreen,const Position &fromPos,const Position &toPos);

    void updateDockScreenName(const QString &screenName);

    // 任务栏内容区域大小
    QSize contentSize(const QString &screenName);
    // 任务栏正常显示时的区域
    QRect dockRect(const QString &screenName);
    // 处理任务栏的离开事件
    void handleLeaveEvent(QEvent *event);

signals:
    // 任务栏四大信号
    void displayModeChanegd();      //父对象需要更新圆角情况
    void windowHideModeChanged();
    void windowVisibleChanged();

    // 更新监视区域
    void requestUpdateRegionMonitor();

    void requestUpdateFrontendGeometry(const QRect &rect);
    void requestNotifyWindowManager();
    void requestUpdatePosition(const Position &fromPos, const Position &toPos);
    void requestUpdateLayout(const QString &screenName);      //界面需要根据任务栏更新布局的方向

    void dockScreenNameChanged(const QString &screenName);

public slots:
    void onAutoHideChanged(bool autoHide);

private slots:
    void onRegionMonitorChanged(int x, int y, const QString &key);
    void onLeaveMonitorChanged(int x, int y, const QString &key);
    void onMonitorListChanged(const QList<QDBusObjectPath> &mons);
    void monitorAdded(const QString &path);
    void monitorRemoved(const QString &path);

    void showAniFinished();
    void hideAniFinished();

    // 任务栏四大槽函数
    void onPositionChanged();
    void onDisplayModeChanged();
    void hideModeChanged();
    void hideStateChanged();

    void onRequestUpdateRegionMonitor();

    // 通知后端任务栏所在位置
    void onRequestUpdateFrontendGeometry(const QRect &rect);

    void onRequestNotifyWindowManager();
    void onRequestUpdatePosition(const Position &fromPos, const Position &toPos);

    void updateGeometry();
    void updateInterRect(const QList<Monitor *>monitorList, QList<MonitRect> &list);

private:
    MainWindow *parent();
    // 获取任务栏分别显示和隐藏时对应的位置
    QRect getDockShowGeometry(const QString &screenName,const Position &pos, const DisplayMode &displaymode);
    QRect getDockHideGeometry(const QString &screenName,const Position &pos, const DisplayMode &displaymode);

    void updateWindowManagerDock();
    QScreen *screenByName(const QString &screenName);
    bool contains(const MonitRect &rect, const QPoint &pos);
    bool contains(const QList<MonitRect> &rectList, const QPoint &pos);

private:
    QWidget *m_parent;
    DWindowManagerHelper *m_wmHelper;
    XcbMisc *m_xcbMisc;

    XEventMonitor *m_eventInter;
    XEventMonitor *m_leaveMonitorInter;

    DBusDock *m_dockInter;
    DisplayInter *m_displayInter;

    QTimer *m_leaveTimer;

    QVariantAnimation *m_showAni;
    QVariantAnimation *m_hideAni;

    QString m_fromScreen;           // 上一次的屏幕
    QString m_toScreen;             // 下一次的屏幕(最新)

    // 任务栏四大属性
    Position m_position;            // 当前任务栏位置
    HideMode m_hideMode;            // 一直显示，一直隐藏，智能隐藏
    HideState m_hideState;          //
    DisplayMode m_displayMode;      // 时尚，高效

    QMap<Monitor *, MonitorInter *> m_monitorInfo; //屏幕信息

    // 不和其他流程产生交互，不要操作这里的变量
    int m_screenRawHeight;
    int m_screenRawWidth;
    QString m_registerKey;
    QString m_leaveRegisterKey;
    bool m_dockVisible;             // 任务栏当前是否可见
    bool m_aniStart;                // changeDockPosition正在运行中
    bool m_autoHide;                // 和DockSettings保持一致,可以直接使用其单例进行获取
    QList<MonitRect> m_monitorRectList;
    QList<MonitRect> m_interRectList;       // 离开事件发生在这块区域时,不做处理(一直隐藏的模式下,本来是要隐藏的)
};

#endif // MULTISCREENWORKER_H
