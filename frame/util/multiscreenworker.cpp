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

#include <QWidget>
#include <QScreen>
#include <QEvent>
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
    , m_dockVisible(false)
{
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
    m_leaveTimer->setInterval(100);
    m_leaveTimer->setSingleShot(true);

    //　设置应用角色为任务栏
    m_xcbMisc->set_window_type(parent()->winId(), XcbMisc::Dock);

    //　初始化动画信息
    m_showAni->setEasingCurve(QEasingCurve::InOutCubic);
    m_hideAni->setEasingCurve(QEasingCurve::InOutCubic);

    const bool composite = m_wmHelper->hasComposite();

#ifndef DISABLE_SHOW_ANIMATION
    const int duration = composite ? 300 : 0;
#else
    const int duration = 0;
#endif

    m_showAni->setDuration(duration);
    m_hideAni->setDuration(duration);
}

void MultiScreenWorker::initConnection()
{
    connect(m_displayInter, &DisplayInter::MonitorsChanged, this, &MultiScreenWorker::onMonitorListChanged);

    connect(m_showAni, &QVariantAnimation::valueChanged, parent(), [ = ](QVariant value) {
        QRect rect = value.toRect();
#ifdef QT_DEBUG
        //        qDebug() << rect;
#endif
        parent()->setFixedSize(rect.size());
        parent()->setGeometry(rect);
    });

    connect(m_hideAni, &QVariantAnimation::valueChanged, parent(), [ = ](QVariant value) {
        QRect rect = value.toRect();
#ifdef QT_DEBUG
        //        qDebug() << rect;
#endif
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

    connect(this, &MultiScreenWorker::requestUpdateRegionMonitor, this, &MultiScreenWorker::onRequestUpdateRegionMonitor);
    connect(this, &MultiScreenWorker::requestUpdateFrontendGeometry, this, &MultiScreenWorker::onRequestUpdateFrontendGeometry);
    connect(this, &MultiScreenWorker::requestUpdatePosition, this, &MultiScreenWorker::onRequestUpdatePosition);
    connect(this, &MultiScreenWorker::requestNotifyWindowManager, this, &MultiScreenWorker::onRequestNotifyWindowManager);
}

void MultiScreenWorker::initShow()
{
    emit requestUpdateLayout(m_toScreen);

    if (m_hideMode == HideMode::KeepShowing) {
        showAni(m_toScreen);
    } else {
        parent()->setGeometry(getDockHideGeometry(toScreen(), position(), displayMode()));
    }
}

void MultiScreenWorker::showAni(const QString &screen)
{
    if (dockVisible())
        return;

    if (m_showAni->state() == QVariantAnimation::Running)
        return;

    if (m_hideAni->state() == QVariantAnimation::Running)
        emit m_hideAni->finished();

    m_showAni->setStartValue(getDockHideGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode));
    m_showAni->setEndValue(getDockShowGeometry(screen, static_cast<Position>(m_dockInter->position()), m_displayMode));
    m_showAni->start();
}

void MultiScreenWorker::hideAni(const QString &screen)
{
    if (!dockVisible())
        return;

    if (m_hideAni->state() == QVariantAnimation::Running)
        return;

    if (m_showAni->state() == QVariantAnimation::Running)
        emit m_showAni->finished();

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
    const int duration = composite ? 300 : 0;
#else
    const int duration = 0;
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
            updateDockVisible(false);
            emit requestUpdateLayout(fromScreen);
        }); //隐藏后需要通知界面更新布局方向
    // 通知窗管更新任务栏区域
    connect(group, &QVariantAnimation::finished, this, [ = ] {
        updateDockVisible(true);
        emit requestNotifyWindowManager();
    });
    //　结束之后需要根据确定需要再隐藏
    connect(group, &QVariantAnimation::finished, this, &MultiScreenWorker::showAniFinished);

    group->start(QVariantAnimation::DeleteWhenStopped);
}

void MultiScreenWorker::updateDockScreenName(const QString &screenName)
{
    m_fromScreen = m_toScreen;
    m_toScreen = screenName;

    emit dockScreenNameChanged(m_toScreen);
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

QRect MultiScreenWorker::dockRect(const QString &screenName)
{
    // TODO 根据显示模式，回复不同的区域
    return getDockShowGeometry(screenName, m_position, m_displayMode);
}

void MultiScreenWorker::handleLeaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    if (m_hideAni->state() == QVariantAnimation::Running)
        return;

    // 一直隐藏，在鼠标离开任务栏时，需要自动隐藏
    if (m_hideMode == HideMode::KeepHidden) {
        m_leaveTimer->start();
    }
}

