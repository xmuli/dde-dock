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

#include "multiscreenworker.h"
#include "window/mainwindow.h"

#include <QWidget>
#include <QScreen>
#include <QEvent>
#include <QRegion>
#include <QSequentialAnimationGroup>
#include <QVariantAnimation>

#include <QDBusConnection>

MultiScreenWorker::MultiScreenWorker(QWidget *parent, DWindowManagerHelper *helper)
    : QObject(nullptr)
    , m_parent(parent)
    , m_wmHelper(helper)
    , m_xcbMisc(XcbMisc::instance())
    , m_eventInter(new XEventMonitor("com.deepin.api.XEventMonitor", "/com/deepin/api/XEventMonitor", QDBusConnection::sessionBus()))
    , m_dockInter(new DBusDock("com.deepin.dde.daemon.Dock", "/com/deepin/dde/daemon/Dock", QDBusConnection::sessionBus(), this))
    , m_displayInter(new DisplayInter("com.deepin.daemon.Display", "/com/deepin/daemon/Display", QDBusConnection::sessionBus(), this))
    , m_monitorUpdateTimer(new QTimer(this))
    , m_showAni(new QVariantAnimation(this))
    , m_hideAni(new QVariantAnimation(this))
    , m_lastScreen(qApp->primaryScreen()->name())
    , m_currentScreen(qApp->primaryScreen()->name())
    , m_position(Dock::Position(m_dockInter->position()))
    , m_hideMode(Dock::HideMode(m_dockInter->hideMode()))
    , m_hideState(Dock::HideState(m_dockInter->hideState()))
    , m_displayMode(Dock::DisplayMode(m_dockInter->displayMode()))
    , m_screenRawHeight(m_displayInter->screenHeight())
    , m_screenRawWidth(m_displayInter->screenWidth())
    , m_aniStart(false)
    , m_draging(false)
    , m_autoHide(true)
{
    qDebug() << "init dock screen: " << m_currentScreen;
    initMembers();
    initConnection();
    onMonitorListChanged(m_displayInter->monitors());
}

MultiScreenWorker::~MultiScreenWorker()
{
    delete m_xcbMisc;
}

void MultiScreenWorker::initMembers()
{
    m_monitorUpdateTimer->setInterval(3000);
    m_monitorUpdateTimer->setSingleShot(true);

    //　设置应用角色为任务栏
    m_xcbMisc->set_window_type(parent()->winId(), XcbMisc::Dock);

    //　初始化动画信息
    m_showAni->setEasingCurve(QEasingCurve::InOutCubic);
    m_hideAni->setEasingCurve(QEasingCurve::InOutCubic);

    const bool composite = m_wmHelper->hasComposite();

#ifndef DISABLE_SHOW_ANIMATION
    const int duration = composite ? ANIMATIONTIME : 0;
#else
    const int duration = 10;
#endif

    m_showAni->setDuration(duration);
    m_hideAni->setDuration(duration);
}

void MultiScreenWorker::initConnection()
{
    connect(m_displayInter, &DisplayInter::MonitorsChanged, this, &MultiScreenWorker::onMonitorListChanged);

    connect(m_showAni, &QVariantAnimation::valueChanged, parent(), [ = ](QVariant value) {
        QRect rect = value.toRect();
        parent()->setFixedSize(rect.size());
        parent()->setGeometry(rect);

        switch (m_position) {
        case Position::Top: {
            const int panelSize = parent()->panel()->height();
            parent()->panel()->move(0, rect.height() - panelSize);
        }
        break;
        case Position::Left: {
            const int panelSize = parent()->panel()->width();
            parent()->panel()->move(rect.width() - panelSize, 0);
        }
        break;
        case Position::Bottom:
        case Position::Right:
            break;
        }
    });

    connect(m_hideAni, &QVariantAnimation::valueChanged, parent(), [ = ](QVariant value) {
        QRect rect = value.toRect();
        parent()->setFixedSize(rect.size());
        parent()->setGeometry(rect);

        switch (m_position) {
        case Position::Top: {
            const int panelSize = parent()->panel()->height();
            parent()->panel()->move(0, rect.height() - panelSize);
        }
        break;
        case Position::Left: {
            const int panelSize = parent()->panel()->width();
            parent()->panel()->move(rect.width() - panelSize, 0);
        }
        break;
        case Position::Bottom:
        case Position::Right:
            break;
        }
    });

    connect(m_showAni, &QVariantAnimation::finished, this, &MultiScreenWorker::showAniFinished);
    connect(m_hideAni, &QVariantAnimation::finished, this, &MultiScreenWorker::hideAniFinished);

    //FIX: 这里关联信号有时候收不到,未查明原因,handleDbusSignal处理
#if 0
    //    connect(m_dockInter, &DBusDock::PositionChanged, this, &MultiScreenWorker::onPositionChanged);
    //    connect(m_dockInter, &DBusDock::DisplayModeChanged, this, &MultiScreenWorker::onDisplayModeChanged);
    //    connect(m_dockInter, &DBusDock::HideModeChanged, this, &MultiScreenWorker::hideModeChanged);
    //    connect(m_dockInter, &DBusDock::HideStateChanged, this, &MultiScreenWorker::hideStateChanged);
#else
    QDBusConnection::sessionBus().connect("com.deepin.dde.daemon.Dock",
                                          "/com/deepin/dde/daemon/Dock",
                                          "org.freedesktop.DBus.Properties",
                                          "PropertiesChanged",
                                          "sa{sv}as",
                                          this, SLOT(handleDbusSignal(QDBusMessage)));
#endif
    connect(m_dockInter, &DBusDock::ServiceRestarted, this, [ = ] {
        updateDockScreenName();
        emit requestUpdateFrontendGeometry(dockRect(m_currentScreen));
    });

    connect(m_displayInter, &DisplayInter::ScreenWidthChanged, this, [ = ](ushort  value) {m_screenRawWidth = value;});
    connect(m_displayInter, &DisplayInter::ScreenHeightChanged, this, [ = ](ushort  value) {m_screenRawHeight = value;});

    connect(m_eventInter, &XEventMonitor::CursorMove, this, &MultiScreenWorker::onRegionMonitorChanged);

    connect(this, &MultiScreenWorker::requestUpdateRegionMonitor, this, &MultiScreenWorker::onRequestUpdateRegionMonitor);
    connect(this, &MultiScreenWorker::requestUpdateFrontendGeometry, this, &MultiScreenWorker::onRequestUpdateFrontendGeometry);
    connect(this, &MultiScreenWorker::requestUpdatePosition, this, &MultiScreenWorker::onRequestUpdatePosition);
    connect(this, &MultiScreenWorker::requestNotifyWindowManager, this, &MultiScreenWorker::onRequestNotifyWindowManager);
    connect(this, &MultiScreenWorker::requestUpdateDragArea, this, &MultiScreenWorker::onRequestUpdateDragArea);
    connect(this, &MultiScreenWorker::monitorInfoChaged, this, &MultiScreenWorker::onMonitorInfoChaged);

    connect(m_monitorUpdateTimer, &QTimer::timeout, this, [ = ] {
        // 更新屏幕停靠信息
        updateMonitorDockedInfo(m_monitorInfo);
        // 更新所在屏幕
        updateDockScreenName();
        // 更新任务栏自身信息
        parent()->setGeometry(dockRect(m_currentScreen));
        parent()->panel()->setFixedSize(dockRect(m_currentScreen).size());
        parent()->panel()->move(0, 0);
        // 通知后端
        emit requestUpdateFrontendGeometry(dockRect(m_currentScreen, m_position, HideMode::KeepShowing, m_displayMode));
        // 拖拽区域
        emit requestUpdateDragArea();
        // 监控区域
        emit requestUpdateRegionMonitor();
        // 通知窗管
        emit requestNotifyWindowManager();
    });
}

