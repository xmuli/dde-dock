#ifndef MENUWORKER_H
#define MENUWORKER_H
#include <QObject>

#include "constants.h"

#include <com_deepin_dde_daemon_dock.h>

using DBusDock = com::deepin::dde::daemon::Dock;
class QMenu;
class QAction;
class DockItemManager;
class MenuWorker : public QObject
{
    Q_OBJECT
public:
    explicit MenuWorker(DBusDock *dockInter,QWidget *parent = nullptr);
    ~ MenuWorker();
    //TODO 后面考虑做成单例
    //    static MenuWorker& Instance();

    void initMember();
    void initUI();
    void initConnection();

    void showDockSettingsMenu();
    inline bool menuEnable() const { return m_menuEnable; }
    inline quint8 Opacity() const { return quint8(m_opacity * 255); }

    void onGSettingsChanged(const QString &key);
    // TODO 是否还有其他的插件未处理其gsettings配置,另外,这里后面可以优化全自动的,而非一个一个单独处理
    void onTrashGSettingsChanged(const QString &key);

signals:
    void autoHideChanged(const bool autoHide) const;
    void trayCountChanged();

private slots:
    void menuActionClicked(QAction *action);
    void trayVisableCountChanged(const int &count);
    void gtkIconThemeChanged();

public slots:
    void setAutoHide(const bool autoHide);

private:
    MenuWorker(MenuWorker const &) = delete;
    MenuWorker operator =(MenuWorker const &) = delete;

    DockItemManager *m_itemManager;
    DBusDock *m_dockInter;

    QMenu *m_settingsMenu;
    QMenu *m_hideSubMenu;
    QAction *m_fashionModeAct;
    QAction *m_efficientModeAct;
    QAction *m_topPosAct;
    QAction *m_bottomPosAct;
    QAction *m_leftPosAct;
    QAction *m_rightPosAct;
    QAction *m_keepShownAct;
    QAction *m_keepHiddenAct;
    QAction *m_smartHideAct;

    bool m_menuEnable;
    bool m_autoHide;
    bool m_trashPluginShow;
    double m_opacity;
};

#endif // MENUWORKER_H
