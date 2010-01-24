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

#ifndef EMACSKEYS_HANDLER_H
#define EMACSKEYS_HANDLER_H

#include "emacskeysactions.h"

#include <QtCore/QObject>
#include <QtGui/QTextEdit>

namespace EmacsKeys {
namespace Internal {

class EmacsKeysHandler : public QObject
{
    Q_OBJECT

public:
    EmacsKeysHandler(QWidget *widget, QObject *parent = 0);
    ~EmacsKeysHandler();

    QWidget *widget();

public slots:
    void setCurrentFileName(const QString &fileName);

    // This executes an "ex" style command taking context
    // information from widget;
    void handleCommand(const QString &cmd);

    void installEventFilter();

    // Convenience
    void setupWidget();
    void restoreWidget();

signals:
    void commandBufferChanged(const QString &msg);
    void statusDataChanged(const QString &msg);
    void extraInformationChanged(const QString &msg);
    void quitRequested(bool force);
    void quitAllRequested(bool force);
    void selectionChanged(const QList<QTextEdit::ExtraSelection> &selection);
    void writeFileRequested(bool *handled,
        const QString &fileName, const QString &contents);
    void moveToMatchingParenthesis(bool *moved, bool *forward, QTextCursor *cursor);
    void indentRegion(int *amount, int beginLine, int endLine, QChar typedChar);
    void completionRequested();
    void windowCommandRequested(int key);
    void findRequested(bool reverse);
    void findNextRequested(bool reverse);

public:
    class Private;

private:
    bool eventFilter(QObject *ob, QEvent *ev);
    friend class Private;
    Private *d;
};

} // namespace Internal
} // namespace EmacsKeys

#endif // EMACSKEYS_H
