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

#include "mainwindow.h"
#include "panel/mainpanelcontrol.h"
#include "controller/dockitemmanager.h"
#include "util/utils.h"
#include "util/docksettings.h"

#include <DStyle>
#include <DPlatformWindowHandle>

#include <QDebug>
#include <QEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QX11Info>
#include <qpa/qplatformwindow.h>

#include <X11/X.h>
#include <X11/Xutil.h>

#define SNI_WATCHER_SERVICE "org.kde.StatusNotifierWatcher"
#define SNI_WATCHER_PATH "/StatusNotifierWatcher"

#define MAINWINDOW_MAX_SIZE       DOCK_MAX_SIZE
#define MAINWINDOW_MIN_SIZE       (40)
#define DRAG_AREA_SIZE (5)

using org::kde::StatusNotifierWatcher;

const QPoint rawXPosition(const QPoint &scaledPos)
{
    QScreen const *screen = Utils::screenAtByScaled(scaledPos);

    return screen ? screen->geometry().topLeft() +
                    (scaledPos - screen->geometry().topLeft()) *
                    screen->devicePixelRatio()
                  : scaledPos;
}

const QPoint scaledPos(const QPoint &rawXPos)
{
    QScreen const *screen = Utils::screenAt(rawXPos);

    return screen
            ? screen->geometry().topLeft() +
              (rawXPos - screen->geometry().topLeft()) / screen->devicePixelRatio()
            : rawXPos;
}

MainWindow::MainWindow(QWidget *parent)
    : DBlurEffectWidget(parent)
    , m_launched(false)
    , m_mainPanel(new MainPanelControl(this))
    , m_platformWindowHandle(this)
    , m_wmHelper(DWindowManagerHelper::instance())
    , m_multiScreenWorker(new MultiScreenWorker(this,m_wmHelper))
    , m_eventInter(new XEventMonitor("com.deepin.api.XEventMonitor", "/com/deepin/api/XEventMonitor", QDBusConnection::sessionBus()))
    , m_positionUpdateTimer(new QTimer(this))
    , m_expandDelayTimer(new QTimer(this))
    , m_leaveDelayTimer(new QTimer(this))
    , m_shadowMaskOptimizeTimer(new QTimer(this))
    , m_panelShowAni(new QVariantAnimation(this))
    , m_panelHideAni(new QVariantAnimation(this))
    , m_showAni(new QPropertyAnimation(this,"geometry"))
    , m_hideAni(new QPropertyAnimation(this,"geometry"))
    //    , m_xcbMisc(XcbMisc::instance())
    , m_dbusDaemonInterface(QDBusConnection::sessionBus().interface())
    , m_sniWatcher(new StatusNotifierWatcher(SNI_WATCHER_SERVICE, SNI_WATCHER_PATH, QDBusConnection::sessionBus(), this))
    , m_dragWidget(new DragWidget(this))
    //    , m_mouseCauseDock(false)
{
    setAccessibleName("mainwindow");
    m_mainPanel->setAccessibleName("mainpanel");
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setAcceptDrops(true);

    DPlatformWindowHandle::enableDXcbForWindow(this, true);
    m_platformWindowHandle.setEnableBlurWindow(true);
    m_platformWindowHandle.setTranslucentBackground(true);
    m_platformWindowHandle.setWindowRadius(0);
    m_platformWindowHandle.setShadowOffset(QPoint(0, 5));
    m_platformWindowHandle.setShadowColor(QColor(0, 0, 0, 0.3 * 255));

    m_settings = &DockSettings::Instance();
    //    m_xcbMisc->set_window_type(winId(), XcbMisc::Dock);
    //    m_size = m_settings->m_mainWindowSize;
    m_mainPanel->setDisplayMode(m_settings->displayMode());
    initSNIHost();
    initComponents();
    initConnections();

    resetDragWindow();

    m_mainPanel->setDelegate(this);
    for (auto item : DockItemManager::instance()->itemList())
        m_mainPanel->insertItem(-1, item);

    m_dragWidget->setMouseTracking(true);
    m_dragWidget->setFocusPolicy(Qt::NoFocus);

    if ((Top == m_multiScreenWorker->position()) || (Bottom == m_multiScreenWorker->position())) {
        m_dragWidget->setCursor(Qt::SizeVerCursor);
    } else {
        m_dragWidget->setCursor(Qt::SizeHorCursor);
    }

    connect(m_multiScreenWorker, &MultiScreenWorker::displayModeChanegd, this, [=]{
        DisplayMode mode = m_multiScreenWorker->displayMode();
        m_mainPanel->setDisplayMode(mode);
    });

    connect(m_multiScreenWorker, &MultiScreenWorker::displayModeChanegd, m_shadowMaskOptimizeTimer, static_cast<void (QTimer::*)()>(&QTimer::start));

    //　通知窗管
    connect(m_multiScreenWorker, &MultiScreenWorker::requestUpdateLayout, this,[=](const QString &screenName){
        // FIXME: 这里有个很奇怪的问题，明明是左边屏幕的左边，偏偏就是显示右边屏幕的左边去，未找到原因
        // FIXME: 避免连续重复大量调用setFixedSize或setGeometry有一定效果)
        // QWidget::setFixedSize(m_multiScreenWorker->dockRect(screenName, m_multiScreenWorker->hideMode()).size());
        // QWidget::move(m_multiScreenWorker->dockRect(screenName, m_multiScreenWorker->hideMode()).topLeft());

        m_mainPanel->setFixedSize(m_multiScreenWorker->dockRect(screenName,HideMode::KeepShowing).size());
        m_mainPanel->move(0,0);
        m_mainPanel->setDisplayMode(m_multiScreenWorker->displayMode());
        m_mainPanel->setPositonValue(m_multiScreenWorker->position());
        m_mainPanel->update();
    });

    //　通知窗管任务栏大小时顺便更新拖拽区域
    connect(m_multiScreenWorker, &MultiScreenWorker::requestUpdateDragArea, this, &MainWindow::resetDragWindow);
}

