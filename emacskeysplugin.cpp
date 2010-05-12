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

#include "emacskeysplugin.h"

#include "emacskeyshandler.h"
#include "ui_emacskeysoptions.h"


#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/filemanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/ifile.h>
#include <coreplugin/dialogs/ioptionspage.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/modemanager.h>
#include <coreplugin/uniqueidmanager.h>

#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/session.h>

#include <texteditor/basetexteditor.h>
#include <texteditor/basetextmark.h>
#include <texteditor/completionsupport.h>
#include <texteditor/itexteditor.h>
#include <texteditor/texteditorconstants.h>
#include <texteditor/tabsettings.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/textblockiterator.h>

#include <find/textfindconstants.h>

#include <utils/qtcassert.h>
#include <utils/savedaction.h>

#include <indenter.h>

#include <QtCore/QDebug>
#include <QtCore/QtPlugin>
#include <QtCore/QObject>
#include <QtCore/QPoint>
#include <QtCore/QSettings>
#include <QtCore/QHash>

#include <QtGui/QMessageBox>
#include <QtGui/QPlainTextEdit>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextEdit>
#include <QtGui/QMainWindow>
#include <QtGui/QMenu>

using namespace EmacsKeys::Internal;
using namespace TextEditor;
using namespace Core;
using namespace ProjectExplorer;


namespace EmacsKeys {
namespace Constants {

const char * const INSTALL_HANDLER        = "TextEditor.EmacsKeysHandler";
const char * const MINI_BUFFER            = "TextEditor.EmacsKeysMiniBuffer";

} // namespace Constants
} // namespace EmacsKeys


///////////////////////////////////////////////////////////////////////
//
// EmacsKeysOptionPage
//
///////////////////////////////////////////////////////////////////////

namespace EmacsKeys {
namespace Internal {

class EmacsKeysOptionPage : public Core::IOptionsPage
{
    Q_OBJECT

public:
    EmacsKeysOptionPage() {}

    // IOptionsPage
    QString id() const { return QLatin1String("General"); }
    QString trName() const { return tr("General"); }
    QString category() const { return QLatin1String("EmacsKeys"); }
    QString trCategory() const { return tr("EmacsKeys"); }
    QString displayName() const { return tr("General"); }
    QString displayCategory() const { return tr("EmacsKeys"); }
    QIcon categoryIcon() const { return QIcon(); }

    QWidget *createPage(QWidget *parent);
    void apply() { m_group.apply(ICore::instance()->settings()); }
    void finish() { m_group.finish(); }

private slots:
    void copyTextEditorSettings();
    void setQtStyle();
    void setPlainStyle();

private:
    friend class DebuggerPlugin;
    Ui::EmacsKeysOptionPage m_ui;

