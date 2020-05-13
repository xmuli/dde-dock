#ifndef CMDCONTROL_H
#define CMDCONTROL_H


class CmdControl
{
private:
    CmdControl();
    static CmdControl *instance;

public:
    static CmdControl *getInstance();
    bool m_luancherEnable;  //启动器
    bool m_appEnable;
    bool m_pluginEnable;
    bool m_trayPluginEnable;
    bool m_aiassistantEnable;
    bool m_datetimeEnable;
    bool m_keyboardEnable;
    bool m_multitaskingEnable;
    bool m_showdesktopEnable;
    bool m_shutdownEnable;
    bool m_trashEnable;
    bool m_overlaywarning;
    bool m_bluetoothEnable;
    bool m_networkEnable;
    bool m_powerEnable;
    bool m_soundEnable;

};

#endif // CMDCONTROL_H