MainWindow::~MainWindow()
{
    //    delete m_xcbMisc;
}

void MainWindow::launch()
{
    setVisible(false);
    QTimer::singleShot(400, this, [&] {
        m_launched = true;
        qApp->processEvents();
        initShow();
    });
}

void MainWindow::initShow()
{
    setVisible(true);

    m_multiScreenWorker->initShow();

    m_shadowMaskOptimizeTimer->start();
}

void MainWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);

    //    connect(qGuiApp, &QGuiApplication::primaryScreenChanged,
    //    windowHandle(), [this](QScreen * new_screen) {
    //        QScreen *old_screen = windowHandle()->screen();
    //        windowHandle()->setScreen(new_screen);
    //        // 屏幕变化后可能导致控件缩放比变化，此时应该重设控件位置大小
    //        // 比如：窗口大小为 100 x 100, 显示在缩放比为 1.0 的屏幕上，此时窗口的真实大小 = 100x100
    //        // 随后窗口被移动到了缩放比为 2.0 的屏幕上，应该将真实大小改为 200x200。另外，只能使用
    //        // QPlatformWindow直接设置大小来绕过QWidget和QWindow对新旧geometry的比较。
    //        const qreal scale = devicePixelRatioF();
    //        const QPoint screenPos = new_screen->geometry().topLeft();
    //        const QPoint posInScreen = this->pos() - old_screen->geometry().topLeft();
    //        const QPoint pos = screenPos + posInScreen * scale;
    //        const QSize size = this->size() * scale;

    //        windowHandle()->handle()->setGeometry(QRect(pos, size));
    //    }, Qt::UniqueConnection);

    //    windowHandle()->setScreen(qGuiApp->primaryScreen());
}

void MainWindow::mousePressEvent(QMouseEvent *e)
{
    e->ignore();
    if (e->button() == Qt::RightButton && m_settings->m_menuVisible) {
        m_settings->showDockSettingsMenu();
        return;
    }
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
#ifdef QT_DEBUG
    case Qt::Key_Escape:        qApp->quit();       break;
#endif
    default:;
    }
}