    Utils::SavedActionSet m_group;
};

QWidget *EmacsKeysOptionPage::createPage(QWidget *parent)
{
    QWidget *w = new QWidget(parent);
    m_ui.setupUi(w);

    m_group.clear();
    m_group.insert(theEmacsKeysSetting(ConfigUseEmacsKeys), 
        m_ui.checkBoxUseEmacsKeys);

    m_group.insert(theEmacsKeysSetting(ConfigExpandTab), 
        m_ui.checkBoxExpandTab);
    m_group.insert(theEmacsKeysSetting(ConfigHlSearch), 
        m_ui.checkBoxHlSearch);
    m_group.insert(theEmacsKeysSetting(ConfigShiftWidth), 
        m_ui.lineEditShiftWidth);

    m_group.insert(theEmacsKeysSetting(ConfigSmartTab), 
        m_ui.checkBoxSmartTab);
    m_group.insert(theEmacsKeysSetting(ConfigStartOfLine), 
        m_ui.checkBoxStartOfLine);
    m_group.insert(theEmacsKeysSetting(ConfigTabStop), 
        m_ui.lineEditTabStop);
    m_group.insert(theEmacsKeysSetting(ConfigBackspace), 
        m_ui.lineEditBackspace);

    m_group.insert(theEmacsKeysSetting(ConfigAutoIndent), 
        m_ui.checkBoxAutoIndent);
    m_group.insert(theEmacsKeysSetting(ConfigIncSearch), 
        m_ui.checkBoxIncSearch);

    connect(m_ui.pushButtonCopyTextEditorSettings, SIGNAL(clicked()),
        this, SLOT(copyTextEditorSettings()));
    connect(m_ui.pushButtonSetQtStyle, SIGNAL(clicked()),
        this, SLOT(setQtStyle()));
    connect(m_ui.pushButtonSetPlainStyle, SIGNAL(clicked()),
        this, SLOT(setPlainStyle()));

    return w;
}

void EmacsKeysOptionPage::copyTextEditorSettings()
{
    TextEditor::TabSettings ts = 
        TextEditor::TextEditorSettings::instance()->tabSettings();
    
    m_ui.checkBoxExpandTab->setChecked(ts.m_spacesForTabs);
    m_ui.lineEditTabStop->setText(QString::number(ts.m_tabSize));
    m_ui.lineEditShiftWidth->setText(QString::number(ts.m_indentSize));
    m_ui.checkBoxSmartTab->setChecked(ts.m_smartBackspace);
    m_ui.checkBoxAutoIndent->setChecked(ts.m_autoIndent);
    // FIXME: Not present in core
    //m_ui.checkBoxIncSearch->setChecked(ts.m_incSearch);
}

void EmacsKeysOptionPage::setQtStyle()
{
    m_ui.checkBoxExpandTab->setChecked(true);
    m_ui.lineEditTabStop->setText("4");
    m_ui.lineEditShiftWidth->setText("4");
    m_ui.checkBoxSmartTab->setChecked(true);
    m_ui.checkBoxAutoIndent->setChecked(true);
    m_ui.checkBoxIncSearch->setChecked(true);
    m_ui.lineEditBackspace->setText("indent,eol,start");
}

void EmacsKeysOptionPage::setPlainStyle()
{
    m_ui.checkBoxExpandTab->setChecked(false);
    m_ui.lineEditTabStop->setText("8");
    m_ui.lineEditShiftWidth->setText("8");
    m_ui.checkBoxSmartTab->setChecked(false);
    m_ui.checkBoxAutoIndent->setChecked(false);
    m_ui.checkBoxIncSearch->setChecked(false);
    m_ui.lineEditBackspace->setText(QString());
}

} // namespace Internal
} // namespace EmacsKeys


///////////////////////////////////////////////////////////////////////
//
// EmacsKeysPluginPrivate
//
///////////////////////////////////////////////////////////////////////

namespace EmacsKeys {
namespace Internal {

class EmacsKeysPluginPrivate : public QObject
{
    Q_OBJECT

public:
    EmacsKeysPluginPrivate(EmacsKeysPlugin *);
    ~EmacsKeysPluginPrivate();
    friend class EmacsKeysPlugin;

    bool initialize();
    void shutdown();

private slots:
    void editorOpened(Core::IEditor *);
    void editorAboutToClose(Core::IEditor *);

    void setUseEmacsKeys(const QVariant &value);
    void quitEmacsKeys();
    void triggerCompletions();
    void windowCommand(int key);
    void find(bool reverse);
    void findNext(bool reverse);
    void showSettingsDialog();

    void showCommandBuffer(const QString &contents);
    void showExtraInformation(const QString &msg);
    void changeSelection(const QList<QTextEdit::ExtraSelection> &selections);
    void writeFile(bool *handled, const QString &fileName, const QString &contents);
    void quitFile(bool forced);
    void quitAllFiles(bool forced);
    void moveToMatchingParenthesis(bool *moved, bool *forward, QTextCursor *cursor);
    void indentRegion(int *amount, int beginLine, int endLine,  QChar typedChar);

private:
    EmacsKeysPlugin *q;
    EmacsKeysOptionPage *m_emacsKeysOptionsPage;
    QHash<Core::IEditor *, EmacsKeysHandler *> m_editorToHandler;

    void triggerAction(const QString& code);
};

} // namespace Internal
} // namespace EmacsKeys

