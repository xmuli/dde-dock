/**
 * Copyright (C) 2015 Deepin Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 **/

#ifndef COMPOSITETRAYITEM_H
#define COMPOSITETRAYITEM_H

#include <QFrame>
#include <QMap>

#include <dimagebutton.h>

#include "interfaces/dockconstants.h"

DWIDGET_USE_NAMESPACE;

class TrayIcon;
class QLabel;
class CompositeTrayItem : public QFrame
{
    Q_OBJECT
public:
    explicit CompositeTrayItem(QWidget *parent = 0);
    virtual ~CompositeTrayItem();

    void addTrayIcon(QString key, TrayIcon * item);
    void remove(QString key);
    void setMode(const Dock::DockMode &mode);
    void clear();

    bool exist(const QString &key);
    QStringList trayIds() const;
    Dock::DockMode mode() const;

    void coverOn();
    void coverOff();

signals:
    void sizeChanged();

public slots:
    void handleTrayiconDamage();
    void handleUpdateTimer();

protected:
    bool eventFilter(QObject *obj, QEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *) Q_DECL_OVERRIDE;
    void enterEvent(QEvent *) Q_DECL_OVERRIDE;
    void leaveEvent(QEvent *) Q_DECL_OVERRIDE;

private:
    Dock::DockMode m_mode;
    QMap<QString, TrayIcon*> m_icons;
    QPixmap m_itemMask;
    QLabel * m_cover;
    QTimer * m_coverTimer;
    QTimer * m_updateTimer;
    DImageButton * m_foldButton;
    DImageButton * m_unfoldButton;
    bool m_isCovered;
    bool m_isFolded;

    void relayout();

private slots:
    void tryCoverOn();
    void fold();
    void unfold();
};

#endif // COMPOSITETRAYITEM_H