void MainWindow::enterEvent(QEvent *e)
{
    QWidget::enterEvent(e);

    if (QApplication::overrideCursor() && QApplication::overrideCursor()->shape() != Qt::ArrowCursor)
        QApplication::restoreOverrideCursor();
}

void MainWindow::mouseMoveEvent(QMouseEvent *e)
{
    //重写mouseMoveEvent 解决bug12866  leaveEvent事件失效
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    //    qDebug() << event->pos();
}

void MainWindow::leaveEvent(QEvent *e)
{
    QWidget::leaveEvent(e);
    return m_multiScreenWorker->handleLeaveEvent(e);

    //    if (m_panelHideAni->state() == QPropertyAnimation::Running)
    //        return;

    //    m_expandDelayTimer->stop();
    //    m_leaveDelayTimer->start();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    QWidget::dragEnterEvent(e);

    if (m_settings->hideState() != Show) {
        m_expandDelayTimer->start();
    }
}

void MainWindow::initSNIHost()
{
    // registor dock as SNI Host on dbus
    QDBusConnection dbusConn = QDBusConnection::sessionBus();
    m_sniHostService = QString("org.kde.StatusNotifierHost-") + QString::number(qApp->applicationPid());
    dbusConn.registerService(m_sniHostService);
    dbusConn.registerObject("/StatusNotifierHost", this);

    if (m_sniWatcher->isValid()) {
        m_sniWatcher->RegisterStatusNotifierHost(m_sniHostService);
    } else {
        qDebug() << SNI_WATCHER_SERVICE << "SNI watcher daemon is not exist for now!";
    }
}

void MainWindow::initComponents()
{
    m_positionUpdateTimer->setSingleShot(true);
    m_positionUpdateTimer->setInterval(20);
    m_positionUpdateTimer->start();

    m_expandDelayTimer->setSingleShot(true);
    m_expandDelayTimer->setInterval(m_settings->expandTimeout());

    m_leaveDelayTimer->setSingleShot(true);
    m_leaveDelayTimer->setInterval(m_settings->narrowTimeout());

    m_shadowMaskOptimizeTimer->setSingleShot(true);
    m_shadowMaskOptimizeTimer->setInterval(100);

    m_panelShowAni->setEasingCurve(QEasingCurve::InOutCubic);
    m_panelHideAni->setEasingCurve(QEasingCurve::InOutCubic);
    m_showAni->setEasingCurve(QEasingCurve::InOutCubic);
    m_hideAni->setEasingCurve(QEasingCurve::InOutCubic);

    QTimer::singleShot(1, this, &MainWindow::compositeChanged);

    themeTypeChanged(DGuiApplicationHelper::instance()->themeType());
}

void MainWindow::compositeChanged()
{
    const bool composite = m_wmHelper->hasComposite();
    setComposite(composite);

    // NOTE(justforlxz): On the sw platform, there is an unstable
    // display position error, disable animation solution
#ifndef DISABLE_SHOW_ANIMATION
    const int duration = composite ? 300 : 0;
#else
    const int duration = 0;
#endif

    m_panelHideAni->setDuration(duration);
    m_panelShowAni->setDuration(duration);
    m_showAni->setDuration(duration);
    m_hideAni->setDuration(duration);

    m_shadowMaskOptimizeTimer->start();
}

//void MainWindow::internalMove(const QPoint &p)
//{
//    return;
//    const bool isHide = m_settings->hideState() == HideState::Hide && !testAttribute(Qt::WA_UnderMouse);
//    const bool pos_adjust = m_settings->hideMode() != HideMode::KeepShowing &&
//            isHide &&
//            m_panelShowAni->state() == QVariantAnimation::Stopped;
//    if (!pos_adjust) {
//        m_mainPanel->move(0, 0);
//        return QWidget::move(p);
//    }
//}

