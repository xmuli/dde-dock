#include "cmdcontrol.h"


CmdControl * CmdControl::instance = nullptr;

CmdControl::CmdControl()
  : m_luancherEnable(true)
  , m_appEnable(true)
  , m_pluginEnable(true)
  , m_trayPluginEnable(true)
  , m_aiassistantEnable(true)
  , m_datetimeEnable(true)
  , m_keyboardEnable(true)
  , m_multitaskingEnable(true)
  , m_showdesktopEnable(true)
  , m_shutdownEnable(true)
  , m_trashEnable(true)
  , m_overlaywarning(true)
  , m_bluetoothEnable(true)
  , m_networkEnable(true)
  , m_powerEnable(true)
  , m_soundEnable(true)
{

}

CmdControl *CmdControl::getInstance()
{
    if (instance == nullptr)
        instance = new CmdControl;

    return instance;
}