void MultiScreenWorker::initShow()
{
    // 仅在初始化时调用一次
    static bool first = true;
    Q_ASSERT(first);
    first = false;

    // 找到一个可以使用的主屏去停靠任务栏
    updateDockScreenName();

    // 初始化界面布局
    emit requestUpdateLayout(m_currentScreen);

    // 设置panel初始大小
    parent()->panel()->setFixedSize(dockRect(m_currentScreen, m_position, HideMode::KeepShowing, m_displayMode).size());
    parent()->panel()->move(0, 0);

    if (m_hideMode == HideMode::KeepShowing)
        showAni(m_currentScreen);
    else {
        QRect rect = getDockHideGeometry(m_currentScreen, m_position, m_displayMode);
        parent()->setFixedSize(rect.size());

        rect = getDockShowGeometry(m_currentScreen, m_position, m_displayMode);
        parent()->panel()->setFixedSize(rect.size());
        parent()->panel()->move(0, 0);

        parent()->setGeometry(rect);
    }
}

void MultiScreenWorker::showAni(const QString &screen)
{
    if (m_showAni->state() == QVariantAnimation::Running || m_aniStart)
        return;

    emit requestUpdateFrontendGeometry(dockRect(m_currentScreen, m_position, HideMode::KeepShowing, m_displayMode));

    // 任务栏位置已经正确就不需要再重复一次动画了
    if (getDockShowGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode) == parent()->geometry()) {
        parent()->panel()->setFixedSize(dockRect(m_currentScreen, m_position, HideMode::KeepShowing, m_displayMode).size());
        parent()->panel()->move(0, 0);
        emit requestNotifyWindowManager();
        return;
    }

    updateDockScreenName(screen);

    if (m_hideAni->state() == QVariantAnimation::Running)
        m_hideAni->stop();

    m_showAni->setStartValue(getDockHideGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode));
    m_showAni->setEndValue(getDockShowGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode));
    m_showAni->start();
}

void MultiScreenWorker::hideAni(const QString &screen)
{
    if (m_hideAni->state() == QVariantAnimation::Running || m_aniStart)
        return;

    // 任务栏位置已经正确就不需要再重复一次动画了
    if (getDockHideGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode) == parent()->geometry()) {
        parent()->panel()->setFixedSize(dockRect(m_currentScreen, m_position, HideMode::KeepShowing, m_displayMode).size());
        parent()->panel()->move(0, 0);
        emit requestNotifyWindowManager();
        return;
    }

    if (m_showAni->state() == QVariantAnimation::Running)
        m_showAni->stop();

    updateDockScreenName(screen);

    m_hideAni->setStartValue(getDockShowGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode));
    m_hideAni->setEndValue(getDockHideGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode));
    m_hideAni->start();
}

