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
const QPoint rawXPosition1(const QPoint &scaledPos)
{
    QScreen const *screen = Utils::screenAtByScaled(scaledPos);

    return screen ? screen->geometry().topLeft() +
                    (scaledPos - screen->geometry().topLeft()) *
                    screen->devicePixelRatio()
                  : scaledPos;
}

const QPoint scaledPos1(const QPoint &rawXPos)
{
    QScreen const *screen = Utils::screenAt(rawXPos);

    return screen
            ? screen->geometry().topLeft() +
              (rawXPos - screen->geometry().topLeft()) / screen->devicePixelRatio()
            : rawXPos;
}
MultiScreenWorker::MultiScreenWorker(QWidget *parent, DWindowManagerHelper *helper)
    : QObject(nullptr)
    , m_parent(parent)
    , m_wmHelper(helper)
    , m_xcbMisc(XcbMisc::instance())
    , m_eventInter(new XEventMonitor("com.deepin.api.XEventMonitor", "/com/deepin/api/XEventMonitor", QDBusConnection::sessionBus()))
    , m_leaveMonitorInter(new XEventMonitor("com.deepin.api.XEventMonitor", "/com/deepin/api/XEventMonitor", QDBusConnection::sessionBus()))
    , m_dockInter(new DBusDock("com.deepin.dde.daemon.Dock", "/com/deepin/dde/daemon/Dock", QDBusConnection::sessionBus(), this))
    , m_displayInter(new DisplayInter("com.deepin.daemon.Display", "/com/deepin/daemon/Display", QDBusConnection::sessionBus(), this))
    , m_leaveTimer(new QTimer(this))
    , m_showAni(new QVariantAnimation(this))
    , m_hideAni(new QVariantAnimation(this))
    , m_fromScreen(qApp->primaryScreen()->name())
    , m_toScreen(qApp->primaryScreen()->name())
    , m_position(Dock::Position(m_dockInter->position()))
    , m_hideMode(Dock::HideMode(m_dockInter->hideMode()))
    , m_hideState(Dock::HideState(m_dockInter->hideState()))
    , m_displayMode(Dock::DisplayMode(m_dockInter->displayMode()))
    , m_screenRawHeight(m_displayInter->screenHeight())
    , m_screenRawWidth(m_displayInter->screenWidth())
    //    , m_dockVisible(false)
    , m_aniStart(false)
    , m_autoHide(true)
{
    qDebug() << "init dock screen: " << m_toScreen;
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
    m_leaveTimer->setInterval(10);
    m_leaveTimer->setSingleShot(true);

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
        //        const int dockSize = int(m_displayMode == DisplayMode::Fashion ? m_dockInter->windowSizeFashion() : m_dockInter->windowSizeEfficient());

        QRect rect = value.toRect();
        parent()->setFixedSize(rect.size());
        parent()->setGeometry(rect);

        //        switch (m_dockInter->position()) {
        //        case Position::Top:
        //        {
        //            parent()->panel()->setFixedHeight(dockSize);
        //            parent()->panel()->move(rect.x(),rect.y() + rect.height() - dockSize);
        //        }
        //            break;
        //        case Position::Bottom:
        //            parent()->panel()->move(0, 0);
        //            break;
        //        case Position::Left:
        //            break;
        //        case Position::Right:
        //            break;
        //        }
    });

    connect(m_hideAni, &QVariantAnimation::valueChanged, parent(), [ = ](QVariant value) {
        QRect rect = value.toRect();
        parent()->setFixedSize(rect.size());
        parent()->setGeometry(rect);
    });

    connect(m_leaveTimer, &QTimer::timeout, this, &MultiScreenWorker::updateGeometry);

    connect(m_showAni, &QVariantAnimation::finished, this, &MultiScreenWorker::showAniFinished);
    connect(m_hideAni, &QVariantAnimation::finished, this, &MultiScreenWorker::hideAniFinished);

    connect(m_dockInter, &DBusDock::PositionChanged, this, &MultiScreenWorker::onPositionChanged);
    connect(m_dockInter, &DBusDock::DisplayModeChanged, this, &MultiScreenWorker::onDisplayModeChanged);
    connect(m_dockInter, &DBusDock::HideModeChanged, this, &MultiScreenWorker::hideModeChanged, Qt::QueuedConnection);
    connect(m_dockInter, &DBusDock::HideStateChanged, this, &MultiScreenWorker::hideStateChanged);

    connect(m_eventInter, &XEventMonitor::CursorMove, this, &MultiScreenWorker::onRegionMonitorChanged);
    connect(m_leaveMonitorInter, &XEventMonitor::CursorMove, this, &MultiScreenWorker::onLeaveMonitorChanged);

    connect(this, &MultiScreenWorker::requestUpdateRegionMonitor, this, &MultiScreenWorker::onRequestUpdateRegionMonitor);
    connect(this, &MultiScreenWorker::requestUpdateFrontendGeometry, this, &MultiScreenWorker::onRequestUpdateFrontendGeometry);
    connect(this, &MultiScreenWorker::requestUpdatePosition, this, &MultiScreenWorker::onRequestUpdatePosition);
    connect(this, &MultiScreenWorker::requestNotifyWindowManager, this, &MultiScreenWorker::onRequestNotifyWindowManager);
    connect(this, &MultiScreenWorker::requestUpdateDragArea, this, &MultiScreenWorker::onRequestUpdateDragArea);
    connect(this, &MultiScreenWorker::monitorInfoChaged, this, &MultiScreenWorker::onMonitorInfoChaged);
}

