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

#ifndef EMACSKEYSPLUGIN_H
#define EMACSKEYSPLUGIN_H

#include <extensionsystem/iplugin.h>

namespace EmacsKeys {
namespace Internal {

class EmacsKeysHandler;

class EmacsKeysPluginPrivate;

class EmacsKeysPlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT

public:
    EmacsKeysPlugin();
    ~EmacsKeysPlugin();

private:
    // implementation of ExtensionSystem::IPlugin
    bool initialize(const QStringList &arguments, QString *error_message);
    void shutdown();
    void extensionsInitialized();

private:
    friend class EmacsKeysPluginPrivate;
    EmacsKeysPluginPrivate *d;
};

} // namespace Internal
} // namespace EmacsKeys

#endif // EMACSKEYSPLUGIN_H