void MultiScreenWorker::changeDockPosition(QString fromScreen, QString toScreen, const Position &fromPos, const Position &toPos)
{
    if (fromScreen == toScreen && fromPos == toPos) {
        qDebug() << "shoulen't be here,nothing happend!";
        return;
    }
    // TODO: 考虑切换过快的情况,这里需要停止上一次的动画,可增加信息号控制,暂时无需要
    qDebug() << "from: " << fromScreen << "  to: " << toScreen;
    updateDockScreenName(toScreen);

    QSequentialAnimationGroup *group = new QSequentialAnimationGroup(this);

    QVariantAnimation *ani1 = new QVariantAnimation(group);
    QVariantAnimation *ani2 = new QVariantAnimation(group);

    //　初始化动画信息
    ani1->setEasingCurve(QEasingCurve::InOutCubic);
    ani2->setEasingCurve(QEasingCurve::InOutCubic);

    const bool composite = m_wmHelper->hasComposite();
#ifndef DISABLE_SHOW_ANIMATION
    const int duration = composite ? ANIMATIONTIME : 0;
#else
    const int duration = 10;
#endif

    ani1->setDuration(duration);
    ani2->setDuration(duration);

    //　隐藏
    ani1->setStartValue(getDockShowGeometry(fromScreen, static_cast<Position>(fromPos), m_displayMode));
    ani1->setEndValue(getDockHideGeometry(fromScreen, static_cast<Position>(fromPos), m_displayMode));
#ifdef QT_DEBUG
    qDebug() << fromScreen << "hide from :" << getDockShowGeometry(fromScreen, static_cast<Position>(fromPos), m_displayMode);
    qDebug() << fromScreen << "hide to   :" << getDockHideGeometry(fromScreen, static_cast<Position>(fromPos), m_displayMode);
#endif

    //　显示
    ani2->setStartValue(getDockHideGeometry(toScreen, static_cast<Position>(toPos), m_displayMode));
    ani2->setEndValue(getDockShowGeometry(toScreen, static_cast<Position>(toPos), m_displayMode));
#ifdef QT_DEBUG
    qDebug() << toScreen << "show from :" << getDockHideGeometry(toScreen, static_cast<Position>(toPos), m_displayMode);
    qDebug() << toScreen << "show to   :" << getDockShowGeometry(toScreen, static_cast<Position>(toPos), m_displayMode);
#endif

    group->addAnimation(ani1);
    group->addAnimation(ani2);

    connect(ani1, &QVariantAnimation::valueChanged, this, [ = ](QVariant value) {
        parent()->setFixedSize(value.toRect().size());
        parent()->setGeometry(value.toRect());

        switch (m_position) {
        case Position::Top: {
            const int panelSize = parent()->panel()->height();
            parent()->panel()->move(0, dockRect(m_currentScreen).height() - panelSize);
        }
        break;
        case Position::Left: {
            const int panelSize = parent()->panel()->width();
            parent()->panel()->move(dockRect(m_currentScreen).width() - panelSize, 0);
        }
        break;
        case Position::Bottom:
        case Position::Right:
            break;
        }
    });

    connect(ani2, &QVariantAnimation::valueChanged, this, [ = ](QVariant value) {
        parent()->setFixedSize(value.toRect().size());
        parent()->setGeometry(value.toRect());

        switch (m_position) {
        case Position::Top: {
            const int panelSize = parent()->panel()->height();
            parent()->panel()->move(0, dockRect(m_currentScreen).height() - panelSize);
        }
        break;
        case Position::Left: {
            const int panelSize = parent()->panel()->width();
            parent()->panel()->move(dockRect(m_currentScreen).width() - panelSize, 0);
        }
        break;
        case Position::Bottom:
        case Position::Right:
            break;
        }
    });

    // 如果更改了显示位置，在显示之前应该更新一下界面布局方向
    if (fromPos != toPos)
        connect(ani1, &QVariantAnimation::finished, this, [ = ] {
        // 预设一下大小
        parent()->panel()->setFixedSize(dockRect(m_currentScreen, m_position, HideMode::KeepHidden, m_displayMode).size());
        //隐藏后需要通知界面更新布局方向
        emit requestUpdateLayout(m_currentScreen);
    }, Qt::DirectConnection);


    connect(group, &QVariantAnimation::finished, this, [ = ] {
        m_aniStart = false;

        //　结束之后需要根据确定需要再隐藏
        emit showAniFinished();
        emit requestUpdateFrontendGeometry(dockRect(m_currentScreen, m_position, HideMode::KeepShowing, m_displayMode));
        emit requestNotifyWindowManager();
    });

    m_aniStart = true;

    group->start(QVariantAnimation::DeleteWhenStopped);
}

void MultiScreenWorker::updateDockScreenName(const QString &screenName)
{
    if (m_currentScreen == screenName)
        return;

    m_lastScreen = m_currentScreen;
    m_currentScreen = screenName;

    qDebug() << "update dock screen: " << screenName;

    emit requestUpdateLayout(m_currentScreen);
}

void MultiScreenWorker::updateDockScreenName()
{
    QList<Monitor *> monitorList = m_monitorInfo.keys();
    if (monitorList.size() == 2) {
        Monitor *primaryMonitor = monitorByName(m_monitorInfo, m_currentScreen);
        if (!primaryMonitor->dockPosition().docked(position())) {
            foreach (auto monitor, monitorList) {
                if (monitor->name() != m_currentScreen
                        && monitor->dockPosition().docked(position())) {
                    updateDockScreenName(monitor->name());
                }
            }
        }
    }
}

QRect MultiScreenWorker::dockRect(const QString &screenName, const Position &pos, const HideMode &hideMode, const DisplayMode &displayMode)
{
    if (hideMode == HideMode::KeepShowing)
        return getDockShowGeometry(screenName, pos, displayMode);
    else
        return getDockHideGeometry(screenName, pos, displayMode);
}

QRect MultiScreenWorker::dockRect(const QString &screenName)
{
    return dockRect(screenName, m_position, m_hideMode, m_displayMode);
}

void MultiScreenWorker::handleLeaveEvent(QEvent *event)
{
    Q_UNUSED(event);

    if (m_hideMode == HideMode::KeepShowing)
        return;

    if (!m_autoHide)
        return;

    // 判断是否还在'离开'监控区域内,是的话不处理,否则按照显示状态来处理
    if (!contains(m_monitorRectList, scaledPos(QCursor::pos()))) {
        switch (m_hideMode) {
        case HideMode::SmartHide:
        case HideMode::KeepHidden:
            hideAni(m_currentScreen);
            break;
        case HideMode::KeepShowing:
            break;
        }
    }
}