EmacsKeysPluginPrivate::EmacsKeysPluginPrivate(EmacsKeysPlugin *plugin)
{       
    q = plugin;
    m_emacsKeysOptionsPage = 0;
}

EmacsKeysPluginPrivate::~EmacsKeysPluginPrivate()
{
}

void EmacsKeysPluginPrivate::shutdown()
{
    q->removeObject(m_emacsKeysOptionsPage);
    delete m_emacsKeysOptionsPage;
    m_emacsKeysOptionsPage = 0;
    theEmacsKeysSettings()->writeSettings(Core::ICore::instance()->settings());
    delete theEmacsKeysSettings();
}

bool EmacsKeysPluginPrivate::initialize()
{
    Core::ActionManager *actionManager = Core::ICore::instance()->actionManager();
    QTC_ASSERT(actionManager, return false);



    m_emacsKeysOptionsPage = new EmacsKeysOptionPage;
    q->addObject(m_emacsKeysOptionsPage);
    theEmacsKeysSettings()->readSettings(Core::ICore::instance()->settings());
    
    QList<int> globalcontext;
    globalcontext << Core::Constants::C_GLOBAL_ID;
    Core::Command *cmd = 0;
    cmd = actionManager->registerAction(theEmacsKeysSetting(ConfigUseEmacsKeys),
        Constants::INSTALL_HANDLER, globalcontext);

    ActionContainer *advancedMenu =
        actionManager->actionContainer(Core::Constants::M_EDIT_ADVANCED);
    advancedMenu->addAction(cmd, Core::Constants::G_EDIT_EDITOR);

    ActionContainer* actionContainer = actionManager->actionContainer(Core::Constants::M_FILE);
    QMenu* menu = actionContainer->menu();
    menu->setTitle("F&ile");

    actionContainer = actionManager->actionContainer(Core::Constants::M_EDIT);
    menu = actionContainer->menu();
    menu->setTitle("Edit");

    QHash<QString, QString> replacements;
    replacements["&Build"] = "Build";
    replacements["&Debug"] = "Debug";
    replacements["&Tools"] = "Tools";
    replacements["&Window"] = "Window";

    actionContainer = actionManager->actionContainer(Core::Constants::MENU_BAR);
    qDebug() << menu->parentWidget()->metaObject()->className() << endl;
    QMainWindow* mainWindow = qobject_cast<QMainWindow*>(menu->parentWidget());
    foreach(QObject* child, mainWindow->children()) {
        if (QMenu* menuChild = qobject_cast<QMenu*>(child)) {
            if (replacements.contains(menuChild->title())) {
                menuChild->setTitle(replacements[menuChild->title()]);
            }
        }
    }

    Command* command = actionManager->command("QtCreator.Locate");
    command->setKeySequence(QKeySequence("Ctrl+X,B"));

    command = actionManager->command(TextEditor::Constants::COMPLETE_THIS);
    command->setKeySequence(QKeySequence("Alt+/"));

    command = actionManager->command("QtCreator.Sidebar.File System");
    if (command) {
        command->setKeySequence(QKeySequence("Ctrl+X,Ctrl+B"));
    }

    // EditorManager
    QObject *editorManager = Core::ICore::instance()->editorManager();
    connect(editorManager, SIGNAL(editorAboutToClose(Core::IEditor*)),
        this, SLOT(editorAboutToClose(Core::IEditor*)));
    connect(editorManager, SIGNAL(editorOpened(Core::IEditor*)),
        this, SLOT(editorOpened(Core::IEditor*)));

    connect(theEmacsKeysSetting(SettingsDialog), SIGNAL(triggered()),
        this, SLOT(showSettingsDialog()));
    connect(theEmacsKeysSetting(ConfigUseEmacsKeys), SIGNAL(valueChanged(QVariant)),
        this, SLOT(setUseEmacsKeys(QVariant)));

    return true;
}

void EmacsKeysPluginPrivate::showSettingsDialog()
{
    Core::ICore::instance()->showOptionsDialog("EmacsKeys", "General");
}