void MultiScreenWorker::onRegionMonitorChanged(int x, int y, const QString &key)
{
    if (m_registerKey != key)
        return;

    QScreen *screen = Utils::screenAtByScaled(QPoint(x, y));
    if (!screen)
        return;

    // 过滤重复坐标
    static QPoint lastPos(0, 0);
    if (lastPos == QPoint(x, y))
        return;
    lastPos = QPoint(x, y);

    // 如果离开定时器已启动，这时候如果鼠标又在监视区域移动了，那就保持现状，不需要再做可能还要隐藏任务栏的操作
    if (m_leaveTimer->isActive())
        m_leaveTimer->stop();

    //    qDebug() << x << y << m_toScreen;

    //1 任务栏显示状态，但需要切换屏幕
    if (screen->name() != m_toScreen) {
        changeDockPosition(m_toScreen, screen->name()
                           , static_cast<Position>(m_dockInter->position())
                           , static_cast<Position>(m_dockInter->position()));

        //        updateDockScreenName(screen->name());
    } else {
        //2 任务栏当前显示状态，但需要隐藏
        // TODO　这部分放大leaveEvent中处理
        //        qDebug() << hideMode() << dockVisible();
        //3 任务栏隐藏状态，但需要显示
        if (!dockVisible())
        {
            if (hideMode() == HideMode::KeepShowing
                    || hideMode() == HideMode::KeepHidden) {
                showAni(toScreen());
            }
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
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;
    //    emit requestUpdateFrontendGeometry(getDockShowGeometry(m_dockScreenName,m_position,m_displayMode));//后面再加
    emit requestNotifyWindowManager();
    emit requestUpdateLayout(m_toScreen);
    updateDockVisible(true);

    switch (m_hideMode) {
    case HideMode::SmartHide:
        break;
    case HideMode::KeepHidden:
        //        hideAni(toScreen());
        break;
    case HideMode::KeepShowing:
        break;
    }
}

void MultiScreenWorker::hideAniFinished()
{
    //    emit requestUpdateFrontendGeometry(parent()->geometry());//后面再加
    emit requestNotifyWindowManager();
    emit requestUpdateLayout(m_toScreen);
    updateDockVisible(false);

    switch (m_hideMode) {
    case HideMode::SmartHide:
        break;
    case HideMode::KeepHidden:
        break;
    case HideMode::KeepShowing:
        //        showAni();
        break;
    }
}

void MultiScreenWorker::onPositionChanged()
{
    const Position position = Dock::Position(m_dockInter->position());
    Position lastPos = m_position;
    if (m_position == position)
        return;
    m_position = position;

    DockItem::setDockPosition(m_position);

    emit requestUpdatePosition(lastPos, m_position);
    emit requestUpdateRegionMonitor();
}

void MultiScreenWorker::onDisplayModeChanged()
{
    DisplayMode displayMode = Dock::DisplayMode(m_dockInter->displayMode());

    if (displayMode == m_displayMode)
        return;

    m_displayMode = displayMode;

    DockItem::setDockDisplayMode(m_displayMode);

    emit displayModeChanegd();
    emit requestUpdateRegionMonitor();
}

void MultiScreenWorker::hideModeChanged()
{
    HideMode hideMode = Dock::HideMode(m_dockInter->hideMode());

    if (m_hideMode == hideMode)
        return;

    m_hideMode = hideMode;

    emit windowHideModeChanged();
}

void MultiScreenWorker::hideStateChanged()
{
    const Dock::HideState state = Dock::HideState(m_dockInter->hideState());

    if (state == Dock::Unknown)
        return;

    m_hideState = state;

    emit windowVisibleChanged();
}

void MultiScreenWorker::onRequestUpdateRegionMonitor()
{
    if (!m_registerKey.isEmpty()) {
        bool ret = m_eventInter->UnregisterArea(m_registerKey);
#ifdef QT_DEBUG
        qDebug() << "取消区域监听:" << ret;
#endif
    }

    const static int flags = Motion | Button | Key;
    const static int monitorHeight = 10;

    QList<MonitRect> monitList;
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

        monitList << rect;
#ifdef QT_DEBUG
        qDebug() << "监听区域：" << rect.x1 << rect.y1 << rect.x2 << rect.y2;
#endif
    }
    m_registerKey = m_eventInter->RegisterAreas(monitList, flags);
}

void MultiScreenWorker::onRequestUpdateFrontendGeometry(const QRect &rect)
{
    // TODO　应该获取当前屏幕的缩放
    const qreal scale = parent()->devicePixelRatioF();
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
    //TODO　切换位置默认切换到主屏，或者这里应该封装一个primaryScreen函数
    changeDockPosition(m_toScreen, qApp->primaryScreen()->name(), fromPos, toPos);
}

void MultiScreenWorker::updateGeometry()
{
    qDebug() << __PRETTY_FUNCTION__ << __LINE__ << __FILE__;

    switch (hideMode()) {
    case HideMode::SmartHide:
        break;
    case HideMode::KeepHidden:
        hideAni(toScreen());
        break;
    case HideMode::KeepShowing:
        showAni(toScreen());
        break;
    }
}

QWidget *MultiScreenWorker::parent()
{
    return m_parent;
}

QRect MultiScreenWorker::getDockShowGeometry(const QString &screenName, const Position &pos, const DisplayMode &displaymode)
{
    QRect rect;
    foreach (Monitor *inter, m_monitorInfo.keys()) {
        if (inter->name() == screenName) {
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
                rect.setHeight(inter->w() - 2 * WINDOWMARGIN);
            }
                break;
            case Right: {
                rect.setX(inter->x() + inter->w() - WINDOWMARGIN - dockSize);
                rect.setY(inter->y() + WINDOWMARGIN);
                rect.setWidth(dockSize);
                rect.setHeight(inter->w() - 2 * WINDOWMARGIN);
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
                rect.setHeight(inter->w() - 2 * margin);
            }
                break;
            case Right: {
                rect.setX(inter->x() + inter->w());
                rect.setY(inter->y() + margin);
                rect.setWidth(0);
                rect.setHeight(inter->w() - 2 * margin);
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

    //    qDebug() << strut << strutStart << strutEnd;
    m_xcbMisc->set_strut_partial(parent()->winId(), orientation, strut + WINDOWMARGIN * ratio, strutStart, strutEnd);
}

QScreen *MultiScreenWorker::screenByName(const QString &screenName)
{
    foreach (QScreen *screen, qApp->screens()) {
        if (screen->name() == screenName)
            return screen;
    }
    return nullptr;
}