void MultiScreenWorker::onAutoHideChanged(bool autoHide)
{
    m_autoHide = autoHide;

    if (m_autoHide) {
        switch (m_hideMode) {
        case HideMode::KeepHidden:
        case HideMode::SmartHide:
            hideAni(m_currentScreen);
            break;
        case HideMode::KeepShowing:
            break;
        }
    }
}

void MultiScreenWorker::updateDaemonDockSize(int dockSize)
{
    m_dockInter->setWindowSize(uint(dockSize));
    if (m_displayMode == DisplayMode::Fashion)
        m_dockInter->setWindowSizeFashion(uint(dockSize));
    else
        m_dockInter->setWindowSizeEfficient(uint(dockSize));
}

void MultiScreenWorker::onDragStateChanged(bool draging)
{
    m_draging = draging;
}

void MultiScreenWorker::handleDbusSignal(QDBusMessage msg)
{
    QList<QVariant> arguments = msg.arguments();
    // 参数固定长度
    if (3 != arguments.count())
        return;
    // 返回的数据中,这一部分对应的是数据发送方的interfacename,可判断是否是自己需要的服务
    QString interfaceName = msg.arguments().at(0).toString();
    if (interfaceName != "com.deepin.dde.daemon.Dock")
        return;
    QVariantMap changedProps = qdbus_cast<QVariantMap>(arguments.at(1).value<QDBusArgument>());
    QStringList keys = changedProps.keys();
    foreach (const QString &prop, keys) {
        if (prop == "Position") {
            onPositionChanged();
        } else if (prop == "DisplayMode") {
            onDisplayModeChanged();
        } else if (prop == "HideMode") {
            onHideModeChanged();
        } else if (prop == "HideState") {
            onHideStateChanged();
        }
    }
}

void MultiScreenWorker::onRegionMonitorChanged(int x, int y, const QString &key)
{
    if (m_registerKey != key)
        return;

    if (m_draging) {
        qDebug() << "dock is draging";
        return;
    }

    QString toScreen;
    QScreen *screen = Utils::screenAtByScaled(QPoint(x, y));
    if (!screen) {
        qDebug() << "cannot find the screen" << QPoint(x, y);
        return;
    }

    toScreen = screen->name();

    /**
     * 坐标位于当前屏幕边缘时,当做屏幕内移动处理(防止鼠标移动到边缘时不唤醒任务栏)
     * 使用screenAtByScaled获取屏幕名时,实际上获取的不一定是当前屏幕
     * 举例:点(100,100)不在(0,0,100,100)的屏幕上
     */
    if (onScreenEdge(m_currentScreen, QPoint(x, y))) {
        toScreen = m_currentScreen;
    }

    // 过滤重复坐标
    static QPoint lastPos(0, 0);
    if (lastPos == QPoint(x, y)) {
        qDebug() << "same point,should return";
        return;
    }
    lastPos = QPoint(x, y);

#ifdef QT_DEBUG
    qDebug() << x << y << toScreen << m_currentScreen;
#endif

    // 任务栏显示状态，但需要切换屏幕
    if (toScreen != m_currentScreen) {
        Monitor *currentMonitor = monitorByName(m_monitorInfo, toScreen);
        if (!currentMonitor) {
            qDebug() << "cannot find monitor by name: " << toScreen;
            return;
        }

        // 检查边缘是否允许停靠
        if (currentMonitor->dockPosition().docked(static_cast<Position>(m_dockInter->position())))
            changeDockPosition(m_currentScreen, screen->name()
                               , static_cast<Position>(m_dockInter->position())
                               , static_cast<Position>(m_dockInter->position()));
    } else {
        // 任务栏隐藏状态，但需要显示
        if (hideMode() == HideMode::KeepShowing) {
#ifdef QT_DEBUG
            qDebug() << "showing";
#endif
            parent()->setGeometry(dockRect(m_currentScreen));
            return;
        }

        if (m_showAni->state() == QVariantAnimation::Running) {
#ifdef QT_DEBUG
            qDebug() << "animation is running";
#endif
            return;
        }

        const QRect boundRect = parent()->visibleRegion().boundingRect();
#ifdef QT_DEBUG
        qDebug() << "boundRect:" << boundRect;
#endif
        if ((hideMode() == HideMode::KeepHidden || m_hideMode == HideMode::SmartHide)
                && (boundRect.isEmpty())) {
            showAni(m_currentScreen);
        }
    }
}

void MultiScreenWorker::onMonitorListChanged(const QList<QDBusObjectPath> &mons)
{
    if (mons.isEmpty())
        return;

    QList<QString> ops;
    for (const auto *mon : m_monitorInfo.keys())
        ops << mon->path();

    QList<QString> pathList;
    for (auto op : mons) {
        const QString path = op.path();
        pathList << path;
        if (!ops.contains(path))
            monitorAdded(path);
    }

    for (auto op : ops)
        if (!pathList.contains(op))
            monitorRemoved(op);
}