void EmacsKeysPluginPrivate::triggerAction(const QString& code)
{
    Core::ActionManager *am = Core::ICore::instance()->actionManager();
    QTC_ASSERT(am, return);
    Core::Command *cmd = am->command(code);
    QTC_ASSERT(cmd, return);
    QAction *action = cmd->action();
    QTC_ASSERT(action, return);
    action->trigger();
}

void EmacsKeysPluginPrivate::windowCommand(int key)
{
    #define control(n) (256 + n)
    QString code;
    switch (key) {
        case 'c': case 'C': case control('c'):
            code = Core::Constants::CLOSE;
            break;
        case 'n': case 'N': case control('n'):
            code = Core::Constants::GOTONEXT;
            break;
        case 'o': case 'O': case control('o'):
            code = Core::Constants::REMOVE_ALL_SPLITS;
            code = Core::Constants::REMOVE_CURRENT_SPLIT;
            break;
        case 'p': case 'P': case control('p'):
            code = Core::Constants::GOTOPREV;
            break;
        case 's': case 'S': case control('s'):
            code = Core::Constants::SPLIT;
            break;
        case 'w': case 'W': case control('w'):
            code = Core::Constants::GOTO_OTHER_SPLIT;
            break;
    }
    #undef control
    qDebug() << "RUNNING WINDOW COMMAND: " << key << code;
    if (code.isEmpty()) {
        qDebug() << "UNKNOWN WINDOWS COMMAND: " << key;
        return;
    }
    triggerAction(code);
}

void EmacsKeysPluginPrivate::find(bool reverse)
{
    Q_UNUSED(reverse);  // TODO: Creator needs an action for find in reverse.
    triggerAction(Find::Constants::FIND_IN_DOCUMENT);
}

void EmacsKeysPluginPrivate::findNext(bool reverse)
{
    if (reverse)
        triggerAction(Find::Constants::FIND_PREVIOUS);
    else
        triggerAction(Find::Constants::FIND_NEXT);
}

void EmacsKeysPluginPrivate::editorOpened(Core::IEditor *editor)
{
    if (!editor)
        return;

    QWidget *widget = editor->widget();
    if (!widget)
        return;

    // we can only handle QTextEdit and QPlainTextEdit
    if (!qobject_cast<QTextEdit *>(widget) && !qobject_cast<QPlainTextEdit *>(widget))
        return;
    
    //qDebug() << "OPENING: " << editor << editor->widget()
    //    << "MODE: " << theEmacsKeysSetting(ConfigUseEmacsKeys)->value();

    EmacsKeysHandler *handler = new EmacsKeysHandler(widget, widget);
    m_editorToHandler[editor] = handler;

    connect(handler, SIGNAL(extraInformationChanged(QString)),
        this, SLOT(showExtraInformation(QString)));
    connect(handler, SIGNAL(commandBufferChanged(QString)),
        this, SLOT(showCommandBuffer(QString)));
    connect(handler, SIGNAL(quitRequested(bool)),
        this, SLOT(quitFile(bool)), Qt::QueuedConnection);
    connect(handler, SIGNAL(quitAllRequested(bool)),
        this, SLOT(quitAllFiles(bool)), Qt::QueuedConnection);
    connect(handler, SIGNAL(writeFileRequested(bool*,QString,QString)),
        this, SLOT(writeFile(bool*,QString,QString)));
    connect(handler, SIGNAL(selectionChanged(QList<QTextEdit::ExtraSelection>)),
        this, SLOT(changeSelection(QList<QTextEdit::ExtraSelection>)));
    connect(handler, SIGNAL(moveToMatchingParenthesis(bool*,bool*,QTextCursor*)),
        this, SLOT(moveToMatchingParenthesis(bool*,bool*,QTextCursor*)));
    connect(handler, SIGNAL(indentRegion(int*,int,int,QChar)),
        this, SLOT(indentRegion(int*,int,int,QChar)));
    connect(handler, SIGNAL(completionRequested()),
        this, SLOT(triggerCompletions()));
    connect(handler, SIGNAL(windowCommandRequested(int)),
        this, SLOT(windowCommand(int)));
    connect(handler, SIGNAL(findRequested(bool)),
        this, SLOT(find(bool)));
    connect(handler, SIGNAL(findNextRequested(bool)),
        this, SLOT(findNext(bool)));

    handler->setCurrentFileName(editor->file()->fileName());
    handler->installEventFilter();
    
    // pop up the bar
    if (theEmacsKeysSetting(ConfigUseEmacsKeys)->value().toBool())
       showCommandBuffer("");
}