void MultiScreenWorker::initShow()
{
    // 找到一个可以使用的主屏去停靠任务栏
    updateDockScreenName();

    if (m_hideMode == HideMode::KeepShowing)
        showAni(m_toScreen);
    else
        parent()->setGeometry(getDockHideGeometry(m_toScreen, m_position, m_displayMode));
}

void MultiScreenWorker::showAni(const QString &screen)
{
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    if (m_showAni->state() == QVariantAnimation::Running || m_aniStart)
        return;

    emit requestUpdateFrontendGeometry(dockRect(m_toScreen, m_hideMode));

    // 任务栏位置已经正确就不需要再重复一次动画了
    if (getDockShowGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode) == parent()->geometry()) {
        emit requestNotifyWindowManager();
        return;
    }

    updateDockScreenName(screen);

    if (m_hideAni->state() == QVariantAnimation::Running)
        m_hideAni->stop();
    //qDebug() << getDockHideGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode);
    //qDebug() << getDockShowGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode);
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
        //        emit requestUpdateFrontendGeometry(dockRect(m_toScreen, m_hideMode));
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
    });

    connect(ani2, &QVariantAnimation::valueChanged, this, [ = ](QVariant value) {
        parent()->setFixedSize(value.toRect().size());
        parent()->setGeometry(value.toRect());
    });

    // 如果更改了显示位置，在显示之前应该更新一下界面布局方向
    if (fromPos != toPos)
        connect(ani1, &QVariantAnimation::finished, this, [ = ] {
            //            updateDockVisible(false);
            //隐藏后需要通知界面更新布局方向
            emit requestUpdateLayout(fromScreen);
        });


    connect(group, &QVariantAnimation::finished, this, [ = ] {
        //        updateDockVisible(true);
        m_aniStart = false;

        //　结束之后需要根据确定需要再隐藏
        emit showAniFinished();
        emit requestUpdateFrontendGeometry(dockRect(m_toScreen, m_hideMode));
        emit requestNotifyWindowManager();
    });

    m_aniStart = true;
    group->start(QVariantAnimation::DeleteWhenStopped);
}

void MultiScreenWorker::updateDockScreenName(const QString &screenName)
{
    m_fromScreen = m_toScreen;
    m_toScreen = screenName;

    qDebug() << "update dock screen: " << screenName;

    emit requestUpdateLayout(screenName);
}

void MultiScreenWorker::updateDockScreenName()
{
    QList<Monitor *> monitorList = m_monitorInfo.keys();
    if (monitorList.size() == 2) {
        Monitor *primaryMonitor = monitorByName(m_monitorInfo, m_toScreen);
        if (!primaryMonitor->dockPosition().docked(position())) {
            foreach (auto monitor, monitorList) {
                if (monitor->name() != m_toScreen
                        && monitor->dockPosition().docked(position())) {
                    updateDockScreenName(monitor->name());
                }
            }
        }
    }
}

QSize MultiScreenWorker::contentSize(const QString &screenName)
{
    const int dockSize = int(m_displayMode == DisplayMode::Fashion ? m_dockInter->windowSizeFashion() : m_dockInter->windowSizeEfficient());
    switch (m_position) {
    case Top:
    case Bottom:
        if (m_displayMode == DisplayMode::Fashion)
            return QSize(screenByName(screenName)->geometry().width() -  2 * WINDOWMARGIN, dockSize);
        return QSize(screenByName(screenName)->geometry().width(), dockSize);
    case Left:
    case Right:
        if (m_displayMode == DisplayMode::Fashion)
            return QSize(dockSize, screenByName(screenName)->geometry().height() - 2 * WINDOWMARGIN);
        return QSize(dockSize, screenByName(screenName)->geometry().height());
    }
}

