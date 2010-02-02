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

#ifndef EMACSKEYS_ACTIONS_H
#define EMACSKEYS_ACTIONS_H

#include <utils/savedaction.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>

namespace EmacsKeys {
namespace Internal {

enum EmacsKeysSettingsCode
{
    ConfigUseEmacsKeys,
    ConfigStartOfLine,
    ConfigHlSearch,
    ConfigTabStop,
    ConfigSmartTab,
    ConfigShiftWidth,
    ConfigExpandTab,
    ConfigAutoIndent,
    ConfigIncSearch,

    // indent  allow backspacing over autoindent
    // eol     allow backspacing over line breaks (join lines)
    // start   allow backspacing over the start of insert; CTRL-W and CTRL-U
    //         stop once at the start of insert.
    ConfigBackspace,

    // other actions
    SettingsDialog,
};

class EmacsKeysSettings : public QObject
{
public:
    EmacsKeysSettings();
    ~EmacsKeysSettings();
    void insertItem(int code, Utils::SavedAction *item,
        const QString &longname = QString(),
        const QString &shortname = QString());

    Utils::SavedAction *item(int code);
    Utils::SavedAction *item(const QString &name);

    void readSettings(QSettings *settings);
    void writeSettings(QSettings *settings);

private:
    QHash<int, Utils::SavedAction *> m_items;
    QHash<QString, int> m_nameToCode; 
    QHash<int, QString> m_codeToName; 
};

EmacsKeysSettings *theEmacsKeysSettings();
Utils::SavedAction *theEmacsKeysSetting(int code);

} // namespace Internal
} // namespace EmacsKeys

#endif // EMACSKEYS_ACTTIONS_H