void EmacsKeysPluginPrivate::editorAboutToClose(Core::IEditor *editor)
{
    //qDebug() << "CLOSING: " << editor << editor->widget();
    m_editorToHandler.remove(editor);
}

void EmacsKeysPluginPrivate::setUseEmacsKeys(const QVariant &value)
{
    //qDebug() << "SET USE EMACSKEYS" << value;
    bool on = value.toBool();
    if (on) {
        Core::EditorManager::instance()->showEditorStatusBar( 
            QLatin1String(Constants::MINI_BUFFER), 
            "vi emulation mode. Type :q to leave. Use , Ctrl-R to trigger run.",
            tr("Quit EmacsKeys"), this, SLOT(quitEmacsKeys()));
        foreach (Core::IEditor *editor, m_editorToHandler.keys())
            m_editorToHandler[editor]->setupWidget();
    } else {
        Core::EditorManager::instance()->hideEditorStatusBar(
            QLatin1String(Constants::MINI_BUFFER));
        foreach (Core::IEditor *editor, m_editorToHandler.keys())
            m_editorToHandler[editor]->restoreWidget();
    }
}

void EmacsKeysPluginPrivate::triggerCompletions()
{
    EmacsKeysHandler *handler = qobject_cast<EmacsKeysHandler *>(sender());
    if (!handler)
        return;
    if (BaseTextEditor *bt = qobject_cast<BaseTextEditor *>(handler->widget()))
        TextEditor::Internal::CompletionSupport::instance()->
            autoComplete(bt->editableInterface(), false);
   //     bt->triggerCompletions();
}

void EmacsKeysPluginPrivate::quitFile(bool forced)
{
    EmacsKeysHandler *handler = qobject_cast<EmacsKeysHandler *>(sender());
    if (!handler)
        return;
    QList<Core::IEditor *> editors;
    editors.append(m_editorToHandler.key(handler));
    Core::EditorManager::instance()->closeEditors(editors, !forced);
}

void EmacsKeysPluginPrivate::quitAllFiles(bool forced)
{
    Core::EditorManager::instance()->closeAllEditors(!forced);
}

void EmacsKeysPluginPrivate::writeFile(bool *handled,
    const QString &fileName, const QString &contents)
{
    Q_UNUSED(contents);

    EmacsKeysHandler *handler = qobject_cast<EmacsKeysHandler *>(sender());
    if (!handler)
        return;

    Core::IEditor *editor = m_editorToHandler.key(handler);
    if (editor && editor->file()->fileName() == fileName) {
        // Handle that as a special case for nicer interaction with core
        Core::IFile *file = editor->file();
        Core::ICore::instance()->fileManager()->blockFileChange(file);
        file->save(fileName);
        Core::ICore::instance()->fileManager()->unblockFileChange(file);
        *handled = true;
    } 
}

void EmacsKeysPluginPrivate::moveToMatchingParenthesis(bool *moved, bool *forward,
        QTextCursor *cursor)
{
    *moved = false;

    bool undoFakeEOL = false;
    if (cursor->atBlockEnd() && cursor->block().length() > 1) {
        cursor->movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
        undoFakeEOL = true;
    }
    TextEditor::TextBlockUserData::MatchType match
        = TextEditor::TextBlockUserData::matchCursorForward(cursor);
    if (match == TextEditor::TextBlockUserData::Match) {
        *moved = true;
        *forward = true;
   } else {
        if (undoFakeEOL)
            cursor->movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
        if (match == TextEditor::TextBlockUserData::NoMatch) {
            // backward matching is according to the character before the cursor
            bool undoMove = false;
            if (!cursor->atBlockEnd()) {
                cursor->movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
                undoMove = true;
            }
            match = TextEditor::TextBlockUserData::matchCursorBackward(cursor);
            if (match == TextEditor::TextBlockUserData::Match) {
                *moved = true;
                *forward = false;
            } else if (undoMove) {
                cursor->movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 1);
            }
        }
    }
}