QRect MultiScreenWorker::dockRect(const QString &screenName, const HideMode &mode)
{
    if (mode == HideMode::KeepShowing)
        return getDockShowGeometry(screenName, m_position, m_displayMode);
    else
        return getDockHideGeometry(screenName, m_position, m_displayMode);
}

void MultiScreenWorker::handleLeaveEvent(QEvent *event)
{
    Q_UNUSED(event);

    if (m_hideMode == HideMode::SmartHide)
        return;

    m_leaveTimer->start();
#ifdef QT_DEBUG
    qDebug() << "start leave timer" << QCursor::pos();
#endif
}

void MultiScreenWorker::onAutoHideChanged(bool autoHide)
{
    m_autoHide = autoHide;
}

void MultiScreenWorker::updateDaemonDockSize(int dockSize)
{
    m_dockInter->setWindowSize(uint(dockSize));
    if (m_displayMode == DisplayMode::Fashion)
        m_dockInter->setWindowSizeFashion(uint(dockSize));
    else
        m_dockInter->setWindowSizeEfficient(uint(dockSize));
}

void MultiScreenWorker::onRegionMonitorChanged(int x, int y, const QString &key)
{
    if (m_registerKey != key)
        return;

    QString toScreen;
    QScreen *screen = Utils::screenAtByScaled(QPoint(x, y));
    if (!screen) {
        qDebug() << "cannot find the screen" << QPoint(x, y);
        return;
    }

    toScreen = screen->name();

    /**
     * 坐标位于当前屏幕边缘时,当做当前屏幕处理(防止鼠标移动到边缘时不唤醒任务栏)
     * 使用screenAtByScaled获取屏幕名时,实际上获取的不一定是当前屏幕
     * 举例:点(100,100)不在(0,0,100,100)的屏幕上
     */
    if (onScreenEdge(m_toScreen, QPoint(x, y))) {
        toScreen = m_toScreen;
        qDebug() << "on the current screen edge";
    }

    // 过滤重复坐标
    static QPoint lastPos(0, 0);
    if (lastPos == QPoint(x, y)) {
        qDebug() << "same point,should return";
        return;
    }
    lastPos = QPoint(x, y);

    qDebug() << x << y << m_toScreen;

    // 如果离开定时器已启动，这时候如果鼠标又在监视区域移动了，那就保持现状，不需要再做其他操作
    if (m_leaveTimer->isActive())
        m_leaveTimer->stop();

    //1 任务栏显示状态，但需要切换屏幕
    if (toScreen != m_toScreen) {
        Monitor *currentMonitor = monitorByName(m_monitorInfo, toScreen);
        if (!currentMonitor)
            return;

        // 检查边缘是否允许停靠
        if (currentMonitor->dockPosition().docked(static_cast<Position>(m_dockInter->position())))
            changeDockPosition(m_toScreen, screen->name()
                               , static_cast<Position>(m_dockInter->position())
                               , static_cast<Position>(m_dockInter->position()));
    } else {
        // 任务栏隐藏状态，但需要显示
        if (hideMode() == HideMode::KeepShowing)
            return;

        if (m_showAni->state() == QVariantAnimation::Running)
            return;

        const QRect boundRect = parent()->visibleRegion().boundingRect();
        if ((hideMode() == HideMode::KeepHidden || m_hideMode == HideMode::SmartHide)
                && (boundRect.isEmpty())) {
            showAni(m_toScreen);
        }
    }
}

