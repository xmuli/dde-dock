#include "docksettings.h"

#include <QDebug>

DockSettings::DockSettings(QObject *parent)
    : QObject(parent)
{
}

DockSide DockSettings::side() const
{
    return Bottom;
}

const QSize DockSettings::mainWindowSize() const
{
    return m_mainWindowSize;
}

void DockSettings::updateGeometry()
{

}