void EmacsKeysPluginPrivate::indentRegion(int *amount, int beginLine, int endLine,
      QChar typedChar)
{
    EmacsKeysHandler *handler = qobject_cast<EmacsKeysHandler *>(sender());
    if (!handler)
        return;

    BaseTextEditor *bt = qobject_cast<BaseTextEditor *>(handler->widget());
    if (!bt)
        return;

    TextEditor::TabSettings tabSettings = 
        TextEditor::TextEditorSettings::instance()->tabSettings();
    typedef SharedTools::Indenter<TextEditor::TextBlockIterator> Indenter;
    Indenter &indenter = Indenter::instance();
    indenter.setIndentSize(tabSettings.m_indentSize);
    indenter.setTabSize(tabSettings.m_tabSize);

    const QTextDocument *doc = bt->document();
    QTextBlock begin = doc->findBlockByNumber(beginLine);
    QTextBlock end = doc->findBlockByNumber(endLine);
    const TextEditor::TextBlockIterator docStart(doc->begin());
    QTextBlock cur = begin;
    do {
        if (typedChar == 0 && cur.text().simplified().isEmpty()) {
            *amount = 0;
            if (cur != end) {
                QTextCursor cursor(cur);
                while (!cursor.atBlockEnd())
                    cursor.deleteChar();
            }
        } else {
            const TextEditor::TextBlockIterator current(cur);
            const TextEditor::TextBlockIterator next(cur.next());
            *amount = indenter.indentForBottomLine(current, docStart, next, typedChar);
            if (cur != end)
                tabSettings.indentLine(cur, *amount);
        }
        if (cur != end)
           cur = cur.next();
    } while (cur != end);
}

void EmacsKeysPluginPrivate::quitEmacsKeys()
{
    theEmacsKeysSetting(ConfigUseEmacsKeys)->setValue(false);
}

void EmacsKeysPluginPrivate::showCommandBuffer(const QString &contents)
{
    //qDebug() << "SHOW COMMAND BUFFER" << contents;
    Core::EditorManager::instance()->showEditorStatusBar( 
        QLatin1String(Constants::MINI_BUFFER), contents,
        tr("Quit EmacsKeys"), this, SLOT(quitEmacsKeys()));
}

void EmacsKeysPluginPrivate::showExtraInformation(const QString &text)
{
    EmacsKeysHandler *handler = qobject_cast<EmacsKeysHandler *>(sender());
    if (handler)
        QMessageBox::information(handler->widget(), tr("EmacsKeys Information"), text);
}

void EmacsKeysPluginPrivate::changeSelection
    (const QList<QTextEdit::ExtraSelection> &selection)
{
    if (EmacsKeysHandler *handler = qobject_cast<EmacsKeysHandler *>(sender()))
        if (BaseTextEditor *bt = qobject_cast<BaseTextEditor *>(handler->widget()))
            bt->setExtraSelections(BaseTextEditor::FakeVimSelection, selection);
}


///////////////////////////////////////////////////////////////////////
//
// EmacsKeysPlugin
//
///////////////////////////////////////////////////////////////////////

EmacsKeysPlugin::EmacsKeysPlugin()
    : d(new EmacsKeysPluginPrivate(this))
{}

EmacsKeysPlugin::~EmacsKeysPlugin()
{
    delete d;
}

bool EmacsKeysPlugin::initialize(const QStringList &arguments, QString *errorMessage)
{
    Q_UNUSED(arguments);
    Q_UNUSED(errorMessage);
    return d->initialize();
}

void EmacsKeysPlugin::shutdown()
{
    d->shutdown();
}

void EmacsKeysPlugin::extensionsInitialized()
{
}

#include "emacskeysplugin.moc"

Q_EXPORT_PLUGIN(EmacsKeysPlugin)
