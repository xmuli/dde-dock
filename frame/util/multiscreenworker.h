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
#define ANIMATIONTIME 300

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
//    inline bool dockVisible() {return m_dockVisible;}
//    void updateDockVisible(bool visible){m_dockVisible = visible;}

    // 适用于切换到另外一个位置
    void changeDockPosition(QString fromScreen,QString toScreen,const Position &fromPos,const Position &toPos);

    void updateDockScreenName(const QString &screenName);
    /**
     * @brief updateDockScreenName      找一个可以停靠当前位置任务栏的屏幕当目标屏幕
     */
    void updateDockScreenName();
    // 任务栏内容区域大小
    QSize contentSize(const QString &screenName);
    // 任务栏正常隐藏时的区域
    QRect dockRect(const QString &screenName, const HideMode &mode);
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
    void requestUpdateLayout(const QString &screenName);        //　界面需要根据任务栏更新布局的方向
    void requestUpdateDragArea();                               //　更新拖拽区域
    void monitorInfoChaged();                                   //　屏幕信息发生变化，需要更新任务栏大小，拖拽区域，所在屏幕，监控区域，通知窗管，通知后端，

    void updatePositionDone();

public slots:
    void onAutoHideChanged(bool autoHide);
    void updateDaemonDockSize(int dockSize);

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
    void onRequestUpdateDragArea();
    void onMonitorInfoChaged();

    void updateGeometry();
    void updateInterRect(const QList<Monitor *>monitorList, QList<MonitRect> &list);
    void updateMonitorDockedInfo(QMap<Monitor *, MonitorInter *> &map);

private:
    MainWindow *parent();
    // 获取任务栏分别显示和隐藏时对应的位置
    QRect getDockShowGeometry(const QString &screenName,const Position &pos, const DisplayMode &displaymode);
    QRect getDockHideGeometry(const QString &screenName,const Position &pos, const DisplayMode &displaymode);

    void updateWindowManagerDock();
    /**
     * @brief monitorByName     根据屏幕名获取对应信息
     * @param screenName        屏幕名
     * @return                  屏幕信息对应指针
     */
    Monitor *monitorByName(const QMap<Monitor *, MonitorInter *> &map,const QString &screenName);
    QScreen *screenByName(const QString &screenName);
    bool onScreenEdge(const QString &screenName,const QPoint &point);
    bool onScreenEdge(const QPoint &point);
    bool contains(const MonitRect &rect, const QPoint &pos);
    bool contains(const QList<MonitRect> &rectList, const QPoint &pos);
    /**
     * @brief positionDocked    检查任务栏在此屏幕的对应位置是否允许停靠
     * @param screenName        屏幕名
     * @param pos               任务栏位置
     * @return                  true:允许,false:不允许
     */
    bool positionDocked(const QString &screenName, const Position &pos);

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
    HideState m_hideState;          // 智能隐藏设置,按照后端状态直接设置任务栏的显示或隐藏即可
    DisplayMode m_displayMode;      // 时尚，高效

    QMap<Monitor *, MonitorInter *> m_monitorInfo; //屏幕信息

    // 不和其他流程产生交互，不要操作这里的变量
    int m_screenRawHeight;
    int m_screenRawWidth;
    QString m_registerKey;
    QString m_leaveRegisterKey;
//    bool m_dockVisible;             // 任务栏当前是否可见
    bool m_aniStart;                // changeDockPosition正在运行中
    bool m_autoHide;                // 和DockSettings保持一致,可以直接使用其单例进行获取
    QList<MonitRect> m_monitorRectList;     // 监听唤起任务栏区域
    QList<MonitRect> m_interRectList;       // 离开事件发生在这块区域时,不做处理(一直隐藏的模式下,本来是要隐藏的)
};

#endif // MULTISCREENWORKER_H