void MainWindow::initConnections()
{
    connect(m_settings, &DockSettings::dataChanged, m_positionUpdateTimer, static_cast<void (QTimer::*)()>(&QTimer::start));
    //    connect(m_settings, &DockSettings::positionChanged, this, &MainWindow::positionChanged);
    connect(m_settings, &DockSettings::autoHideChanged, m_leaveDelayTimer, static_cast<void (QTimer::*)()>(&QTimer::start));
    connect(m_settings, &DockSettings::windowGeometryChanged, this, &MainWindow::updateGeometry, Qt::DirectConnection);
    connect(m_settings, &DockSettings::trayCountChanged, this, &MainWindow::getTrayVisableItemCount, Qt::DirectConnection);
    //    connect(m_settings, &DockSettings::windowHideModeChanged, this, &MainWindow::setStrutPartial, Qt::QueuedConnection);
    //    connect(m_settings, &DockSettings::windowHideModeChanged, [this] { resetPanelEnvironment(); });
    connect(m_settings, &DockSettings::windowHideModeChanged, m_leaveDelayTimer, static_cast<void (QTimer::*)()>(&QTimer::start));
    //    connect(m_settings, &DockSettings::windowVisibleChanged, this, &MainWindow::updatePanelVisible, Qt::QueuedConnection);
    connect(m_settings, &DockSettings::displayModeChanegd, m_positionUpdateTimer, static_cast<void (QTimer::*)()>(&QTimer::start));
    connect(&DockSettings::Instance(), &DockSettings::opacityChanged, this, &MainWindow::setMaskAlpha);
    connect(m_settings, &DockSettings::displayModeChanegd, this, &MainWindow::updateDisplayMode, Qt::QueuedConnection);

    //    connect(m_positionUpdateTimer, &QTimer::timeout, this, &MainWindow::updatePosition, Qt::QueuedConnection);
    //    connect(m_expandDelayTimer, &QTimer::timeout, this, &MainWindow::expand, Qt::QueuedConnection);
    //    connect(m_leaveDelayTimer, &QTimer::timeout, this, &MainWindow::updatePanelVisible, Qt::QueuedConnection);
    connect(m_shadowMaskOptimizeTimer, &QTimer::timeout, this, &MainWindow::adjustShadowMask, Qt::QueuedConnection);

    connect(m_panelHideAni, &QPropertyAnimation::finished, m_shadowMaskOptimizeTimer, static_cast<void (QTimer::*)()>(&QTimer::start));
    connect(m_panelShowAni, &QPropertyAnimation::finished, m_shadowMaskOptimizeTimer, static_cast<void (QTimer::*)()>(&QTimer::start));
    connect(m_panelHideAni, &QPropertyAnimation::finished, this, &MainWindow::panelGeometryChanged);
    connect(m_panelShowAni, &QPropertyAnimation::finished, this, &MainWindow::panelGeometryChanged);

    connect(m_wmHelper, &DWindowManagerHelper::hasCompositeChanged, this, &MainWindow::compositeChanged, Qt::QueuedConnection);
    connect(&m_platformWindowHandle, &DPlatformWindowHandle::frameMarginsChanged, m_shadowMaskOptimizeTimer, static_cast<void (QTimer::*)()>(&QTimer::start));

    connect(m_dbusDaemonInterface, &QDBusConnectionInterface::serviceOwnerChanged, this, &MainWindow::onDbusNameOwnerChanged);

    connect(DockItemManager::instance(), &DockItemManager::itemInserted, m_mainPanel, &MainPanelControl::insertItem, Qt::DirectConnection);
    connect(DockItemManager::instance(), &DockItemManager::itemRemoved, m_mainPanel, &MainPanelControl::removeItem, Qt::DirectConnection);
    connect(DockItemManager::instance(), &DockItemManager::itemUpdated, m_mainPanel, &MainPanelControl::itemUpdated, Qt::DirectConnection);
    //    connect(DockItemManager::instance(), &DockItemManager::requestRefershWindowVisible, this, &MainWindow::updatePanelVisible, Qt::QueuedConnection);
    connect(DockItemManager::instance(), &DockItemManager::requestWindowAutoHide, m_settings, &DockSettings::setAutoHide);
    connect(m_mainPanel, &MainPanelControl::itemMoved, DockItemManager::instance(), &DockItemManager::itemMoved, Qt::DirectConnection);
    connect(m_mainPanel, &MainPanelControl::itemAdded, DockItemManager::instance(), &DockItemManager::itemAdded, Qt::DirectConnection);
    connect(m_dragWidget, &DragWidget::dragPointOffset, this, &MainWindow::onMainWindowSizeChanged);
    connect(m_dragWidget, &DragWidget::dragFinished, this, &MainWindow::onDragFinished);
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &MainWindow::themeTypeChanged);
    //    connect(m_eventInter, &XEventMonitor::CursorMove, this, &MainWindow::onRegionMonitorChanged);

    connect(m_settings, &DockSettings::autoHideChanged, m_multiScreenWorker, &MultiScreenWorker::onAutoHideChanged);
}