void MultiScreenWorker::onLeaveMonitorChanged(int x, int y, const QString &key)
{
    // 这个函数是为了处理XEventMonitor信号发送不及时导致leaveEvent事件触发后启动定时器又被取消操作的问题
    if (m_leaveRegisterKey != key)
        return;

    // 智能隐藏模式不处理
    if (m_hideMode == HideMode::SmartHide)
        return;

    QPoint p(x, y);
    if (!contains(m_monitorRectList, p)) {
        // 唤起区域外部,属于离开事件发生区域
        if (!parent()->visibleRegion().boundingRect().size().isEmpty()) {
            m_leaveTimer->start();
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
    emit requestUpdateFrontendGeometry(dockRect(m_toScreen, m_hideMode));
    emit requestNotifyWindowManager();
    //    emit requestUpdateLayout(m_toScreen);
    emit requestUpdateDragArea();
}

void MultiScreenWorker::hideAniFinished()
{
    emit requestUpdateFrontendGeometry(dockRect(m_toScreen, m_hideMode));
    emit requestNotifyWindowManager();
    //    emit requestUpdateLayout(m_toScreen);
}

void MultiScreenWorker::onPositionChanged()
{
    const Position position = Dock::Position(m_dockInter->position());
    Position lastPos = m_position;
    if (m_position == position)
        return;
    m_position = position;

    // 更新鼠标拖拽样式
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

    qDebug() << "displat mode change:" << displayMode;

    m_displayMode = displayMode;

    DockItem::setDockDisplayMode(displayMode);
    qApp->setProperty(PROP_DISPLAY_MODE, QVariant::fromValue(displayMode));

    // 不显示的就不用处理,在显示时会再处理一遍的
    if (parent()->visibleRegion().boundingRect().isEmpty())
        return;

    parent()->setFixedSize(dockRect(m_toScreen, m_hideMode).size());
    parent()->move(dockRect(m_toScreen, m_hideMode).topLeft());

    emit displayModeChanegd();
    emit requestUpdateRegionMonitor();
    emit requestUpdateFrontendGeometry(dockRect(m_toScreen, m_hideMode));
    emit requestNotifyWindowManager();
}

void MultiScreenWorker::hideModeChanged()
{
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    HideMode hideMode = Dock::HideMode(m_dockInter->hideMode());

    if (m_hideMode == hideMode)
        return;

    m_hideMode = hideMode;

    emit windowHideModeChanged();
    emit requestUpdateFrontendGeometry(dockRect(m_toScreen, m_hideMode));
    emit requestNotifyWindowManager();
}

void MultiScreenWorker::hideStateChanged()
{
    const Dock::HideState state = Dock::HideState(m_dockInter->hideState());

    if (state == Dock::Unknown)
        return;

    m_hideState = state;

    if (m_hideMode == HideMode::SmartHide
            || m_hideMode == HideMode::KeepHidden) {
        if (m_hideState == HideState::Show)
            showAni(m_toScreen);
        else if (m_hideState == HideState::Hide)
            hideAni(m_toScreen);
    }

    emit windowVisibleChanged();
}

void MultiScreenWorker::onRequestUpdateRegionMonitor()
{
    if (!m_registerKey.isEmpty()) {
        bool ret1 = m_eventInter->UnregisterArea(m_registerKey);
        bool ret2 = m_leaveMonitorInter->UnregisterArea(m_leaveRegisterKey);
#ifdef QT_DEBUG
        qDebug() << "取消唤起区域监听:" << ret1;
        qDebug() << "取消离开区域监听:" << ret2;
#endif
    }

    const static int flags = Motion | Button | Key;
    const static int monitorHeight = 15;
    //    const int dockSize = int(displayMode() == DisplayMode::Fashion ? m_dockInter->windowSizeFashion() : m_dockInter->windowSizeEfficient());

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

        m_monitorRectList << rect;
#ifdef QT_DEBUG
        qDebug() << "监听区域：" << rect.x1 << rect.y1 << rect.x2 << rect.y2;
#endif
    }

    // 任务栏事件区域,用于模拟离开事件
    QList<MonitRect> fulleScreenMonitorList;
    foreach (Monitor *inter, m_monitorInfo.keys()) {
        MonitRect rect;
        rect.x1 = inter->x();
        rect.y1 = inter->y();
        rect.x2 = inter->x() + inter->w();
        rect.y2 = inter->y() + inter->h();

        fulleScreenMonitorList << rect;
#ifdef QT_DEBUG
        qDebug() << "全屏监听区域：" << rect.x1 << rect.y1 << rect.x2 << rect.y2;
#endif
    }

    // 离开事件不做处理的区域
    updateInterRect(m_monitorInfo.keys(), m_interRectList);

    m_registerKey = m_eventInter->RegisterAreas(m_monitorRectList, flags);
    m_leaveRegisterKey = m_leaveMonitorInter->RegisterAreas(fulleScreenMonitorList, flags);
}

void MultiScreenWorker::onRequestUpdateFrontendGeometry(const QRect &rect)
{
    //    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    // TODO　应该获取当前屏幕的缩放
    const qreal scale = parent()->devicePixelRatioF();
#ifdef QT_DEBUG
    //    qDebug() << rect;
#endif

    m_dockInter->SetFrontendWindowRect(rect.x() / scale, rect.y() / scale, rect.width() / scale, rect.height() / scale);
}

void MultiScreenWorker::onRequestNotifyWindowManager()
{
    //    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    updateWindowManagerDock();
}

void MultiScreenWorker::onRequestUpdatePosition(const Position &fromPos, const Position &toPos)
{
    //    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    // 切换位置默认切换到主屏，主屏不允许时再尝试其他屏幕
    updateDockScreenName();

    changeDockPosition(fromScreen(), toScreen(), fromPos, toPos);

    //    QWidget *widget = parent()->dragWidget();

}

void MultiScreenWorker::onRequestUpdateDragArea()
{
    parent()->resetDragWindow();
}

void MultiScreenWorker::onMonitorInfoChaged()
{
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    // 更新所在屏幕
    updateDockScreenName();
    // 更新任务栏大小
    parent()->setGeometry(dockRect(m_toScreen, m_hideMode));
    // 通知后端
    emit requestUpdateFrontendGeometry(dockRect(m_toScreen, m_hideMode));
    // 拖拽区域
    emit requestUpdateDragArea();
    // 监控区域
    emit requestUpdateRegionMonitor();
    // 通知窗管
    emit requestNotifyWindowManager();
}

void MultiScreenWorker::updateGeometry()
{
    Q_ASSERT(sender() == m_leaveTimer);

    if (!m_autoHide)
        return;

    if (contains(m_interRectList, scaledPos1(QCursor::pos())))
        return;

    switch (m_hideMode) {
    case HideMode::SmartHide:
    case HideMode::KeepHidden:
        hideAni(toScreen());
        break;
    case HideMode::KeepShowing:
        break;
    }
}

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
    QList<Monitor *>screens = map.keys();

    // 最多支持双屏,这里只计算双屏,单屏默认四边均可停靠任务栏
    if (screens.size() != 2)
        return;

    Monitor *s1 = screens.at(0);
    Monitor *s2 = screens.at(1);
    if (!s1 || !s2) {
        qFatal("shouldn't be here");
    }

    // 对齐
    bool isAligment = false;
    // 左右拼
    if (s1->bottom() > s2->top() && s1->top() < s2->bottom()) {
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
    }
    // 上下拼
    if (s1->right() > s2->left() && s1->left() < s2->right()) {
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
            qDebug ()<< inter->x() << inter->y() << inter->w() << inter->h();
            const int dockSize = int(displaymode == DisplayMode::Fashion ? m_dockInter->windowSizeFashion() : m_dockInter->windowSizeEfficient());
            //            const int margin = (displaymode == DisplayMode::Fashion ? WINDOWMARGIN : 0);

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
    qDebug() << rect;
#endif

    return rect;
}

QRect MultiScreenWorker::getDockHideGeometry(const QString &screenName, const Position &pos, const DisplayMode &displaymode)
{
    QRect rect;
    foreach (Monitor *inter, m_monitorInfo.keys()) {
        if (inter->name() == screenName) {
            qDebug ()<< inter->x() << inter->y() << inter->w() << inter->h();
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
    qDebug() << rect;
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

    //TODO 获取的位置有问题，需要优化
    const QRect rect = getDockShowGeometry(m_toScreen, m_position, m_displayMode);

    const QPoint &p = rawXPosition1(rect.topLeft());
    const QSize &s = rect.size();
    const QRect &primaryRawRect = rect;

    XcbMisc::Orientation orientation = XcbMisc::OrientationTop;
    uint strut = 0;
    uint strutStart = 0;
    uint strutEnd = 0;

    switch (m_dockInter->position()) {
    case Position::Top:
        orientation = XcbMisc::OrientationTop;
        strut = p.y() + s.height() * ratio;
        strutStart = p.x();
        strutEnd = qMin(qRound(p.x() + s.width() * ratio), primaryRawRect.right());
        break;
    case Position::Bottom:
        orientation = XcbMisc::OrientationBottom;
        strut = m_screenRawHeight - p.y();
        strutStart = p.x();
        strutEnd = qMin(qRound(p.x() + s.width() * ratio), primaryRawRect.right());
        break;
    case Position::Left:
        orientation = XcbMisc::OrientationLeft;
        strut = p.x() + s.width() * ratio;
        strutStart = p.y();
        strutEnd = qMin(qRound(p.y() + s.height() * ratio), primaryRawRect.bottom());
        break;
    case Position::Right:
        orientation = XcbMisc::OrientationRight;
        strut = m_screenRawWidth - p.x();
        strutStart = p.y();
        strutEnd = qMin(qRound(p.y() + s.height() * ratio), primaryRawRect.bottom());
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
    //        qDebug() << rect.x1 << rect.y1 << rect.x2 << rect.y2 << pos;
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

bool MultiScreenWorker::positionDocked(const QString &screenName, const Position &pos)
{
    bool ret = false;

    foreach (auto monitor, m_monitorInfo.keys()) {
        if (monitor->name() == screenName) {
            //检查此屏幕相对位置

        }
    }
    return ret;
}