void MultiScreenWorker::monitorAdded(const QString &path)
{
    MonitorInter *inter = new MonitorInter("com.deepin.daemon.Display", path, QDBusConnection::sessionBus(), this);
    Monitor *mon = new Monitor(this);

    connect(inter, &MonitorInter::XChanged, mon, &Monitor::setX);
    connect(inter, &MonitorInter::YChanged, mon, &Monitor::setY);
    connect(inter, &MonitorInter::WidthChanged, mon, &Monitor::setW);
    connect(inter, &MonitorInter::HeightChanged, mon, &Monitor::setH);
    connect(inter, &MonitorInter::MmWidthChanged, mon, &Monitor::setMmWidth);
    connect(inter, &MonitorInter::MmHeightChanged, mon, &Monitor::setMmHeight);
    connect(inter, &MonitorInter::RotationChanged, mon, &Monitor::setRotate);
    connect(inter, &MonitorInter::NameChanged, mon, &Monitor::setName);
    connect(inter, &MonitorInter::CurrentModeChanged, mon, &Monitor::setCurrentMode);
    connect(inter, &MonitorInter::ModesChanged, mon, &Monitor::setModeList);
    connect(inter, &MonitorInter::RotationsChanged, mon, &Monitor::setRotateList);
    connect(inter, &MonitorInter::EnabledChanged, mon, &Monitor::setMonitorEnable);
    connect(m_displayInter, static_cast<void (DisplayInter::*)(const QString &) const>(&DisplayInter::PrimaryChanged), mon, &Monitor::setPrimary);

    //　屏幕信息发生变化(注意:这里需要放在上面的关联之后,确保上面数据先更新,再走monitorInfoChaged,从而保证后面的一系列操作拿到的都是正确的数据)
    connect(inter, &MonitorInter::XChanged, this, &MultiScreenWorker::monitorInfoChaged);
    connect(inter, &MonitorInter::YChanged, this, &MultiScreenWorker::monitorInfoChaged);
    connect(inter, &MonitorInter::WidthChanged, this, &MultiScreenWorker::monitorInfoChaged);
    connect(inter, &MonitorInter::HeightChanged, this, &MultiScreenWorker::monitorInfoChaged);

    // NOTE: DO NOT using async dbus call. because we need to have a unique name to distinguish each monitor
    Q_ASSERT(inter->isValid());
    mon->setName(inter->name());

    mon->setMonitorEnable(inter->enabled());
    mon->setPath(path);
    mon->setX(inter->x());
    mon->setY(inter->y());
    mon->setW(inter->width());
    mon->setH(inter->height());
    mon->setRotate(inter->rotation());
    mon->setCurrentMode(inter->currentMode());
    mon->setModeList(inter->modes());

    mon->setRotateList(inter->rotations());
    mon->setPrimary(m_displayInter->primary());
    mon->setMmWidth(inter->mmWidth());
    mon->setMmHeight(inter->mmHeight());

    m_monitorInfo.insert(mon, inter);
    inter->setSync(false);

    // 更新位置停靠信息
    updateMonitorDockedInfo(m_monitorInfo);

    emit requestUpdateRegionMonitor();
}

void MultiScreenWorker::monitorRemoved(const QString &path)
{
    Monitor *monitor = nullptr;
    for (auto it(m_monitorInfo.cbegin()); it != m_monitorInfo.cend(); ++it) {
        if (it.key()->path() == path) {
            monitor = it.key();
            break;
        }
    }
    if (!monitor)
        return;

    m_monitorInfo.value(monitor)->deleteLater();
    m_monitorInfo.remove(monitor);

    monitor->deleteLater();
}

void MultiScreenWorker::showAniFinished()
{
    const QRect rect = dockRect(m_currentScreen, m_position, HideMode::KeepShowing, m_displayMode);

    parent()->panel()->setFixedSize(rect.size());
    parent()->panel()->move(0, 0);

    emit requestUpdateFrontendGeometry(rect);
    emit requestNotifyWindowManager();
    emit requestUpdateDragArea();
}

void MultiScreenWorker::hideAniFinished()
{
    const QRect rect = dockRect(m_currentScreen, m_position, HideMode::KeepShowing, m_displayMode);

    parent()->panel()->setFixedSize(rect.size());
    parent()->panel()->move(0, 0);

    emit requestUpdateFrontendGeometry(rect);
    emit requestNotifyWindowManager();
}

void MultiScreenWorker::onPositionChanged()
{
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    const Position position = Dock::Position(m_dockInter->position());
    Position lastPos = m_position;
    if (lastPos == position)
        return;
#ifdef QT_DEBUG
    qDebug() << "position change from: " << lastPos << " to: " << position;
#endif
    m_position = position;

    // 更新鼠标拖拽样式
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    if ((Top == m_position) || (Bottom == m_position)) {
        parent()->panel()->setCursor(Qt::SizeVerCursor);
    } else {
        parent()->panel()->setCursor(Qt::SizeHorCursor);
    }

    DockItem::setDockPosition(position);
    qApp->setProperty(PROP_POSITION, QVariant::fromValue(position));

    emit requestUpdatePosition(lastPos, position);
    emit requestUpdateRegionMonitor();
}

void MultiScreenWorker::onDisplayModeChanged()
{
    DisplayMode displayMode = Dock::DisplayMode(m_dockInter->displayMode());

    if (displayMode == m_displayMode)
        return;

    qDebug() << "display mode change:" << displayMode;

    m_displayMode = displayMode;

    DockItem::setDockDisplayMode(displayMode);
    qApp->setProperty(PROP_DISPLAY_MODE, QVariant::fromValue(displayMode));

    //    // 不显示的就不用处理,在显示时会再处理一遍的
    //    if (parent()->visibleRegion().boundingRect().isEmpty())
    //        return;

    parent()->setFixedSize(dockRect(m_currentScreen).size());
    parent()->move(dockRect(m_currentScreen).topLeft());

    emit displayModeChanegd();
    emit requestUpdateRegionMonitor();
    emit requestUpdateFrontendGeometry(dockRect(m_currentScreen, m_position, HideMode::KeepShowing, m_displayMode));
    emit requestNotifyWindowManager();
}