//const QPoint MainWindow::x11GetWindowPos()
//{
//    const auto disp = QX11Info::display();

//    unsigned int unused;
//    int x;
//    int y;
//    Window unused_window;

//    XGetGeometry(disp, winId(), &unused_window, &x, &y, &unused, &unused, &unused, &unused);
//    XFlush(disp);

//    return QPoint(x, y);
//}

//void MainWindow::x11MoveWindow(const int x, const int y)
//{
//    const auto disp = QX11Info::display();

//    XMoveWindow(disp, winId(), x, y);
//    XFlush(disp);
//}

//void MainWindow::x11MoveResizeWindow(const int x, const int y, const int w, const int h)
//{
//    const auto disp = QX11Info::display();

//    XMoveResizeWindow(disp, winId(), x, y, w, h);
//    XFlush(disp);
//}

void MainWindow::getTrayVisableItemCount()
{
    m_mainPanel->getTrayVisableItemCount();
}

void MainWindow::setGeometry(const QRect &rect)
{
    this->windowHandle()->setGeometry(rect);
    //FIX: 切换屏幕显示模式,重置任务栏大小时,不生效
    //    QWidget::setGeometry(rect);

    qDebug() << m_mainPanel->geometry() << this->geometry();
}

void MainWindow::adjustShadowMask()
{
    if (!m_launched)
        return;

    if (m_shadowMaskOptimizeTimer->isActive())
        return;

    const bool composite = m_wmHelper->hasComposite();
    const bool isFasion = m_settings->displayMode() == Fashion;

    DStyleHelper dstyle(style());
    const int radius = dstyle.pixelMetric(DStyle::PM_TopLevelWindowRadius);

    m_platformWindowHandle.setWindowRadius(composite && isFasion ? radius : 0);
}

void MainWindow::onDbusNameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    Q_UNUSED(oldOwner);

    if (name == SNI_WATCHER_SERVICE && !newOwner.isEmpty()) {
        qDebug() << SNI_WATCHER_SERVICE << "SNI watcher daemon started, register dock to watcher as SNI Host";
        m_sniWatcher->RegisterStatusNotifierHost(m_sniHostService);
    }
}

void MainWindow::setEffectEnabled(const bool enabled)
{
    setMaskColor(AutoColor);

    setMaskAlpha(DockSettings::Instance().Opacity());

    m_platformWindowHandle.setBorderWidth(enabled ? 1 : 0);
}

void MainWindow::setComposite(const bool hasComposite)
{
    setEffectEnabled(hasComposite);
}

//void MainWindow::X11MoveResizeWindow(const int x, const int y, const int w, const int h)
//{
//    const auto disp = QX11Info::display();

//    XMoveResizeWindow(disp, winId(), x, y, w, h);
//    XFlush(disp);
//}

bool MainWindow::appIsOnDock(const QString &appDesktop)
{
    return DockItemManager::instance()->appIsOnDock(appDesktop);
}

