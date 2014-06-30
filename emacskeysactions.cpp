/**************************************************************************
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://www.qtsoftware.com/contact.
**
**************************************************************************/

#include "emacskeysactions.h"

// Please do not add any direct dependencies to other Qt Creator code  here.
// Instead emit signals and let the EmacsKeysPlugin channel the information to
// Qt Creator. The idea is to keep this file here in a "clean" state that
// allows easy reuse with any QTextEdit or QPlainTextEdit derived class.


#include <utils/qtcassert.h>

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QRegExp>
#include <QtCore/QTextStream>
#include <QtCore/QtAlgorithms>
#include <QtCore/QCoreApplication>
#include <QtCore/QStack>

using namespace Core::Utils;

///////////////////////////////////////////////////////////////////////
//
// EmacsKeysSettings
//
///////////////////////////////////////////////////////////////////////

namespace EmacsKeys {
namespace Internal {

EmacsKeysSettings::EmacsKeysSettings()
{}

EmacsKeysSettings::~EmacsKeysSettings()
{
    qDeleteAll(m_items);
}

void EmacsKeysSettings::insertItem(int code, SavedAction *item,
    const QString &longName, const QString &shortName)
{
    QTC_ASSERT(!m_items.contains(code), qDebug() << code << item->toString(); return);
    m_items[code] = item;
    if (!longName.isEmpty()) {
        m_nameToCode[longName] = code;
        m_codeToName[code] = longName;
    }
    if (!shortName.isEmpty()) {
        m_nameToCode[shortName] = code;
    }
}

void EmacsKeysSettings::readSettings(QSettings *settings)
{
    foreach (SavedAction *item, m_items)
        item->readSettings(settings);
}

void EmacsKeysSettings::writeSettings(QSettings *settings)
{
    foreach (SavedAction *item, m_items)
        item->writeSettings(settings);
}

SavedAction *EmacsKeysSettings::item(int code)
{
    QTC_ASSERT(m_items.value(code, 0), qDebug() << "CODE: " << code; return 0);
    return m_items.value(code, 0);
}

SavedAction *EmacsKeysSettings::item(const QString &name)
{
    return m_items.value(m_nameToCode.value(name, -1), 0);
}

EmacsKeysSettings *theEmacsKeysSettings()
{
    static EmacsKeysSettings *instance = 0;
    if (instance)
        return instance;

    instance = new EmacsKeysSettings;

    SavedAction *item = 0;

    const QString group = QLatin1String("EmacsKeys");
    item = new SavedAction(instance);
    item->setText(QCoreApplication::translate("EmacsKeys::Internal", "Toggle vim-style editing"));
    item->setSettingsKey(group, QLatin1String("UseEmacsKeys"));
    item->setCheckable(true);
    instance->insertItem(ConfigUseEmacsKeys, item);

    item = new SavedAction(instance);
    item->setDefaultValue(false);
    item->setSettingsKey(group, QLatin1String("StartOfLine"));
    item->setCheckable(true);
    instance->insertItem(ConfigStartOfLine, item, QLatin1String("startofline"), QLatin1String("sol"));

    item = new SavedAction(instance);
    item->setDefaultValue(8);
    item->setSettingsKey(group, QLatin1String("TabStop"));
    instance->insertItem(ConfigTabStop, item, QLatin1String("tabstop"), QLatin1String("ts"));

    item = new SavedAction(instance);
    item->setDefaultValue(false);
    item->setSettingsKey(group, QLatin1String("SmartTab"));
    instance->insertItem(ConfigSmartTab, item, QLatin1String("smarttab"), QLatin1String("sta"));

    item = new SavedAction(instance);
    item->setDefaultValue(true);
    item->setSettingsKey(group, QLatin1String("HlSearch"));
    item->setCheckable(true);
    instance->insertItem(ConfigHlSearch, item, QLatin1String("hlsearch"), QLatin1String("hls"));

    item = new SavedAction(instance);
    item->setDefaultValue(8);
    item->setSettingsKey(group, QLatin1String("ShiftWidth"));
    instance->insertItem(ConfigShiftWidth, item, QLatin1String("shiftwidth"), QLatin1String("sw"));

    item = new SavedAction(instance);
    item->setDefaultValue(false);
    item->setSettingsKey(group, QLatin1String("ExpandTab"));
    item->setCheckable(true);
    instance->insertItem(ConfigExpandTab, item, QLatin1String("expandtab"), QLatin1String("et"));

    item = new SavedAction(instance);
    item->setDefaultValue(false);
    item->setSettingsKey(group, QLatin1String("AutoIndent"));
    item->setCheckable(true);
    instance->insertItem(ConfigAutoIndent, item, QLatin1String("autoindent"), QLatin1String("ai"));

    item = new SavedAction(instance);
    item->setDefaultValue(true);
    item->setSettingsKey(group, QLatin1String("IncSearch"));
    item->setCheckable(true);
    instance->insertItem(ConfigIncSearch, item, QLatin1String("incsearch"), QLatin1String("is"));

    item = new SavedAction(instance);
    item->setDefaultValue(QLatin1String("indent,eol,start"));
    item->setSettingsKey(group, QLatin1String("Backspace"));
    instance->insertItem(ConfigBackspace, item, QLatin1String("backspace"), QLatin1String("bs"));

    item = new SavedAction(instance);
    item->setText(QCoreApplication::translate("EmacsKeys::Internal", "EmacsKeys properties..."));
    instance->insertItem(SettingsDialog, item);

    return instance;
}

SavedAction *theEmacsKeysSetting(int code)
{
    return theEmacsKeysSettings()->item(code);
}

} // namespace Internal
} // namespace EmacsKeys