void MultiScreenWorker::onHideModeChanged()
{
    HideMode hideMode = Dock::HideMode(m_dockInter->hideMode());

    if (m_hideMode == hideMode)
        return;

    qDebug() << "hidemode change:" << hideMode;

    m_hideMode = hideMode;

    emit requestUpdateFrontendGeometry(dockRect(m_currentScreen));
    emit requestNotifyWindowManager();
}

void MultiScreenWorker::onHideStateChanged()
{
    const Dock::HideState state = Dock::HideState(m_dockInter->hideState());

    if (state == Dock::Unknown)
        return;

    qDebug() << "hidestate change:" << state;

    m_hideState = state;

    // 两种隐藏模式下都可以被唤醒
    if (m_hideMode == HideMode::SmartHide
            || m_hideMode == HideMode::KeepHidden) {
        if (m_hideState == HideState::Show)
            showAni(m_currentScreen);
        else if (m_hideState == HideState::Hide)
            hideAni(m_currentScreen);
    } else if (m_hideMode == HideMode::KeepShowing) {
        showAni(m_currentScreen);
    }
}

void MultiScreenWorker::onRequestUpdateRegionMonitor()
{
    if (!m_registerKey.isEmpty()) {
        bool ret = m_eventInter->UnregisterArea(m_registerKey);
#ifdef QT_DEBUG
        qDebug() << "取消唤起区域监听:" << ret;
#endif
    }

    const static int flags = Motion | Button | Key;
    const static int monitorHeight = 15;

    // 任务栏唤起区域
    m_monitorRectList.clear();
    foreach (Monitor *inter, m_monitorInfo.keys()) {
        MonitRect rect;
        switch (static_cast<Position>(m_dockInter->position())) {
        case Top: {
            rect.x1 = inter->x();
            rect.y1 = inter->y();
            rect.x2 = inter->x() + inter->w();
            rect.y2 = inter->y() + monitorHeight;
        }
        break;
        case Bottom: {
            rect.x1 = inter->x();
            rect.y1 = inter->y() + inter->h() - monitorHeight;
            rect.x2 = inter->x() + inter->w();
            rect.y2 = inter->y() + inter->h();
        }
        break;
        case Left: {
            rect.x1 = inter->x();
            rect.y1 = inter->y();
            rect.x2 = inter->x() + monitorHeight;
            rect.y2 = inter->y() + inter->h();
        }
        break;
        case Right: {
            rect.x1 = inter->x() + inter->w() - monitorHeight;
            rect.y1 = inter->y();
            rect.x2 = inter->x() + inter->w();
            rect.y2 = inter->y() + inter->h();
        }
        break;
        }

        if (!m_monitorRectList.contains(rect)) {
            m_monitorRectList << rect;
#ifdef QT_DEBUG
            qDebug() << "监听区域：" << rect.x1 << rect.y1 << rect.x2 << rect.y2;
#endif
        }
    }

    // 任务栏事件区域,用于模拟离开事件
    QList<MonitRect> fulleScreenMonitorList;
    foreach (Monitor *inter, m_monitorInfo.keys()) {
        MonitRect rect;
        rect.x1 = inter->x();
        rect.y1 = inter->y();
        rect.x2 = inter->x() + inter->w();
        rect.y2 = inter->y() + inter->h();

        if (!fulleScreenMonitorList.contains(rect)) {
            fulleScreenMonitorList << rect;
#ifdef QT_DEBUG
            qDebug() << "全屏监听区域：" << rect.x1 << rect.y1 << rect.x2 << rect.y2;
#endif
        }
    }

    m_registerKey = m_eventInter->RegisterAreas(m_monitorRectList, flags);
}

void MultiScreenWorker::onRequestUpdateFrontendGeometry(const QRect &rect)
{
    const qreal scale = scaleByName(m_currentScreen);
#ifdef QT_DEBUG
    qDebug() << rect;
#endif

    m_dockInter->SetFrontendWindowRect(rect.x() / scale, rect.y() / scale, rect.width() / scale, rect.height() / scale);
}

void MultiScreenWorker::onRequestNotifyWindowManager()
{
    updateWindowManagerDock();
}

void MultiScreenWorker::onRequestUpdatePosition(const Position &fromPos, const Position &toPos)
{
    // 切换位置默认切换到主屏，主屏不允许时再尝试其他屏幕
    updateDockScreenName();

    changeDockPosition(m_lastScreen, m_currentScreen, fromPos, toPos);
}

void MultiScreenWorker::onRequestUpdateDragArea()
{
    parent()->resetDragWindow();
}

void MultiScreenWorker::onMonitorInfoChaged()
{
    if (m_monitorInfo.keys().size() == 2
            && m_monitorInfo.keys().first()->rect() == m_monitorInfo.keys().last()->rect()) {
        qDebug() << "repeat screen";
        return;
    }

    if (m_monitorUpdateTimer->isActive()) {
        m_monitorUpdateTimer->stop();
    }

    m_monitorUpdateTimer->start();
}

//void MultiScreenWorker::updateGeometry()
//{
////    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
////    Q_ASSERT(sender() == m_leaveTimer);

//    if (!m_autoHide)
//        return;

//    if (contains(m_interRectList, scaledPos(QCursor::pos())))
//        return;

//    switch (m_hideMode) {
//    case HideMode::SmartHide:
//    case HideMode::KeepHidden:
//        hideAni(toScreen());
//        break;
//    case HideMode::KeepShowing:
//        break;
//    }
//}