void MainWindow::resetDragWindow()
{
    switch (m_multiScreenWorker->position()) {
    case Dock::Top:
        m_dragWidget->setGeometry(0, height() - DRAG_AREA_SIZE, width(), DRAG_AREA_SIZE);
        break;
    case Dock::Bottom:
        m_dragWidget->setGeometry(0, 0, width(), DRAG_AREA_SIZE);
        break;
    case Dock::Left:
        m_dragWidget->setGeometry(width() - DRAG_AREA_SIZE, 0, DRAG_AREA_SIZE, height());
        break;
    case Dock::Right:
        m_dragWidget->setGeometry(0, 0, DRAG_AREA_SIZE, height());
        break;
    }

    if (m_dockSize == 0)
        m_dockSize = m_multiScreenWorker->dockRect(m_multiScreenWorker->toScreen(),m_multiScreenWorker->hideMode()).height();

    // 通知窗管和后端更新数据
    m_multiScreenWorker->updateDaemonDockSize(m_dockSize);
    m_multiScreenWorker->requestNotifyWindowManager();

    if ((Top == m_multiScreenWorker->position()) || (Bottom == m_multiScreenWorker->position())) {
        m_dragWidget->setCursor(Qt::SizeVerCursor);
    } else {
        m_dragWidget->setCursor(Qt::SizeHorCursor);
    }

}

void MainWindow::updateDisplayMode()
{
    m_mainPanel->setDisplayMode(m_settings->displayMode());
    adjustShadowMask();
}

void MainWindow::onMainWindowSizeChanged(QPoint offset)
{
    const QRect &rect = m_multiScreenWorker->dockRect(m_multiScreenWorker->toScreen(),m_multiScreenWorker->hideMode());

    QRect newRect;
    switch(m_multiScreenWorker->position())
    {
    case Top:
    {
        newRect.setX(rect.x());
        newRect.setY(rect.y());
        newRect.setWidth(rect.width());
        newRect.setHeight(qBound(MAINWINDOW_MIN_SIZE, rect.height() + offset.y(), MAINWINDOW_MAX_SIZE));

        m_dockSize = newRect.height();
    }
        break;
    case Bottom:
    {
        newRect.setX(rect.x());
        newRect.setY(rect.y() + rect.height() - qBound(MAINWINDOW_MIN_SIZE, rect.height() - offset.y(), MAINWINDOW_MAX_SIZE));
        newRect.setWidth(rect.width());
        newRect.setHeight(qBound(MAINWINDOW_MIN_SIZE, rect.height() - offset.y(), MAINWINDOW_MAX_SIZE));

        m_dockSize = newRect.height();
    }
        break;
    case Left:
    {
        newRect.setX(rect.x());
        newRect.setY(rect.y());
        newRect.setWidth(qBound(MAINWINDOW_MIN_SIZE, rect.width() + offset.x(), MAINWINDOW_MAX_SIZE));
        newRect.setHeight(rect.height());

        m_dockSize = newRect.width();
    }
        break;
    case Right:
    {
        newRect.setX(rect.x() + rect.width() - qBound(MAINWINDOW_MIN_SIZE, rect.width() - offset.x(), MAINWINDOW_MAX_SIZE));
        newRect.setY(rect.y());
        newRect.setWidth(qBound(MAINWINDOW_MIN_SIZE, rect.width() - offset.x(), MAINWINDOW_MAX_SIZE));
        newRect.setHeight(rect.height());

        m_dockSize = newRect.width();
    }
        break;
    }

    // 更新界面大小
    qDebug() << newRect;
    m_mainPanel->setFixedSize(newRect.size());
    setFixedSize(newRect.size());
    move(newRect.topLeft());
}

void MainWindow::onDragFinished()
{
    resetDragWindow();
}

void MainWindow::themeTypeChanged(DGuiApplicationHelper::ColorType themeType)
{
    if (m_wmHelper->hasComposite()) {

        if (themeType == DGuiApplicationHelper::DarkType)
            m_platformWindowHandle.setBorderColor(QColor(0, 0, 0, 255 * 0.3));
        else
            m_platformWindowHandle.setBorderColor(QColor(QColor::Invalid));
    }
}


#include "mainwindow.moc"