void MultiScreenWorker::updateInterRect(const QList<Monitor *> monitorList, QList<MonitRect> &list)
{
    list.clear();

    const int dockSize = int(displayMode() == DisplayMode::Fashion ? m_dockInter->windowSizeFashion() : m_dockInter->windowSizeEfficient());

    foreach (Monitor *inter, monitorList) {
        MonitRect rect;
        switch (static_cast<Position>(m_dockInter->position())) {
        case Top: {
            rect.x1 = inter->x();
            rect.y1 = inter->y();
            rect.x2 = inter->x() + inter->w();
            rect.y2 = inter->y() + dockSize + WINDOWMARGIN;
        }
        break;
        case Bottom: {
            rect.x1 = inter->x();
            rect.y1 = inter->y() + inter->h() - dockSize - WINDOWMARGIN;
            rect.x2 = inter->x() + inter->w();
            rect.y2 = inter->y() + inter->h();
        }
        break;
        case Left: {
            rect.x1 = inter->x();
            rect.y1 = inter->y();
            rect.x2 = inter->x() + dockSize + WINDOWMARGIN;
            rect.y2 = inter->y() + inter->h();
        }
        break;
        case Right: {
            rect.x1 = inter->x() + inter->w() - dockSize - WINDOWMARGIN;
            rect.y1 = inter->y();
            rect.x2 = inter->x() + inter->w();
            rect.y2 = inter->y() + inter->h();
        }
        break;
        }

        list << rect;
    }
}

void MultiScreenWorker::updateMonitorDockedInfo(QMap<Monitor *, MonitorInter *> &map)
{
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    QList<Monitor *>screens = map.keys();

    // 最多支持双屏,这里只计算双屏,单屏默认四边均可停靠任务栏
    if (screens.size() != 2)
        return;

    Monitor *s1 = screens.at(0);
    Monitor *s2 = screens.at(1);
    if (!s1 || !s2) {
        qFatal("shouldn't be here");
    }

    if (s1->bottomRight() == s2->topLeft()
            || s1->topLeft() == s2->bottomRight()) {
        s1->dockPosition().reset();
        s2->dockPosition().reset();
        return;
    }

    // 对齐
    bool isAligment = false;
    // 左右拼
    // s1左 s2右
    if (s1->right() == s2->left()) {
        isAligment = (s1->topRight() == s2->topLeft())
                     && (s1->bottomRight() == s2->bottomLeft());
        if (isAligment) {
            s1->dockPosition().rightDock = false;
            s2->dockPosition().leftDock = false;
        }
    }
    // s1右 s2左
    if (s1->left() == s2->right()) {
        isAligment = (s1->topLeft() == s2->topRight())
                     && (s1->bottomLeft() == s2->bottomRight());
        if (isAligment) {
            s1->dockPosition().leftDock = false;
            s2->dockPosition().rightDock = false;
        }
    }

    // 上下拼
    // s1上 s2下
    if (s1->bottom() == s2->top()) {
        isAligment = (s1->bottomLeft() == s2->topLeft())
                     && (s1->bottomRight() == s2->topRight());
        if (isAligment) {
            s1->dockPosition().bottomDock = false;
            s2->dockPosition().topDock = false;
        }
    }
    // s1下 s2上
    if (s1->top() == s2->bottom()) {
        isAligment = (s1->topLeft() == s2->bottomLeft())
                     && (s1->topRight() == s2->bottomRight());
        if (isAligment) {
            s1->dockPosition().topDock = false;
            s2->dockPosition().bottomDock = false;
        }
    }
}

MainWindow *MultiScreenWorker::parent()
{
    return static_cast<MainWindow *>(m_parent);
}

QRect MultiScreenWorker::getDockShowGeometry(const QString &screenName, const Position &pos, const DisplayMode &displaymode)
{
    QRect rect;
    foreach (Monitor *inter, m_monitorInfo.keys()) {
        if (inter->name() == screenName) {
            const int dockSize = int(displaymode == DisplayMode::Fashion ? m_dockInter->windowSizeFashion() : m_dockInter->windowSizeEfficient());

            switch (static_cast<Position>(pos)) {
            case Top: {
                rect.setX(inter->x() + WINDOWMARGIN);
                rect.setY(inter->y() + WINDOWMARGIN);
                rect.setWidth(inter->w() - 2 * WINDOWMARGIN);
                rect.setHeight(dockSize);
            }
            break;
            case Bottom: {
                rect.setX(inter->x() + WINDOWMARGIN);
                rect.setY(inter->y() + inter->h() - WINDOWMARGIN - dockSize);
                rect.setWidth(inter->w() - 2 * WINDOWMARGIN);
                rect.setHeight(dockSize);
            }
            break;
            case Left: {
                rect.setX(inter->x() + WINDOWMARGIN);
                rect.setY(inter->y() + WINDOWMARGIN);
                rect.setWidth(dockSize);
                rect.setHeight(inter->h() - 2 * WINDOWMARGIN);
            }
            break;
            case Right: {
                rect.setX(inter->x() + inter->w() - WINDOWMARGIN - dockSize);
                rect.setY(inter->y() + WINDOWMARGIN);
                rect.setWidth(dockSize);
                rect.setHeight(inter->h() - 2 * WINDOWMARGIN);
            }
            }
            break;
        }
    }

#ifdef QT_DEBUG
//    qDebug() << rect;
#endif

    return rect;
}

QRect MultiScreenWorker::getDockHideGeometry(const QString &screenName, const Position &pos, const DisplayMode &displaymode)
{
    QRect rect;
    foreach (Monitor *inter, m_monitorInfo.keys()) {
        if (inter->name() == screenName) {
            const int margin = (displaymode == DisplayMode::Fashion ? WINDOWMARGIN : 0);

            switch (static_cast<Position>(pos)) {
            case Top: {
                rect.setX(inter->x() + margin);
                rect.setY(inter->y());
                rect.setWidth(inter->w() - 2 * margin);
                rect.setHeight(0);
            }
            break;
            case Bottom: {
                rect.setX(inter->x() + margin);
                rect.setY(inter->y() + inter->h());
                rect.setWidth(inter->w() - 2 * margin);
                rect.setHeight(0);
            }
            break;
            case Left: {
                rect.setX(inter->x());
                rect.setY(inter->y() + margin);
                rect.setWidth(0);
                rect.setHeight(inter->h() - 2 * margin);
            }
            break;
            case Right: {
                rect.setX(inter->x() + inter->w());
                rect.setY(inter->y() + margin);
                rect.setWidth(0);
                rect.setHeight(inter->h() - 2 * margin);
            }
            break;
            }
        }
    }

#ifdef QT_DEBUG
//    qDebug() << rect;
#endif

    return rect;
}

void MultiScreenWorker::updateWindowManagerDock()
{
    // 先清楚原先的窗管任务栏区域
    m_xcbMisc->clear_strut_partial(parent()->winId());

    if (m_hideMode != Dock::KeepShowing)
        return;

    const auto ratio = parent()->devicePixelRatioF();

    const QRect rect = getDockShowGeometry(m_currentScreen, m_position, m_displayMode);
    qDebug() << "wm dock area: " << rect;

    const QPoint &p = rawXPosition(rect.topLeft());

    qDebug() << p << rawXPosition(rect.topLeft()) << ratio
             << m_screenRawHeight << m_screenRawWidth << m_dockInter->position();

    XcbMisc::Orientation orientation = XcbMisc::OrientationTop;
    uint strut = 0;
    uint strutStart = 0;
    uint strutEnd = 0;

    switch (m_dockInter->position()) {
    case Position::Top:
        orientation = XcbMisc::OrientationTop;
        strut = p.y() + rect.height() * ratio;
        strutStart = p.x();
        strutEnd = qMin(qRound(p.x() + rect.width() * ratio), rect.right());
        break;
    case Position::Bottom:
        orientation = XcbMisc::OrientationBottom;
        strut = m_screenRawHeight - p.y();
        strutStart = p.x();
        strutEnd = qMin(qRound(p.x() + rect.width() * ratio), rect.right());
        break;
    case Position::Left:
        orientation = XcbMisc::OrientationLeft;
        strut = p.x() + rect.width() * ratio;
        strutStart = p.y();
        strutEnd = qMin(qRound(p.y() + rect.height() * ratio), rect.bottom());
        break;
    case Position::Right:
        orientation = XcbMisc::OrientationRight;
        strut = m_screenRawWidth - p.x();
        strutStart = p.y();
        strutEnd = qMin(qRound(p.y() + rect.height() * ratio), rect.bottom());
        break;
    }

#ifdef QT_DEBUG
    qDebug() << strut << strutStart << strutEnd;
#endif
    m_xcbMisc->set_strut_partial(parent()->winId(), orientation, strut + WINDOWMARGIN * ratio, strutStart, strutEnd);
}

Monitor *MultiScreenWorker::monitorByName(const QMap<Monitor *, MonitorInter *> &map, const QString &screenName)
{
    foreach (auto monitor, map.keys()) {
        if (monitor->name() == screenName) {
            return monitor;
        }
    }
    return nullptr;
}

QScreen *MultiScreenWorker::screenByName(const QString &screenName)
{
    foreach (QScreen *screen, qApp->screens()) {
        if (screen->name() == screenName)
            return screen;
    }
    return nullptr;
}

qreal MultiScreenWorker::scaleByName(const QString &screenName)
{
    foreach (auto screen, qApp->screens()) {
        if (screen->name() == screenName)
            return screen->devicePixelRatio();
    }

    return qApp->devicePixelRatio();;
}

bool MultiScreenWorker::onScreenEdge(const QString &screenName, const QPoint &point)
{
    bool ret = false;
    QScreen *screen = screenByName(screenName);
    if (screen) {
        const QRect r { screen->geometry() };
        const QRect rect { r.topLeft(), r.size() *screen->devicePixelRatio() };
        if (rect.x() == point.x()
                || rect.x() + rect.width() == point.x()
                || rect.y() == point.y()
                || rect.y() + rect.height() == point.y())
            ret = true;
    }
    return ret;
}

bool MultiScreenWorker::onScreenEdge(const QPoint &point)
{
    bool ret = false;
    foreach (QScreen *screen, qApp->screens()) {
        const QRect r { screen->geometry() };
        const QRect rect { r.topLeft(), r.size() *screen->devicePixelRatio() };
        if (rect.x() == point.x()
                || rect.x() + rect.width() == point.x()
                || rect.y() == point.y()
                || rect.y() + rect.height() == point.y())
            ret = true;
        break;
    }

    return ret;
}

bool MultiScreenWorker::contains(const MonitRect &rect, const QPoint &pos)
{
    return (pos.x() <= rect.x2 && pos.x() >= rect.x1 && pos.y() >= rect.y1 && pos.y() <= rect.y2);
}

bool MultiScreenWorker::contains(const QList<MonitRect> &rectList, const QPoint &pos)
{
    bool ret = false;
    foreach (auto rect, rectList) {
        if (contains(rect, pos)) {
            ret = true;
            break;
        }
    }
    return ret;
}

const QPoint MultiScreenWorker::rawXPosition(const QPoint &scaledPos)
{
    QScreen const *screen = Utils::screenAtByScaled(scaledPos);

    return screen ? screen->geometry().topLeft() +
           (scaledPos - screen->geometry().topLeft()) *
           screen->devicePixelRatio()
           : scaledPos;
}

const QPoint MultiScreenWorker::scaledPos(const QPoint &rawXPos)
{
    QScreen const *screen = Utils::screenAt(rawXPos);

    return screen
           ? screen->geometry().topLeft() +
           (rawXPos - screen->geometry().topLeft()) / screen->devicePixelRatio()
           : rawXPos;
}
