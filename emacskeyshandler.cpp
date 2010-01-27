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

#include "emacskeyshandler.h"

//
// ATTENTION:
//
// 1 Please do not add any direct dependencies to other Qt Creator code here.
//   Instead emit signals and let the EmacsKeysPlugin channel the information to
//   Qt Creator. The idea is to keep this file here in a "clean" state that
//   allows easy reuse with any QTextEdit or QPlainTextEdit derived class.
//
// 2 There are a few auto tests located in ../../../tests/auto/emacsKeys.
//   Commands that are covered there are marked as "// tested" below.
//
// 3 Some conventions:
//
//   Use 1 based line numbers and 0 based column numbers. Even though
//   the 1 based line are not nice it matches vim's and QTextEdit's 'line'
//   concepts.
//
//   Do not pass QTextCursor etc around unless really needed. Convert
//   early to  line/column.
//
//   There is always a "current" cursor (m_tc). A current "region of interest"
//   spans between m_anchor (== anchor()) and  m_tc.position() (== position())
//   The value of m_tc.anchor() is not used.
// 

#include <utils/qtcassert.h>

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QRegExp>
#include <QtCore/QTextStream>
#include <QtCore/QtAlgorithms>
#include <QtCore/QStack>

#include <QtGui/QApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QLineEdit>
#include <QtGui/QPlainTextEdit>
#include <QtGui/QScrollBar>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocumentFragment>
#include <QtGui/QTextEdit>


#define DEBUG_KEY  1
#if DEBUG_KEY
#   define KEY_DEBUG(s) qDebug() << s
#else
#   define KEY_DEBUG(s)
#endif

//#define DEBUG_UNDO  1
#if DEBUG_UNDO
#   define UNDO_DEBUG(s) qDebug() << << m_tc.document()->revision() << s
#else
#   define UNDO_DEBUG(s)
#endif

using namespace Core::Utils;

namespace EmacsKeys {
namespace Internal {

///////////////////////////////////////////////////////////////////////
//
// EmacsKeysHandler
//
///////////////////////////////////////////////////////////////////////

#define StartOfLine     QTextCursor::StartOfLine
#define EndOfLine       QTextCursor::EndOfLine
#define MoveAnchor      QTextCursor::MoveAnchor
#define KeepAnchor      QTextCursor::KeepAnchor
#define Up              QTextCursor::Up
#define Down            QTextCursor::Down
#define Right           QTextCursor::Right
#define Left            QTextCursor::Left
#define EndOfDocument   QTextCursor::End
#define StartOfDocument QTextCursor::Start

#define EDITOR(s) (m_textedit ? m_textedit->s : m_plaintextedit->s)

const int ParagraphSeparator = 0x00002029;

using namespace Qt;


enum Mode
{
    InsertMode,
    CommandMode,
    ExMode,
    SearchForwardMode,
    SearchBackwardMode,
};

enum SubMode
{
    NoSubMode,
    ChangeSubMode,     // used for c
    DeleteSubMode,     // used for d
    FilterSubMode,     // used for !
    IndentSubMode,     // used for =
    RegisterSubMode,   // used for "
    ReplaceSubMode,    // used for R and r
    ShiftLeftSubMode,  // used for <
    ShiftRightSubMode, // used for >
    WindowSubMode,     // used for Ctrl-w
    YankSubMode,       // used for y
    ZSubMode,          // used for z
    CapitalZSubMode    // used for Z
};

enum SubSubMode
{
    // typically used for things that require one more data item
    // and are 'nested' behind a mode
    NoSubSubMode,
    FtSubSubMode,       // used for f, F, t, T
    MarkSubSubMode,     // used for m
    BackTickSubSubMode, // used for `
    TickSubSubMode,     // used for '
};

enum VisualMode
{
    NoVisualMode,
    VisualCharMode,
    VisualLineMode,
    VisualBlockMode,
};

enum MoveType
{
    MoveExclusive,
    MoveInclusive,
    MoveLineWise,
};

QDebug &operator<<(QDebug &ts, const QList<QTextEdit::ExtraSelection> &sels)
{
    foreach (QTextEdit::ExtraSelection sel, sels)
        ts << "SEL: " << sel.cursor.anchor() << sel.cursor.position();
    return ts;
}

QString quoteUnprintable(const QString &ba)
{
    QString res;
    for (int i = 0, n = ba.size(); i != n; ++i) {
        QChar c = ba.at(i);
        if (c.isPrint())
            res += c;
        else
            res += QString("\\x%1").arg(c.unicode(), 2, 16);
    }
    return res;
}

enum EventResult
{
    EventHandled,
    EventUnhandled,
    EventPassedToCore
};

class EmacsKeysHandler::Private
{
public:
    Private(EmacsKeysHandler *parent, QWidget *widget);

    EventResult handleEvent(QKeyEvent *ev);
    bool wantsOverride(QKeyEvent *ev);
    void handleCommand(const QString &cmd); // sets m_tc + handleExCommand
    void handleExCommand(const QString &cmd);

    void installEventFilter();
    void setupWidget();
    void restoreWidget();

    friend class EmacsKeysHandler;
    static int shift(int key) { return key + 32; }
    static int control(int key) { return key + 256; }

    void init();
    EventResult handleKey(int key, int unmodified, const QString &text);
    EventResult handleInsertMode(int key, int unmodified, const QString &text);
    EventResult handleCommandMode(int key, int unmodified, const QString &text);
    EventResult handleRegisterMode(int key, int unmodified, const QString &text);
    EventResult handleMiniBufferModes(int key, int unmodified, const QString &text);
    bool exactMatch(int key, const QKeySequence& keySequence);
    void finishMovement(const QString &text = QString());
    void search(const QString &needle, bool forward);
    void highlightMatches(const QString &needle);

  /*
  void yankPop(QTextEdit* view);
  void setMark();
  void exchangeDotAndMark();
  void popToMark();
  void copy();
  void cut();
  void yank();
  void killLine();
  void nextLine();
  void previousLine();
  void forwardChar();
  void backwardChar();
  void deleteNextChar();
  void charactersInserted(int line, int column, const QString& text);
  */

    int mvCount() const { return m_mvcount.isEmpty() ? 1 : m_mvcount.toInt(); }
    int opCount() const { return m_opcount.isEmpty() ? 1 : m_opcount.toInt(); }
    int count() const { return mvCount() * opCount(); }
    int leftDist() const { return m_tc.position() - m_tc.block().position(); }
    int rightDist() const { return m_tc.block().length() - leftDist() - 1; }
    bool atEndOfLine() const
        { return m_tc.atBlockEnd() && m_tc.block().length() > 1; }

    int lastPositionInDocument() const;
    int firstPositionInLine(int line) const; // 1 based line, 0 based pos
    int lastPositionInLine(int line) const; // 1 based line, 0 based pos
    int lineForPosition(int pos) const;  // 1 based line, 0 based pos

    // all zero-based counting
    int cursorLineOnScreen() const;
    int linesOnScreen() const;
    int columnsOnScreen() const;
    int cursorLineInDocument() const;
    int cursorColumnInDocument() const;
    int linesInDocument() const;
    void scrollToLineInDocument(int line);
    void scrollUp(int count);
    void scrollDown(int count) { scrollUp(-count); }

    // helper functions for indenting
    bool isElectricCharacter(QChar c) const
        { return c == '{' || c == '}' || c == '#'; }
    void indentRegion(QChar lastTyped = QChar());
    void shiftRegionLeft(int repeat = 1);
    void shiftRegionRight(int repeat = 1);

    void moveToFirstNonBlankOnLine();
    void moveToTargetColumn();
    void setTargetColumn() {
        m_targetColumn = leftDist();
        //qDebug() << "TARGET: " << m_targetColumn;
    }
    void moveToNextWord(bool simple);
    void moveToMatchingParanthesis();
    void moveToWordBoundary(bool simple, bool forward);

    // to reduce line noise
    void moveToEndOfDocument() { m_tc.movePosition(EndOfDocument, MoveAnchor); }
    void moveToStartOfLine();
    void moveToEndOfLine();
    void moveUp(int n = 1) { moveDown(-n); }
    void moveDown(int n = 1); // { m_tc.movePosition(Down, MoveAnchor, n); }
    void moveRight(int n = 1) { m_tc.movePosition(Right, MoveAnchor, n); }
    void moveLeft(int n = 1) { m_tc.movePosition(Left, MoveAnchor, n); }
    void setAnchor() { m_anchor = m_tc.position(); }
    void setAnchor(int position) { m_anchor = position; }
    void setPosition(int position) { m_tc.setPosition(position, MoveAnchor); }

    void handleFfTt(int key);

    // helper function for handleExCommand. return 1 based line index.
    int readLineCode(QString &cmd);
    void selectRange(int beginLine, int endLine);

    void enterInsertMode();
    void enterCommandMode();
    void enterExMode();
    void showRedMessage(const QString &msg);
    void showBlackMessage(const QString &msg);
    void notImplementedYet();
    void updateMiniBuffer();
    void updateSelection();
    QWidget *editor() const;
    QChar characterAtCursor() const
        { return m_tc.document()->characterAt(m_tc.position()); }
    void beginEditBlock() { UNDO_DEBUG("BEGIN EDIT BLOCK"); m_tc.beginEditBlock(); }
    void endEditBlock() { UNDO_DEBUG("END EDIT BLOCK"); m_tc.endEditBlock(); }
    void joinPreviousEditBlock() { UNDO_DEBUG("JOIN EDIT BLOCK"); m_tc.joinPreviousEditBlock(); }

public:
    QTextEdit *m_textedit;
    QPlainTextEdit *m_plaintextedit;
    bool m_wasReadOnly; // saves read-only state of document

    EmacsKeysHandler *q;
    Mode m_mode;
    bool m_passing; // let the core see the next event
    SubMode m_submode;
    SubSubMode m_subsubmode;
    int m_subsubdata;
    QString m_input;
    QTextCursor m_tc;
    QTextCursor m_oldTc; // copy from last event to check for external changes
    int m_anchor;
    QHash<int, QString> m_registers;
    int m_register;
    QString m_mvcount;
    QString m_opcount;
    MoveType m_moveType;

    bool m_fakeEnd;

    bool isSearchMode() const
        { return m_mode == SearchForwardMode || m_mode == SearchBackwardMode; }
    int m_gflag;  // whether current command started with 'g'

    QString m_commandBuffer;
    QString m_currentFileName;
    QString m_currentMessage;

    bool m_lastSearchForward;
    QString m_lastInsertion;

    QString removeSelectedText();
    int anchor() const { return m_anchor; }
    int position() const { return m_tc.position(); }
    QString selectedText() const;

    // undo handling
    void undo();
    void redo();
    QMap<int, int> m_undoCursorPosition; // revision -> position

    // extra data for '.'
    void replay(const QString &text, int count);
    void setDotCommand(const QString &cmd) { m_dotCommand = cmd; }
    void setDotCommand(const QString &cmd, int n) { m_dotCommand = cmd.arg(n); }
    QString m_dotCommand;
    bool m_inReplay; // true if we are executing a '.'

    // extra data for ';'
    QString m_semicolonCount;
    int m_semicolonType;  // 'f', 'F', 't', 'T'
    int m_semicolonKey;

    // history for '/'
    QString lastSearchString() const;
    static QStringList m_searchHistory;
    int m_searchHistoryIndex;

    // history for ':'
    static QStringList m_commandHistory;
    int m_commandHistoryIndex;

    // visual line mode
    void enterVisualMode(VisualMode visualMode);
    void leaveVisualMode();
    VisualMode m_visualMode;

    // marks as lines
    QHash<int, int> m_marks;
    QString m_oldNeedle;

    // vi style configuration
    QVariant config(int code) const { return theEmacsKeysSetting(code)->value(); }
    bool hasConfig(int code) const { return config(code).toBool(); }
    bool hasConfig(int code, const char *value) const // FIXME
        { return config(code).toString().contains(value); }

    // for restoring cursor position
    int m_savedYankPosition;
    int m_targetColumn;

    int m_cursorWidth;

    // auto-indent
    void insertAutomaticIndentation(bool goingDown);
    bool removeAutomaticIndentation(); // true if something removed
    // number of autoindented characters
    int m_justAutoIndented;
    void handleStartOfLine();

    void recordJump();
    void recordNewUndo();
    QList<int> m_jumpListUndo;
    QList<int> m_jumpListRedo;

    QList<QTextEdit::ExtraSelection> m_searchSelections;
};

QStringList EmacsKeysHandler::Private::m_searchHistory;
QStringList EmacsKeysHandler::Private::m_commandHistory;

EmacsKeysHandler::Private::Private(EmacsKeysHandler *parent, QWidget *widget)
{
    q = parent;
    m_textedit = qobject_cast<QTextEdit *>(widget);
    m_plaintextedit = qobject_cast<QPlainTextEdit *>(widget);
    init();
}

void EmacsKeysHandler::Private::init()
{
    m_mode = CommandMode;
    m_submode = NoSubMode;
    m_subsubmode = NoSubSubMode;
    m_passing = false;
    m_fakeEnd = false;
    m_lastSearchForward = true;
    m_register = '"';
    m_gflag = false;
    m_visualMode = NoVisualMode;
    m_targetColumn = 0;
    m_moveType = MoveInclusive;
    m_anchor = 0;
    m_savedYankPosition = 0;
    m_cursorWidth = EDITOR(cursorWidth());
    m_inReplay = false;
    m_justAutoIndented = 0;
}

bool EmacsKeysHandler::Private::wantsOverride(QKeyEvent *ev)
{
    const int key = ev->key();
    const int mods = ev->modifiers();
    KEY_DEBUG("SHORTCUT OVERRIDE" << key << "  PASSING: " << m_passing);

    if (key == Key_Escape) {
        // Not sure this feels good. People often hit Esc several times
        if (m_visualMode == NoVisualMode && m_mode == CommandMode)
            return false;
        return true;
    }

    // We are interested in overriding  most Ctrl key combinations
    if (mods == Qt::ControlModifier && key >= Key_A && key <= Key_Z) {
        // Ctrl-K is special as it is the Core's default notion of QuickOpen
        KEY_DEBUG(" NOT PASSING CTRL KEY");
        //updateMiniBuffer();
        return true;
    }

    // Let other shortcuts trigger
    return false;
}

bool EmacsKeysHandler::Private::exactMatch(int key, const QKeySequence& keySequence)
{
    return QKeySequence(key).matches(keySequence) == QKeySequence::ExactMatch;
}

/*
void EmacsKeysHandler::Private::yankPop(QTextEdit* view)
{
  qDebug() << "yankPop called " << endl;
  if (KillRing::instance()->currentYankView() != view) {
    qDebug() << "the last previous yank was not in this view" << endl;
    // generate beep and return
    QApplication::beep();
    return;
  }

  unsigned int l, c;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&l, &c);
  if (l != endLine || c != endColumn) {
    qDebug() << "Cursor has been moved in the meantime" << endl;
    qDebug() << "lines " << endLine << " " << l << endl;
    qDebug() << "columns " << endColumn << " " << c << endl;
    QApplication::beep();
    return;
  }

  QString next(KillRing::instance()->next());
  if (!next.isEmpty()) {
    KTextEditor::EditInterface* editor = 
      KTextEditor::editInterface(view->document());
    KTextEditor::editInterfaceExt(view->document())->editBegin();
    editor->removeText(startLine, startColumn, endLine, endColumn);
    editor->insertText(startLine, startColumn, next);
    KTextEditor::viewCursorInterface(view)->cursorPositionReal(&endLine, 
							       &endColumn);
    KTextEditor::editInterfaceExt(view->document())->editEnd();
  }
  else {
    QApplication::beep();
  }
}

void EmacsKeysHandler::Private::setMark()
{
  unsigned int line, column;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
  qDebug() << "Set mark " << line << " " << column << endl;
  ring.addMark(line, column);
}

void EmacsKeysHandler::Private::exchangeDotAndMark()
{
  unsigned int line, column;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
  ring.addMark(line, column);
  popToMark();
}

void EmacsKeysHandler::Private::popToMark()
{
  qDebug() << "pop mark" << endl;
  Mark mark(ring.getPreviousMark());
  if (mark.valid) {
    KTextEditor::viewCursorInterface(view)->setCursorPositionReal(mark.line,
								  mark.column);
  }
  else {
    QApplication::beep();
  }
}

void EmacsKeysHandler::Private::copy()
{
  qDebug() << "emacs copy" << endl;
  Mark mark(ring.getMostRecentMark());
  if (mark.valid) {
    unsigned int line, column;
    KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
    KTextEditor::selectionInterface(view->document())->setSelection
      (mark.line, mark.column, line, column);
    KTextEditor::clipboardInterface(view)->copy();
    KTextEditor::selectionInterface(view->document())->clearSelection();
    KTextEditor::viewStatusMsgInterface(view)->viewStatusMsg
      (KStringHandler::csqueeze(QApplication::clipboard()->text(), 60));
  }
  else {
    QApplication::beep();
  }
}

void EmacsKeysHandler::Private::cut()
{
  qDebug() << "emacs cut" << endl;
  Mark mark(ring.getMostRecentMark());
  if (mark.valid) {
    unsigned int line, column;
    KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
    KTextEditor::selectionInterface(view->document())->setSelection
      (mark.line, mark.column, line, column);
    KTextEditor::clipboardInterface(view)->cut();
  }
  else {
    QApplication::beep();
  }
}

void EmacsKeysHandler::Private::yank()
{
  qDebug() << "emacs yank" << endl;
  unsigned l, c;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&l, &c);
  startLine = (unsigned int)l;
  startColumn = (unsigned int)c;
  KTextEditor::clipboardInterface(view)->paste();
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&endLine,
							     &endColumn);
  if (l != endLine || c != endColumn) {
    KillRing::instance()->setCurrentYankView(view);
  }
}

void EmacsKeysHandler::Private::killLine()
{
  unsigned int line, column;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
  int length = KTextEditor::editInterface(view->document())->lineLength(line);

  // remove line break
  if (column == (unsigned int)length) {
    KTextEditor::editInterface(view->document())->removeText(line, column, 
							     line + 1, 0);
    return;
  }
  
  KTextEditor::selectionInterface(view->document())->setSelection
    (line, column, line, length);
  KTextEditor::clipboardInterface(view)->cut();
}

void EmacsKeysHandler::Private::nextLine()
{
  unsigned int line, column;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
  unsigned int lines = KTextEditor::editInterface(view->document())->numLines();
  if (++line < lines) {
    KTextEditor::viewCursorInterface(view)->setCursorPositionReal(line, column);
  }
  else {
    QApplication::beep();
  }
}

void EmacsKeysHandler::Private::previousLine()
{
  unsigned int line, column;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
  if (line > 0) {
    KTextEditor::viewCursorInterface(view)->setCursorPositionReal(--line, 
								  column);
  }
  else {
    QApplication::beep();
  }
}

void EmacsKeysHandler::Private::forwardChar()
{
  unsigned int line, column;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
  int length = KTextEditor::editInterface(view->document())->lineLength(line);
  if (column < (unsigned int)length) {
    KTextEditor::viewCursorInterface(view)->setCursorPositionReal(line, 
								  ++column);
  }
  else {
    unsigned int lines =
      KTextEditor::editInterface(view->document())->numLines();
    if (++line < lines) {
      KTextEditor::viewCursorInterface(view)->setCursorPositionReal(line, 0);
    }
    else {
      QApplication::beep();
    }
  }
}

void EmacsKeysHandler::Private::backwardChar()
{
  unsigned int line, column;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
  if (column > 0) {
    KTextEditor::viewCursorInterface(view)->setCursorPositionReal(line, 
								  --column);
  }
  else {
    KTextEditor::viewCursorInterface(view)->setCursorPositionReal(line, 0);
    previousLine();
  }
}

void EmacsKeysHandler::Private::deleteNextChar()
{
  unsigned int line, column;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&line, &column);
  int length = KTextEditor::editInterface(view->document())->lineLength(line);
  if (column == (unsigned int)length) {
    KTextEditor::editInterface(view->document())->removeText(line, column, 
							     line + 1, 0);
  }
  else {
    KTextEditor::editInterface(view->document())->removeText(line, column, line,
							     column + 1);
  }
}

void EmacsKeysHandler::Private::charactersInserted(int l, int c, const QString& text)
{
  qDebug() << "characters inserted " << text << " " << l << " " << c << endl;
  KillRing::instance()->setCurrentYankView(view);
  startLine = l;
  startColumn = c;
  KTextEditor::viewCursorInterface(view)->cursorPositionReal(&endLine, 
							     &endColumn);
}

*/

EventResult EmacsKeysHandler::Private::handleEvent(QKeyEvent *ev)
{
    int key = ev->key();
    const int um = key; // keep unmodified key around
    const int mods = ev->modifiers();
    QKeySequence keySequence(ev->key() + ev->modifiers());

    qDebug() << "sequence: " << keySequence << endl;

    if (key == Key_Shift || key == Key_Alt || key == Key_Control
            || key == Key_Alt || key == Key_AltGr || key == Key_Meta)
    {
        KEY_DEBUG("PLAIN MODIFIER");
        return EventUnhandled;
    }

    // Fake "End of line"
    m_tc = EDITOR(textCursor());

    if (m_tc.position() != m_oldTc.position())
        setTargetColumn();

    m_tc.setVisualNavigation(true);
    
    if (m_fakeEnd)
        moveRight();

    if ((mods & Qt::ControlModifier) != 0) {
        key += 256;
        key += 32; // make it lower case
    } else if (key >= Key_A && key <= Key_Z && (mods & Qt::ShiftModifier) == 0) {
        key += 32;
    }

    m_undoCursorPosition[m_tc.document()->revision()] = m_tc.position();

    EventResult result = EventHandled;
    if (exactMatch(Qt::CTRL + Qt::Key_N, keySequence)) {
        moveDown();
    } else if (exactMatch(Qt::CTRL + Qt::Key_P, keySequence)) {
        moveUp();
    } else if (exactMatch(Qt::CTRL + Qt::Key_A, keySequence)) {
        moveToStartOfLine();
    } else if (exactMatch(Qt::CTRL + Qt::Key_E, keySequence)) {
        moveToEndOfLine();
    } else if (exactMatch(Qt::CTRL + Qt::Key_B, keySequence)) {
        moveLeft();
    } else if (exactMatch(Qt::CTRL + Qt::Key_F, keySequence)) {
        moveRight();
    } else if (exactMatch(Qt::ALT + Qt::Key_B, keySequence)) {
        moveToWordBoundary(false, false);
    } else if (exactMatch(Qt::ALT + Qt::Key_F, keySequence)) {
        moveToNextWord(false);
    } else if (exactMatch(Qt::CTRL + Qt::Key_D, keySequence)) {
      m_tc.deleteChar();
    } else if (exactMatch(Qt::ALT + Qt::SHIFT + Qt::Key_Less, keySequence)) {
      m_tc.movePosition(QTextCursor::Start, QTextCursor::MoveAnchor);
    } else if (exactMatch(Qt::ALT + Qt::SHIFT + Qt::Key_Greater, keySequence)) {
      m_tc.movePosition(QTextCursor::End, QTextCursor::MoveAnchor);
    } else {
        result = handleKey(key, um, ev->text());
    }

    m_oldTc = m_tc;
    EDITOR(setTextCursor(m_tc));
    return result;
}

void EmacsKeysHandler::Private::installEventFilter()
{
    EDITOR(installEventFilter(q));
}

void EmacsKeysHandler::Private::setupWidget()
{
    enterCommandMode();
    //EDITOR(setCursorWidth(QFontMetrics(ed->font()).width(QChar('x')));
    if (m_textedit) {
        m_textedit->setLineWrapMode(QTextEdit::NoWrap);
    } else if (m_plaintextedit) {
        m_plaintextedit->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
    m_wasReadOnly = EDITOR(isReadOnly());
    //EDITOR(setReadOnly(true));

    QTextCursor tc = EDITOR(textCursor());
    if (tc.hasSelection()) {
        int pos = tc.position();
        int anc = tc.anchor();
        m_marks['<'] = anc;
        m_marks['>'] = pos;
        m_anchor = anc;
        m_visualMode = VisualCharMode;
        tc.clearSelection();
        EDITOR(setTextCursor(tc));
        m_tc = tc; // needed in updateSelection
        updateSelection();
    }

    //showBlackMessage("vi emulation mode. Type :q to leave. Use , Ctrl-R to trigger run.");
    updateMiniBuffer();
}

void EmacsKeysHandler::Private::restoreWidget()
{
    //showBlackMessage(QString());
    //updateMiniBuffer();
    //EDITOR(removeEventFilter(q));
    EDITOR(setReadOnly(m_wasReadOnly));
    EDITOR(setCursorWidth(m_cursorWidth));
    EDITOR(setOverwriteMode(false));

    if (m_visualMode == VisualLineMode) {
        m_tc = EDITOR(textCursor());
        int beginLine = lineForPosition(m_marks['<']);
        int endLine = lineForPosition(m_marks['>']);
        m_tc.setPosition(firstPositionInLine(beginLine), MoveAnchor);
        m_tc.setPosition(lastPositionInLine(endLine), KeepAnchor);
        EDITOR(setTextCursor(m_tc));
    } else if (m_visualMode == VisualCharMode) {
        m_tc = EDITOR(textCursor());
        m_tc.setPosition(m_marks['<'], MoveAnchor);
        m_tc.setPosition(m_marks['>'], KeepAnchor);
        EDITOR(setTextCursor(m_tc));
    }

    m_visualMode = NoVisualMode;
    updateSelection();
}

EventResult EmacsKeysHandler::Private::handleKey(int key, int unmodified,
    const QString &text)
{
    qDebug() << "KEY: " << key << text << "POS: " << m_tc.position();
    if (m_mode == InsertMode)
        return handleInsertMode(key, unmodified, text);
    if (m_mode == CommandMode)
        return handleCommandMode(key, unmodified, text);
    if (m_mode == ExMode || m_mode == SearchForwardMode
            || m_mode == SearchBackwardMode)
        return handleMiniBufferModes(key, unmodified, text);
    return EventUnhandled;
}

void EmacsKeysHandler::Private::moveDown(int n)
{
#if 0
    // does not work for "hidden" documents like in the autotests
    m_tc.movePosition(Down, MoveAnchor, n);
#else
    const int col = m_tc.position() - m_tc.block().position();
    const int lastLine = m_tc.document()->lastBlock().blockNumber();
    const int targetLine = qMax(0, qMin(lastLine, m_tc.block().blockNumber() + n));
    const QTextBlock &block = m_tc.document()->findBlockByNumber(targetLine);
    const int pos = block.position();
    setPosition(pos + qMin(block.length() - 1, col));
    moveToTargetColumn();
#endif
}

void EmacsKeysHandler::Private::moveToEndOfLine()
{
#if 0
    // does not work for "hidden" documents like in the autotests
    m_tc.movePosition(EndOfLine, MoveAnchor);
#else
    const QTextBlock &block = m_tc.block();
    setPosition(block.position() + block.length() - 1);
#endif
}

void EmacsKeysHandler::Private::moveToStartOfLine()
{
#if 0
    // does not work for "hidden" documents like in the autotests
    m_tc.movePosition(StartOfLine, MoveAnchor);
#else
    const QTextBlock &block = m_tc.block();
    setPosition(block.position());
#endif
}

void EmacsKeysHandler::Private::finishMovement(const QString &dotCommand)
{
    //qDebug() << "ANCHOR: " << position() << anchor();
    if (m_submode == FilterSubMode) {
        int beginLine = lineForPosition(anchor());
        int endLine = lineForPosition(position());
        setPosition(qMin(anchor(), position()));
        enterExMode();
        m_currentMessage.clear();
        m_commandBuffer = QString(".,+%1!").arg(qAbs(endLine - beginLine));
        m_commandHistory.append(QString());
        m_commandHistoryIndex = m_commandHistory.size() - 1;
        updateMiniBuffer();
        return;
    }

    if (m_visualMode != NoVisualMode)
        m_marks['>'] = m_tc.position();

    if (m_submode == ChangeSubMode) {
        if (m_moveType == MoveInclusive)
            moveRight(); // correction
        if (anchor() >= position())
           m_anchor++;
        if (!dotCommand.isEmpty())
            setDotCommand("c" + dotCommand);
        QString text = removeSelectedText();
        //qDebug() << "CHANGING TO INSERT MODE" << text;
        m_registers[m_register] = text;
        enterInsertMode();
        m_submode = NoSubMode;
    } else if (m_submode == DeleteSubMode) {
        if (m_moveType == MoveInclusive)
            moveRight(); // correction
        if (anchor() >= position())
           m_anchor++;
        if (!dotCommand.isEmpty())
            setDotCommand("d" + dotCommand);
        m_registers[m_register] = removeSelectedText();
        m_submode = NoSubMode;
        if (atEndOfLine())
            moveLeft();
        else
            setTargetColumn();
    } else if (m_submode == YankSubMode) {
        m_registers[m_register] = selectedText();
        setPosition(m_savedYankPosition);
        m_submode = NoSubMode;
    } else if (m_submode == ReplaceSubMode) {
        m_submode = NoSubMode;
    } else if (m_submode == IndentSubMode) {
        recordJump();
        indentRegion();
        m_submode = NoSubMode;
        updateMiniBuffer();
    } else if (m_submode == ShiftRightSubMode) {
        recordJump();
        shiftRegionRight(1);
        m_submode = NoSubMode;
        updateMiniBuffer();
    } else if (m_submode == ShiftLeftSubMode) {
        recordJump();
        shiftRegionLeft(1);
        m_submode = NoSubMode;
        updateMiniBuffer();
    }

    m_moveType = MoveInclusive;
    m_mvcount.clear();
    m_opcount.clear();
    m_gflag = false;
    m_register = '"';
    m_tc.clearSelection();

    updateSelection();
    updateMiniBuffer();
}

void EmacsKeysHandler::Private::updateSelection()
{
    QList<QTextEdit::ExtraSelection> selections = m_searchSelections;
    if (m_visualMode != NoVisualMode) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = m_tc;
        sel.format = m_tc.blockCharFormat();
#if 0
        sel.format.setFontWeight(QFont::Bold);
        sel.format.setFontUnderline(true);
#else
        sel.format.setForeground(Qt::white);
        sel.format.setBackground(Qt::black);
#endif
        int cursorPos = m_tc.position();
        int anchorPos = m_marks['<'];
        //qDebug() << "POS: " << cursorPos << " ANCHOR: " << anchorPos;
        if (m_visualMode == VisualCharMode) {
            sel.cursor.setPosition(anchorPos, KeepAnchor);
            selections.append(sel);
        } else if (m_visualMode == VisualLineMode) {
            sel.cursor.setPosition(qMin(cursorPos, anchorPos), MoveAnchor);
            sel.cursor.movePosition(StartOfLine, MoveAnchor);
            sel.cursor.setPosition(qMax(cursorPos, anchorPos), KeepAnchor);
            sel.cursor.movePosition(EndOfLine, KeepAnchor);
            selections.append(sel);
        } else if (m_visualMode == VisualBlockMode) {
            QTextCursor tc = m_tc;
            tc.setPosition(anchorPos);
            tc.movePosition(StartOfLine, MoveAnchor);
            QTextBlock anchorBlock = tc.block();
            QTextBlock cursorBlock = m_tc.block();
            int anchorColumn = anchorPos - anchorBlock.position();
            int cursorColumn = cursorPos - cursorBlock.position();
            int startColumn = qMin(anchorColumn, cursorColumn);
            int endColumn = qMax(anchorColumn, cursorColumn);
            int endPos = cursorBlock.position();
            while (tc.position() <= endPos) {
                if (startColumn < tc.block().length() - 1) {
                    int last = qMin(tc.block().length() - 1, endColumn);
                    int len = last - startColumn + 1;
                    sel.cursor = tc;
                    sel.cursor.movePosition(Right, MoveAnchor, startColumn);
                    sel.cursor.movePosition(Right, KeepAnchor, len);
                    selections.append(sel);
                }
                tc.movePosition(Down, MoveAnchor, 1);
            }
        }
    }
    //qDebug() << "SELECTION: " << selections;
    emit q->selectionChanged(selections);
}

void EmacsKeysHandler::Private::updateMiniBuffer()
{
    QString msg;
    if (m_passing) {
        msg = "-- PASSING --  ";
    } else if (!m_currentMessage.isEmpty()) {
        msg = m_currentMessage;
    } else if (m_mode == CommandMode && m_visualMode != NoVisualMode) {
        if (m_visualMode == VisualCharMode) {
            msg = "-- VISUAL --";
        } else if (m_visualMode == VisualLineMode) {
            msg = "-- VISUAL LINE --";
        } else if (m_visualMode == VisualBlockMode) {
            msg = "-- VISUAL BLOCK --";
        }
    } else if (m_mode == InsertMode) {
        if (m_submode == ReplaceSubMode)
            msg = "-- REPLACE --";
        else
            msg = "-- INSERT --";
    } else {
        if (m_mode == SearchForwardMode)
            msg += '/';
        else if (m_mode == SearchBackwardMode)
            msg += '?';
        else if (m_mode == ExMode)
            msg += ':';
        foreach (QChar c, m_commandBuffer) {
            if (c.unicode() < 32) {
                msg += '^';
                msg += QChar(c.unicode() + 64);
            } else {
                msg += c;
            }
        }
        if (!msg.isEmpty() && m_mode != CommandMode)
            msg += QChar(10073); // '|'; // FIXME: Use a real "cursor"
    }

    emit q->commandBufferChanged(msg);

    int linesInDoc = linesInDocument();
    int l = cursorLineInDocument();
    QString status;
    QString pos = tr("%1,%2").arg(l + 1).arg(cursorColumnInDocument() + 1);
    status += tr("%1").arg(pos, -10);
    // FIXME: physical "-" logical
    if (linesInDoc != 0) {
        status += tr("%1").arg(l * 100 / linesInDoc, 4);
        status += "%";
    } else {
        status += "All";
    }
    emit q->statusDataChanged(status);
}

void EmacsKeysHandler::Private::showRedMessage(const QString &msg)
{
    //qDebug() << "MSG: " << msg;
    m_currentMessage = msg;
    updateMiniBuffer();
}

void EmacsKeysHandler::Private::showBlackMessage(const QString &msg)
{
    //qDebug() << "MSG: " << msg;
    m_commandBuffer = msg;
    updateMiniBuffer();
}

void EmacsKeysHandler::Private::notImplementedYet()
{
    qDebug() << "Not implemented in EmacsKeys";
    showRedMessage(tr("Not implemented in EmacsKeys"));
    updateMiniBuffer();
}

EventResult EmacsKeysHandler::Private::handleCommandMode(int key, int unmodified,
    const QString &text)
{
    EventResult handled = EventHandled;

    if (m_submode == WindowSubMode) {
        emit q->windowCommandRequested(key);
        m_submode = NoSubMode;
    } else if (m_submode == RegisterSubMode) {
        m_register = key;
        m_submode = NoSubMode;
    } else if (m_submode == ChangeSubMode && key == 'c') { // tested
        moveDown(count() - 1);
        moveToEndOfLine();
        moveLeft();
        setAnchor();
        moveToStartOfLine();
        setTargetColumn();
        moveUp(count() - 1);
        m_moveType = MoveLineWise;
        m_lastInsertion.clear();
        setDotCommand("%1cc", count());
        finishMovement();
    } else if (m_submode == DeleteSubMode && key == 'd') { // tested
        moveToStartOfLine();
        setTargetColumn(); 
        setAnchor();
        moveDown(count());
        m_moveType = MoveLineWise;
        setDotCommand("%1dd", count());
        finishMovement();
    } else if (m_submode == YankSubMode && key == 'y') {
        moveToStartOfLine();
        setAnchor();
        moveDown(count());
        m_moveType = MoveLineWise;
        finishMovement("y");
    } else if (m_submode == ShiftLeftSubMode && key == '<') {
        setAnchor();
        moveDown(count() - 1);
        m_moveType = MoveLineWise;
        setDotCommand("%1<<", count());
        finishMovement();
    } else if (m_submode == ShiftRightSubMode && key == '>') {
        setAnchor();
        moveDown(count() - 1);
        m_moveType = MoveLineWise;
        setDotCommand("%1>>", count());
        finishMovement();
    } else if (m_submode == IndentSubMode && key == '=') {
        setAnchor();
        moveDown(count() - 1);
        m_moveType = MoveLineWise;
        setDotCommand("%1==", count());
        finishMovement();
    } else if (m_submode == ZSubMode) {
        //qDebug() << "Z_MODE " << cursorLineInDocument() << linesOnScreen();
        if (key == Key_Return || key == 't') { // cursor line to top of window
            if (!m_mvcount.isEmpty())
                setPosition(firstPositionInLine(count()));
            scrollUp(- cursorLineOnScreen());
            if (key == Key_Return)
                moveToFirstNonBlankOnLine();
            finishMovement();
        } else if (key == '.' || key == 'z') { // cursor line to center of window
            if (!m_mvcount.isEmpty())
                setPosition(firstPositionInLine(count()));
            scrollUp(linesOnScreen() / 2 - cursorLineOnScreen());
            if (key == '.')
                moveToFirstNonBlankOnLine();
            finishMovement();
        } else if (key == '-' || key == 'b') { // cursor line to bottom of window
            if (!m_mvcount.isEmpty())
                setPosition(firstPositionInLine(count()));
            scrollUp(linesOnScreen() - cursorLineOnScreen());
            if (key == '-')
                moveToFirstNonBlankOnLine();
            finishMovement();
        } else {
            qDebug() << "IGNORED Z_MODE " << key << text;
        }
        m_submode = NoSubMode;
    } else if (m_submode == CapitalZSubMode) {
        // Recognize ZZ and ZQ as aliases for ":x" and ":q!".
        m_submode = NoSubMode;
        if (key == 'Z')
            handleCommand("x");
        else if (key == 'Q')
            handleCommand("q!");
    } else if (m_subsubmode == FtSubSubMode) {
        m_semicolonType = m_subsubdata;
        m_semicolonKey = key;
        handleFfTt(key);
        m_subsubmode = NoSubSubMode;
        finishMovement(QString("%1%2%3")
            .arg(count())
            .arg(QChar(m_semicolonType))
            .arg(QChar(m_semicolonKey)));
    } else if (m_submode == ReplaceSubMode) {
        if (count() <= (rightDist() + atEndOfLine()) && text.size() == 1
                && (text.at(0).isPrint() || text.at(0).isSpace())) {
            if (atEndOfLine())
                moveLeft();
            setAnchor();
            moveRight(count());
            QString rem = removeSelectedText();
            m_tc.insertText(QString(count(), text.at(0)));
            m_moveType = MoveExclusive;
            setDotCommand("%1r" + text, count());
        }
        setTargetColumn();
        m_submode = NoSubMode;
        finishMovement();
    } else if (m_subsubmode == MarkSubSubMode) {
        m_marks[key] = m_tc.position();
        m_subsubmode = NoSubSubMode;
    } else if (m_subsubmode == BackTickSubSubMode
            || m_subsubmode == TickSubSubMode) {
        if (m_marks.contains(key)) {
            setPosition(m_marks[key]);
            if (m_subsubmode == TickSubSubMode)
                moveToFirstNonBlankOnLine();
            finishMovement();
        } else {
            showRedMessage(tr("E20: Mark '%1' not set").arg(text));
        }
        m_subsubmode = NoSubSubMode;
    } else if (key >= '0' && key <= '9') {
        if (key == '0' && m_mvcount.isEmpty()) {
            moveToStartOfLine();
            setTargetColumn();
            finishMovement();
        } else {
            m_mvcount.append(QChar(key));
        }
    } else if (key == '^') {
        moveToFirstNonBlankOnLine();
        finishMovement();
    } else if (0 && key == ',') {
        // FIXME: emacsKeys uses ',' by itself, so it is incompatible
        m_subsubmode = FtSubSubMode;
        // HACK: toggle 'f' <-> 'F', 't' <-> 'T'
        m_subsubdata = m_semicolonType ^ 32;
        handleFfTt(m_semicolonKey);
        m_subsubmode = NoSubSubMode;
        finishMovement();
    } else if (key == ';') {
        m_subsubmode = FtSubSubMode;
        m_subsubdata = m_semicolonType;
        handleFfTt(m_semicolonKey);
        m_subsubmode = NoSubSubMode;
        finishMovement();
    } else if (key == ':') {
        enterExMode();
        m_currentMessage.clear();
        m_commandBuffer.clear();
        if (m_visualMode != NoVisualMode)
            m_commandBuffer = "'<,'>";
        m_commandHistory.append(QString());
        m_commandHistoryIndex = m_commandHistory.size() - 1;
        updateMiniBuffer();
    } else if (key == '/' || key == '?') {
        if (hasConfig(ConfigIncSearch)) {
            // re-use the core dialog.
            emit q->findRequested(key == '?');
        } else {
            // FIXME: make core find dialog sufficiently flexible to
            // produce the "default vi" behaviour too. For now, roll our own.
            enterExMode(); // to get the cursor disabled
            m_currentMessage.clear();
            m_mode = (key == '/') ? SearchForwardMode : SearchBackwardMode;
            m_commandBuffer.clear();
            m_searchHistory.append(QString());
            m_searchHistoryIndex = m_searchHistory.size() - 1;
            updateMiniBuffer();
        }
    } else if (key == '`') {
        m_subsubmode = BackTickSubSubMode;
    } else if (key == '#' || key == '*') {
        // FIXME: That's not proper vim behaviour
        m_tc.select(QTextCursor::WordUnderCursor);
        QString needle = "\\<" + m_tc.selection().toPlainText() + "\\>";
        m_searchHistory.append(needle);
        m_lastSearchForward = (key == '*');
        updateMiniBuffer();
        search(needle, m_lastSearchForward);
        recordJump();
    } else if (key == '\'') {
        m_subsubmode = TickSubSubMode;
    } else if (key == '|') {
        moveToStartOfLine();
        moveRight(qMin(count(), rightDist()) - 1);
        setTargetColumn();
        finishMovement();
    } else if (key == '!' && m_visualMode == NoVisualMode) {
        m_submode = FilterSubMode;
    } else if (key == '!' && m_visualMode != NoVisualMode) {
        enterExMode();
        m_currentMessage.clear();
        m_commandBuffer = "'<,'>!";
        m_commandHistory.append(QString());
        m_commandHistoryIndex = m_commandHistory.size() - 1;
        updateMiniBuffer();
    } else if (key == '"') {
        m_submode = RegisterSubMode;
    } else if (unmodified == Key_Return) {
        moveToStartOfLine();
        moveDown();
        moveToFirstNonBlankOnLine();
        finishMovement();
    } else if (key == '-') {
        moveToStartOfLine();
        moveUp();
        moveToFirstNonBlankOnLine();
        finishMovement();
    } else if (key == Key_Home) {
        moveToStartOfLine();
        setTargetColumn();
        finishMovement();
    } else if (key == '$' || key == Key_End) {
        int submode = m_submode;
        moveToEndOfLine();
        m_moveType = MoveExclusive;
        setTargetColumn();
        if (submode == NoSubMode)
            m_targetColumn = -1;
        finishMovement("$");
    } else if (key == ',') {
        // FIXME: use some other mechanism
        //m_passing = true;
        m_passing = !m_passing;
        updateMiniBuffer();
    } else if (key == '.') {
        qDebug() << "REPEATING" << quoteUnprintable(m_dotCommand);
        QString savedCommand = m_dotCommand;
        m_dotCommand.clear();
        replay(savedCommand, count());
        enterCommandMode();
        m_dotCommand = savedCommand;
    } else if (key == '<' && m_visualMode == NoVisualMode) {
        m_submode = ShiftLeftSubMode;
    } else if (key == '<' && m_visualMode != NoVisualMode) {
        shiftRegionLeft(1);
        leaveVisualMode();
    } else if (key == '>' && m_visualMode == NoVisualMode) {
        m_submode = ShiftRightSubMode;
    } else if (key == '>' && m_visualMode != NoVisualMode) {
        shiftRegionRight(1);
        leaveVisualMode();
    } else if (key == '=' && m_visualMode == NoVisualMode) {
        m_submode = IndentSubMode;
    } else if (key == '=' && m_visualMode != NoVisualMode) {
        indentRegion();
        leaveVisualMode();
    } else if (key == '%') {
        m_moveType = MoveExclusive;
        moveToMatchingParanthesis();
        finishMovement();
    } else if (key == 'a') {
        enterInsertMode();
        m_lastInsertion.clear();
        if (!atEndOfLine())
            moveRight();
        updateMiniBuffer();
    } else if (key == 'A') {
        enterInsertMode();
        moveToEndOfLine();
        setDotCommand("A");
        m_lastInsertion.clear();
    } else if (key == control('a')) {
        // FIXME: eat it to prevent the global "select all" shortcut to trigger
    } else if (key == 'b') {
        m_moveType = MoveExclusive;
        moveToWordBoundary(false, false);
        finishMovement();
    } else if (key == 'B') {
        m_moveType = MoveExclusive;
        moveToWordBoundary(true, false);
        finishMovement();
    } else if (key == 'c' && m_visualMode == NoVisualMode) {
        setAnchor();
        m_submode = ChangeSubMode;
    } else if (key == 'c' && m_visualMode == VisualCharMode) {
        leaveVisualMode();
        m_submode = ChangeSubMode;
        finishMovement();
    } else if (key == 'C') {
        setAnchor();
        moveToEndOfLine();
        m_registers[m_register] = removeSelectedText();
        enterInsertMode();
        setDotCommand("C");
        finishMovement();
    } else if (key == control('c')) {
        showBlackMessage("Type Alt-v,Alt-v  to quit EmacsKeys mode");
    } else if (key == 'd' && m_visualMode == NoVisualMode) {
        if (atEndOfLine())
            moveLeft();
        setAnchor();
        m_opcount = m_mvcount;
        m_mvcount.clear();
        m_submode = DeleteSubMode;
    } else if ((key == 'd' || key == 'x') && m_visualMode == VisualCharMode) {
        leaveVisualMode();
        m_submode = DeleteSubMode;
        finishMovement();
    } else if ((key == 'd' || key == 'x') && m_visualMode == VisualLineMode) {
        leaveVisualMode();
        int beginLine = lineForPosition(m_marks['<']);
        int endLine = lineForPosition(m_marks['>']);
        selectRange(beginLine, endLine);
        m_registers[m_register] = removeSelectedText();
    } else if (key == 'D') {
        setAnchor();
        m_submode = DeleteSubMode;
        moveDown(qMax(count() - 1, 0));
        m_moveType = MoveExclusive;
        moveToEndOfLine();
        setDotCommand("D");
        finishMovement();
    } else if (key == control('d')) {
        int sline = cursorLineOnScreen();
        // FIXME: this should use the "scroll" option, and "count"
        moveDown(linesOnScreen() / 2);
        handleStartOfLine();
        scrollToLineInDocument(cursorLineInDocument() - sline);
        finishMovement();
    } else if (key == 'e') { // tested
        m_moveType = MoveInclusive;
        moveToWordBoundary(false, true);
        finishMovement();
    } else if (key == 'E') {
        m_moveType = MoveInclusive;
        moveToWordBoundary(true, true);
        finishMovement();
    } else if (key == control('e')) {
        // FIXME: this should use the "scroll" option, and "count"
        if (cursorLineOnScreen() == 0)
            moveDown(1);
        scrollDown(1);
        finishMovement();
    } else if (key == 'f') {
        m_subsubmode = FtSubSubMode;
        m_moveType = MoveInclusive;
        m_subsubdata = key;
    } else if (key == 'F') {
        m_subsubmode = FtSubSubMode;
        m_moveType = MoveExclusive;
        m_subsubdata = key;
    } else if (key == 'g') {
        if (m_gflag) {
            m_gflag = false;
            m_tc.setPosition(firstPositionInLine(1), KeepAnchor);
            handleStartOfLine();
            finishMovement();
        } else {
            m_gflag = true;
        }
    } else if (key == 'G') {
        int n = m_mvcount.isEmpty() ? linesInDocument() : count();
        m_tc.setPosition(firstPositionInLine(n), KeepAnchor);
        handleStartOfLine();
        finishMovement();
    } else if (key == 'h' || key == Key_Left
            || key == Key_Backspace || key == control('h')) {
        int n = qMin(count(), leftDist());
        if (m_fakeEnd && m_tc.block().length() > 1)
            ++n;
        moveLeft(n);
        setTargetColumn();
        finishMovement("h");
    } else if (key == 'H') {
        m_tc = EDITOR(cursorForPosition(QPoint(0, 0)));
        moveDown(qMax(count() - 1, 0));
        handleStartOfLine();
        finishMovement();
    } else if (key == 'i' || key == Key_Insert) {
        setDotCommand("i"); // setDotCommand("%1i", count());
        enterInsertMode();
        updateMiniBuffer();
        if (atEndOfLine())
            moveLeft();
    } else if (key == 'I') {
        setDotCommand("I"); // setDotCommand("%1I", count());
        enterInsertMode();
        if (m_gflag)
            moveToStartOfLine();
        else
            moveToFirstNonBlankOnLine();
        m_tc.clearSelection();
    } else if (key == control('i')) {
        if (!m_jumpListRedo.isEmpty()) {
            m_jumpListUndo.append(position());
            setPosition(m_jumpListRedo.takeLast());
        }
    } else if (key == 'j' || key == Key_Down) {
        if (m_submode == NoSubMode || m_submode == ZSubMode
                || m_submode == CapitalZSubMode || m_submode == RegisterSubMode) {
            moveDown(count());
        } else {
            m_moveType = MoveLineWise;
            moveToStartOfLine();
            setAnchor();
            moveDown(count() + 1);
        }
        finishMovement("j");
    } else if (key == 'J') {
        if (m_submode == NoSubMode) {
            for (int i = qMax(count(), 2) - 1; --i >= 0; ) {
                moveToEndOfLine();
                setAnchor();
                moveRight();
                while (characterAtCursor() == ' ')
                    moveRight();
                removeSelectedText();
                if (!m_gflag)
                    m_tc.insertText(" ");
            }
            if (!m_gflag)
                moveLeft();
        }
    } else if (key == 'k' || key == Key_Up) {
        if (m_submode == NoSubMode || m_submode == ZSubMode
                || m_submode == CapitalZSubMode || m_submode == RegisterSubMode) {
            moveUp(count());
        } else {
            m_moveType = MoveLineWise;
            moveToStartOfLine();
            moveDown();
            setAnchor();
            moveUp(count() + 1);
        }
        finishMovement("k");
    } else if (key == 'l' || key == Key_Right || key == ' ') {
        m_moveType = MoveExclusive;
        moveRight(qMin(count(), rightDist()));
        setTargetColumn();
        finishMovement("l");
    } else if (key == 'L') {
        m_tc = EDITOR(cursorForPosition(QPoint(0, EDITOR(height()))));
        moveUp(qMax(count(), 1));
        handleStartOfLine();
        finishMovement();
    } else if (key == control('l')) {
        // screen redraw. should not be needed
    } else if (key == 'm') {
        m_subsubmode = MarkSubSubMode;
    } else if (key == 'M') {
        m_tc = EDITOR(cursorForPosition(QPoint(0, EDITOR(height()) / 2)));
        handleStartOfLine();
        finishMovement();
    } else if (key == 'n') { // FIXME: see comment for '/'
        if (hasConfig(ConfigIncSearch))
            emit q->findNextRequested(false);
        else
            search(lastSearchString(), m_lastSearchForward);
        recordJump();
    } else if (key == 'N') {
        if (hasConfig(ConfigIncSearch))
            emit q->findNextRequested(true);
        else
            search(lastSearchString(), !m_lastSearchForward);
        recordJump();
    } else if (key == 'o' || key == 'O') {
        setDotCommand("%1o", count());
        enterInsertMode();
        moveToFirstNonBlankOnLine();
        if (key == 'O')
            moveUp();
        moveToEndOfLine();
        m_tc.insertText("\n");
        insertAutomaticIndentation(key == 'o');
    } else if (key == control('o')) {
        if (!m_jumpListUndo.isEmpty()) {
            m_jumpListRedo.append(position());
            setPosition(m_jumpListUndo.takeLast());
        }
    } else if (key == 'p' || key == 'P') {
        QString text = m_registers[m_register];
        int n = text.count(QChar('\n'));
        //qDebug() << "REGISTERS: " << m_registers << "MOVE: " << m_moveType;
        //qDebug() << "LINES: " << n << text << m_register;
        if (n > 0) {
            moveToStartOfLine();
            m_targetColumn = 0;
            for (int i = count(); --i >= 0; ) {
                if (key == 'p')
                    moveDown();
                m_tc.insertText(text);
                moveUp(n);
            }
            moveToFirstNonBlankOnLine();
        } else {
            m_targetColumn = 0;
            for (int i = count(); --i >= 0; ) {
                if (key == 'p')
                    moveRight();
                m_tc.insertText(text);
                moveLeft();
            }
        }
        setDotCommand("%1p", count());
        finishMovement();
    } else if (key == 'r') {
        m_submode = ReplaceSubMode;
        setDotCommand("r");
    } else if (key == 'R') {
        // FIXME: right now we repeat the insertion count() times,
        // but not the deletion
        m_lastInsertion.clear();
        enterInsertMode();
        m_submode = ReplaceSubMode;
        setDotCommand("R");
    } else if (key == control('r')) {
        redo();
    } else if (key == 's') {
        if (atEndOfLine())
            moveLeft();
        setAnchor();
        moveRight(qMin(count(), rightDist()));
        m_registers[m_register] = removeSelectedText();
        setDotCommand("s"); // setDotCommand("%1s", count());
        m_opcount.clear();
        m_mvcount.clear();
        enterInsertMode();
    } else if (key == 't') {
        m_moveType = MoveInclusive;
        m_subsubmode = FtSubSubMode;
        m_subsubdata = key;
    } else if (key == 'T') {
        m_moveType = MoveExclusive;
        m_subsubmode = FtSubSubMode;
        m_subsubdata = key;
    } else if (key == 'u') {
        undo();
    } else if (key == control('u')) {
        int sline = cursorLineOnScreen();
        // FIXME: this should use the "scroll" option, and "count"
        moveUp(linesOnScreen() / 2);
        handleStartOfLine();
        scrollToLineInDocument(cursorLineInDocument() - sline);
        finishMovement();
    } else if (key == 'v') {
        enterVisualMode(VisualCharMode);
    } else if (key == 'V') {
        enterVisualMode(VisualLineMode);
    } else if (key == control('v')) {
        enterVisualMode(VisualBlockMode);
    } else if (key == 'w') { // tested
        // Special case: "cw" and "cW" work the same as "ce" and "cE" if the
        // cursor is on a non-blank.
        if (m_submode == ChangeSubMode) {
            moveToWordBoundary(false, true);
            m_moveType = MoveInclusive;
        } else {
            moveToNextWord(false);
            m_moveType = MoveExclusive;
        }
        finishMovement("w");
    } else if (key == 'W') {
        if (m_submode == ChangeSubMode) {
            moveToWordBoundary(true, true);
            m_moveType = MoveInclusive;
        } else {
            moveToNextWord(true);
            m_moveType = MoveExclusive;
        }
        finishMovement("W");
    } else if (key == control('w')) {
        m_submode = WindowSubMode;
    } else if (key == 'x' && m_visualMode == NoVisualMode) { // = "dl"
        m_moveType = MoveExclusive;
        if (atEndOfLine())
            moveLeft();
        setAnchor();
        m_submode = DeleteSubMode;
        moveRight(qMin(count(), rightDist()));
        setDotCommand("%1x", count());
        finishMovement();
    } else if (key == 'X') {
        if (leftDist() > 0) {
            setAnchor();
            moveLeft(qMin(count(), leftDist()));
            m_registers[m_register] = removeSelectedText();
        }
        finishMovement();
    } else if (key == 'y' && m_visualMode == NoVisualMode) {
        m_savedYankPosition = m_tc.position();
        if (atEndOfLine())
            moveLeft();
        setAnchor();
        m_submode = YankSubMode;
    } else if (key == 'y' && m_visualMode == VisualLineMode) {
        int beginLine = lineForPosition(m_marks['<']);
        int endLine = lineForPosition(m_marks['>']);
        selectRange(beginLine, endLine);
        m_registers[m_register] = selectedText();
        setPosition(qMin(position(), anchor()));
        moveToStartOfLine();
        leaveVisualMode();
        updateSelection();
    } else if (key == 'Y') {
        moveToStartOfLine();
        setAnchor();
        moveDown(count());
        m_moveType = MoveLineWise;
        finishMovement();
    } else if (key == 'z') {
        m_submode = ZSubMode;
    } else if (key == 'Z') {
        m_submode = CapitalZSubMode;
    } else if (key == '~' && !atEndOfLine()) {
        setAnchor();
        moveRight(qMin(count(), rightDist()));
        QString str = removeSelectedText();
        for (int i = str.size(); --i >= 0; ) {
            QChar c = str.at(i);
            str[i] = c.isUpper() ? c.toLower() : c.toUpper();
        }
        m_tc.insertText(str);
    } else if (key == Key_PageDown || key == control('f')) {
        moveDown(count() * (linesOnScreen() - 2) - cursorLineOnScreen());
        scrollToLineInDocument(cursorLineInDocument());
        handleStartOfLine();
        finishMovement();
    } else if (key == Key_PageUp || key == control('b')) {
        moveUp(count() * (linesOnScreen() - 2) + cursorLineOnScreen());
        scrollToLineInDocument(cursorLineInDocument() + linesOnScreen() - 2);
        handleStartOfLine();
        finishMovement();
    } else if (key == Key_Delete) {
        setAnchor();
        moveRight(qMin(1, rightDist()));
        removeSelectedText();
    } else if (key == Key_Escape) {
        if (m_visualMode != NoVisualMode) {
            leaveVisualMode();
        } else if (m_submode != NoSubMode) {
            m_submode = NoSubMode;
            m_subsubmode = NoSubSubMode;
            finishMovement();
        }
    } else {
        qDebug() << "IGNORED IN COMMAND MODE: " << key << text
            << " VISUAL: " << m_visualMode;
        handled = EventUnhandled;
    }

    return handled;
}

EventResult EmacsKeysHandler::Private::handleInsertMode(int key, int,
    const QString &text)
{
    if (key == Key_Escape || key == 27 || key == control('c')) {
        // start with '1', as one instance was already physically inserted
        // while typing
        QString data = m_lastInsertion;
        for (int i = 1; i < count(); ++i) {
            m_tc.insertText(m_lastInsertion);
            data += m_lastInsertion;
        }
        moveLeft(qMin(1, leftDist()));
        setTargetColumn();
        m_dotCommand += m_lastInsertion;
        m_dotCommand += QChar(27);
        recordNewUndo();
        enterCommandMode();
    } else if (key == Key_Insert) {
        if (m_submode == ReplaceSubMode) {
            EDITOR(setCursorWidth(m_cursorWidth));
            EDITOR(setOverwriteMode(false));
            m_submode = NoSubMode;
        } else {
            EDITOR(setCursorWidth(m_cursorWidth));
            EDITOR(setOverwriteMode(true));
            m_submode = ReplaceSubMode;
        }
    } else if (key == Key_Left) {
        moveLeft(count());
        m_lastInsertion.clear();
    } else if (key == Key_Down) {
        removeAutomaticIndentation();
        m_submode = NoSubMode;
        moveDown(count());
        m_lastInsertion.clear();
    } else if (key == Key_Up) {
        removeAutomaticIndentation();
        m_submode = NoSubMode;
        moveUp(count());
        m_lastInsertion.clear();
    } else if (key == Key_Right) {
        moveRight(count());
        m_lastInsertion.clear();
    } else if (key == Key_Return) {
        m_submode = NoSubMode;
        m_tc.insertBlock();
        m_lastInsertion += "\n";
        insertAutomaticIndentation(true);
    } else if (key == Key_Backspace || key == control('h')) {
        if (!removeAutomaticIndentation()) 
            if (!m_lastInsertion.isEmpty() || hasConfig(ConfigBackspace, "start")) {
                m_tc.deletePreviousChar();
                m_lastInsertion.chop(1);
            }
    } else if (key == Key_Delete) {
        m_tc.deleteChar();
        m_lastInsertion.clear();
    } else if (key == Key_PageDown || key == control('f')) {
        removeAutomaticIndentation();
        moveDown(count() * (linesOnScreen() - 2));
        m_lastInsertion.clear();
    } else if (key == Key_PageUp || key == control('b')) {
        removeAutomaticIndentation();
        moveUp(count() * (linesOnScreen() - 2));
        m_lastInsertion.clear();
    } else if (key == Key_Tab && hasConfig(ConfigExpandTab)) {
        QString str = QString(theEmacsKeysSetting(ConfigTabStop)->value().toInt(), ' ');
        m_lastInsertion.append(str);
        m_tc.insertText(str);
    } else if (key >= control('a') && key <= control('z')) {
        // ignore these
    } else if (!text.isEmpty()) {
        m_lastInsertion.append(text);
        if (m_submode == ReplaceSubMode) {
            if (atEndOfLine())
                m_submode = NoSubMode;
            else
                m_tc.deleteChar();
        }
        m_tc.insertText(text);
        if (0 && hasConfig(ConfigAutoIndent) && isElectricCharacter(text.at(0))) {
            const QString leftText = m_tc.block().text()
                .left(m_tc.position() - 1 - m_tc.block().position());
            if (leftText.simplified().isEmpty())
                indentRegion(text.at(0));
        }

        if (!m_inReplay)
            emit q->completionRequested();
    } else {
        return EventUnhandled;
    }
    updateMiniBuffer();
    return EventHandled;
}

EventResult EmacsKeysHandler::Private::handleMiniBufferModes(int key, int unmodified,
    const QString &text)
{
    Q_UNUSED(text)

    if (key == Key_Escape || key == control('c')) {
        m_commandBuffer.clear();
        enterCommandMode();
        updateMiniBuffer();
    } else if (key == Key_Backspace) {
        if (m_commandBuffer.isEmpty()) {
            enterCommandMode();
        } else {
            m_commandBuffer.chop(1);
        }
        updateMiniBuffer();
    } else if (key == Key_Left) {
        // FIXME:
        if (!m_commandBuffer.isEmpty())
            m_commandBuffer.chop(1);
        updateMiniBuffer();
    } else if (unmodified == Key_Return && m_mode == ExMode) {
        if (!m_commandBuffer.isEmpty()) {
            m_commandHistory.takeLast();
            m_commandHistory.append(m_commandBuffer);
            handleExCommand(m_commandBuffer);
            leaveVisualMode();
        }
    } else if (unmodified == Key_Return && isSearchMode()) {
        if (!m_commandBuffer.isEmpty()) {
            m_searchHistory.takeLast();
            m_searchHistory.append(m_commandBuffer);
            m_lastSearchForward = (m_mode == SearchForwardMode);
            search(lastSearchString(), m_lastSearchForward);
            recordJump();
        }
        enterCommandMode();
        updateMiniBuffer();
    } else if ((key == Key_Up || key == Key_PageUp) && isSearchMode()) {
        // FIXME: This and the three cases below are wrong as vim
        // takes only matching entries in the history into account.
        if (m_searchHistoryIndex > 0) {
            --m_searchHistoryIndex;
            showBlackMessage(m_searchHistory.at(m_searchHistoryIndex));
        }
    } else if ((key == Key_Up || key == Key_PageUp) && m_mode == ExMode) {
        if (m_commandHistoryIndex > 0) {
            --m_commandHistoryIndex;
            showBlackMessage(m_commandHistory.at(m_commandHistoryIndex));
        }
    } else if ((key == Key_Down || key == Key_PageDown) && isSearchMode()) {
        if (m_searchHistoryIndex < m_searchHistory.size() - 1) {
            ++m_searchHistoryIndex;
            showBlackMessage(m_searchHistory.at(m_searchHistoryIndex));
        }
    } else if ((key == Key_Down || key == Key_PageDown) && m_mode == ExMode) {
        if (m_commandHistoryIndex < m_commandHistory.size() - 1) {
            ++m_commandHistoryIndex;
            showBlackMessage(m_commandHistory.at(m_commandHistoryIndex));
        }
    } else if (key == Key_Tab) {
        m_commandBuffer += QChar(9);
        updateMiniBuffer();
    } else if (QChar(key).isPrint()) {
        m_commandBuffer += QChar(key);
        updateMiniBuffer();
    } else {
        qDebug() << "IGNORED IN MINIBUFFER MODE: " << key << text;
        return EventUnhandled;
    }
    return EventHandled;
}

// 1 based.
int EmacsKeysHandler::Private::readLineCode(QString &cmd)
{
    //qDebug() << "CMD: " << cmd;
    if (cmd.isEmpty())
        return -1;
    QChar c = cmd.at(0);
    cmd = cmd.mid(1);
    if (c == '.')
        return cursorLineInDocument() + 1;
    if (c == '$')
        return linesInDocument();
    if (c == '\'' && !cmd.isEmpty()) {
        int mark = m_marks.value(cmd.at(0).unicode());
        if (!mark) {
            showRedMessage(tr("E20: Mark '%1' not set").arg(cmd.at(0)));
            cmd = cmd.mid(1);
            return -1;
        }
        cmd = cmd.mid(1);
        return lineForPosition(mark);
    }
    if (c == '-') {
        int n = readLineCode(cmd);
        return cursorLineInDocument() + 1 - (n == -1 ? 1 : n);
    }
    if (c == '+') {
        int n = readLineCode(cmd);
        return cursorLineInDocument() + 1 + (n == -1 ? 1 : n);
    }
    if (c == '\'' && !cmd.isEmpty()) {
        int pos = m_marks.value(cmd.at(0).unicode(), -1);
        //qDebug() << " MARK: " << cmd.at(0) << pos << lineForPosition(pos);
        if (pos == -1) {
            showRedMessage(tr("E20: Mark '%1' not set").arg(cmd.at(0)));
            cmd = cmd.mid(1);
            return -1;
        }
        cmd = cmd.mid(1);
        return lineForPosition(pos);
    }
    if (c.isDigit()) {
        int n = c.unicode() - '0';
        while (!cmd.isEmpty()) {
            c = cmd.at(0);
            if (!c.isDigit())
                break;
            cmd = cmd.mid(1);
            n = n * 10 + (c.unicode() - '0');
        }
        //qDebug() << "N: " << n;
        return n;
    }
    // not parsed
    cmd = c + cmd;
    return -1;
}

void EmacsKeysHandler::Private::selectRange(int beginLine, int endLine)
{
    if (beginLine == -1)
        beginLine = cursorLineInDocument();
    if (endLine == -1)
        endLine = cursorLineInDocument();
    if (beginLine > endLine)
        qSwap(beginLine, endLine);
    setAnchor(firstPositionInLine(beginLine));
    if (endLine == linesInDocument())
       setPosition(lastPositionInLine(endLine));
    else
       setPosition(firstPositionInLine(endLine + 1));
}

void EmacsKeysHandler::Private::handleCommand(const QString &cmd)
{
    m_tc = EDITOR(textCursor());
    handleExCommand(cmd);
    EDITOR(setTextCursor(m_tc));
}

void EmacsKeysHandler::Private::handleExCommand(const QString &cmd0)
{
    QString cmd = cmd0;
    if (cmd.startsWith(QLatin1Char('%')))
        cmd = "1,$" + cmd.mid(1);

    int beginLine = -1;
    int endLine = -1;

    int line = readLineCode(cmd);
    if (line != -1)
        beginLine = line;

    if (cmd.startsWith(',')) {
        cmd = cmd.mid(1);
        line = readLineCode(cmd);
        if (line != -1)
            endLine = line;
    }

    //qDebug() << "RANGE: " << beginLine << endLine << cmd << cmd0 << m_marks;

    static QRegExp reQuit("^qa?!?$");
    static QRegExp reDelete("^d( (.*))?$");
    static QRegExp reHistory("^his(tory)?( (.*))?$");
    static QRegExp reNormal("^norm(al)?( (.*))?$");
    static QRegExp reSet("^set?( (.*))?$");
    static QRegExp reWrite("^[wx]q?a?!?( (.*))?$");
    static QRegExp reSubstitute("^s(.)(.*)\\1(.*)\\1([gi]*)");

    if (cmd.isEmpty()) {
        setPosition(firstPositionInLine(beginLine));
        showBlackMessage(QString());
        enterCommandMode();
    } else if (reQuit.indexIn(cmd) != -1) { // :q
        showBlackMessage(QString());
        if (cmd.contains(QChar('a')))
            q->quitAllRequested(cmd.contains(QChar('!')));
        else
            q->quitRequested(cmd.contains(QChar('!')));
    } else if (reDelete.indexIn(cmd) != -1) { // :d
        selectRange(beginLine, endLine);
        QString reg = reDelete.cap(2);
        QString text = removeSelectedText();
        if (!reg.isEmpty())
            m_registers[reg.at(0).unicode()] = text;
    } else if (reWrite.indexIn(cmd) != -1) { // :w and :x
        enterCommandMode();
        bool noArgs = (beginLine == -1);
        if (beginLine == -1)
            beginLine = 0;
        if (endLine == -1)
            endLine = linesInDocument();
        //qDebug() << "LINES: " << beginLine << endLine;
        int indexOfSpace = cmd.indexOf(QChar(' '));
        QString prefix;
        if (indexOfSpace < 0)
            prefix = cmd;
        else
            prefix = cmd.left(indexOfSpace);
        bool forced = prefix.contains(QChar('!'));
        bool quit = prefix.contains(QChar('q')) || prefix.contains(QChar('x'));
        bool quitAll = quit && prefix.contains(QChar('a'));
        QString fileName = reWrite.cap(2);
        if (fileName.isEmpty())
            fileName = m_currentFileName;
        QFile file1(fileName);
        bool exists = file1.exists();
        if (exists && !forced && !noArgs) {
            showRedMessage(tr("File '%1' exists (add ! to override)").arg(fileName));
        } else if (file1.open(QIODevice::ReadWrite)) {
            file1.close();
            QTextCursor tc = m_tc;
            selectRange(beginLine, endLine);
            QString contents = selectedText();
            m_tc = tc;
            qDebug() << "LINES: " << beginLine << endLine;
            bool handled = false;
            emit q->writeFileRequested(&handled, fileName, contents);
            // nobody cared, so act ourselves
            if (!handled) {
                //qDebug() << "HANDLING MANUAL SAVE TO " << fileName;
                QFile::remove(fileName);
                QFile file2(fileName);
                if (file2.open(QIODevice::ReadWrite)) {
                    QTextStream ts(&file2);
                    ts << contents;
                } else {
                    showRedMessage(tr("Cannot open file '%1' for writing").arg(fileName));
                }
            }
            // check result by reading back
            QFile file3(fileName);
            file3.open(QIODevice::ReadOnly);
            QByteArray ba = file3.readAll();
            showBlackMessage(tr("\"%1\" %2 %3L, %4C written")
                .arg(fileName).arg(exists ? " " : " [New] ")
                .arg(ba.count('\n')).arg(ba.size()));
            if (quitAll)
                q->quitAllRequested(forced);
            else if (quit)
                q->quitRequested(forced);
        } else {
            showRedMessage(tr("Cannot open file '%1' for reading").arg(fileName));
        }
    } else if (cmd.startsWith("r ")) { // :r
        m_currentFileName = cmd.mid(2);
        QFile file(m_currentFileName);
        file.open(QIODevice::ReadOnly);
        QTextStream ts(&file);
        QString data = ts.readAll();
        EDITOR(setPlainText(data));
        enterCommandMode();
        showBlackMessage(tr("\"%1\" %2L, %3C")
            .arg(m_currentFileName).arg(data.count('\n')).arg(data.size()));
    } else if (cmd.startsWith(QLatin1Char('!'))) {
        selectRange(beginLine, endLine);
        QString command = cmd.mid(1).trimmed();
        QString text = removeSelectedText();
        QProcess proc;
        proc.start(cmd.mid(1));
        proc.waitForStarted();
        proc.write(text.toUtf8());
        proc.closeWriteChannel();
        proc.waitForFinished();
        QString result = QString::fromUtf8(proc.readAllStandardOutput());
        m_tc.insertText(result);
        leaveVisualMode();
        setPosition(firstPositionInLine(beginLine));
        enterCommandMode();
        //qDebug() << "FILTER: " << command;
        showBlackMessage(tr("%n lines filtered", 0, text.count('\n')));
    } else if (cmd.startsWith(QLatin1Char('>'))) {
        m_anchor = firstPositionInLine(beginLine);
        setPosition(firstPositionInLine(endLine));
        shiftRegionRight(1);
        leaveVisualMode();
        enterCommandMode();
        showBlackMessage(tr("%n lines >ed %1 time", 0, (endLine - beginLine + 1)).arg(1));
    } else if (cmd == "red" || cmd == "redo") { // :redo
        redo();
        enterCommandMode();
        updateMiniBuffer();
    } else if (reNormal.indexIn(cmd) != -1) { // :normal
        enterCommandMode();
        //qDebug() << "REPLAY: " << reNormal.cap(3);
        replay(reNormal.cap(3), 1);
    } else if (reSubstitute.indexIn(cmd) != -1) { // :substitute
        QString needle = reSubstitute.cap(2);
        const QString replacement = reSubstitute.cap(3);
        QString flags = reSubstitute.cap(4);
        const bool startOfLineOnly = needle.startsWith('^');
        if (startOfLineOnly)
           needle.remove(0, 1);
        needle.replace('$', '\n');
        needle.replace("\\\n", "\\$");
        QRegExp pattern(needle);
        if (flags.contains('i'))
            pattern.setCaseSensitivity(Qt::CaseInsensitive);
        const bool global = flags.contains('g');
        beginEditBlock();
        for (int line = beginLine; line <= endLine; ++line) {
            const int start = firstPositionInLine(line);
            const int end = lastPositionInLine(line);
            for (int position = start; position <= end && position >= start; ) {
                position = pattern.indexIn(m_tc.document()->toPlainText(), position);
                if (startOfLineOnly && position != start)
                    break;
                if (position != -1) {
                    m_tc.setPosition(position);
                    m_tc.movePosition(QTextCursor::NextCharacter,
                        KeepAnchor, pattern.matchedLength());
                    QString text = m_tc.selectedText();
                    if (text.endsWith(ParagraphSeparator)) {
                        text = replacement + "\n";
                    } else {
                        text.replace(ParagraphSeparator, "\n");
                        text.replace(pattern, replacement);
                    }
                    m_tc.removeSelectedText();
                    m_tc.insertText(text);
                }
                if (!global)
                    break;
            }
        }
        endEditBlock();
        enterCommandMode();
    } else if (reSet.indexIn(cmd) != -1) { // :set
        showBlackMessage(QString());
        QString arg = reSet.cap(2);
        SavedAction *act = theEmacsKeysSettings()->item(arg);
        if (arg.isEmpty()) {
            theEmacsKeysSetting(SettingsDialog)->trigger(QVariant());
        } else if (act && act->value().type() == QVariant::Bool) {
            // boolean config to be switched on
            bool oldValue = act->value().toBool();
            if (oldValue == false)
                act->setValue(true);
            else if (oldValue == true)
                {} // nothing to do
        } else if (act) {
            // non-boolean to show
            showBlackMessage(arg + '=' + act->value().toString());
        } else if (arg.startsWith("no")
                && (act = theEmacsKeysSettings()->item(arg.mid(2)))) {
            // boolean config to be switched off
            bool oldValue = act->value().toBool();
            if (oldValue == true)
                act->setValue(false);
            else if (oldValue == false)
                {} // nothing to do
        } else if (arg.contains('=')) {
            // non-boolean config to set
            int p = arg.indexOf('=');
            act = theEmacsKeysSettings()->item(arg.left(p));
            if (act)
                act->setValue(arg.mid(p + 1));
        } else {
            showRedMessage(tr("E512: Unknown option: ") + arg);
        }
        enterCommandMode();
        updateMiniBuffer();
    } else if (reHistory.indexIn(cmd) != -1) { // :history
        QString arg = reSet.cap(3);
        if (arg.isEmpty()) {
            QString info;
            info += "#  command history\n";
            int i = 0;
            foreach (const QString &item, m_commandHistory) {
                ++i;
                info += QString("%1 %2\n").arg(i, -8).arg(item);
            }
            emit q->extraInformationChanged(info);
        } else {
            notImplementedYet();
        }
        enterCommandMode();
        updateMiniBuffer();
    } else {
        enterCommandMode();
        showRedMessage(tr("E492: Not an editor command: ") + cmd0);
    }
}

static void vimPatternToQtPattern(QString *needle, QTextDocument::FindFlags *flags)
{
    // FIXME: Rough mapping of a common case
    if (needle->startsWith("\\<") && needle->endsWith("\\>"))
        (*flags) |= QTextDocument::FindWholeWords;
    needle->replace("\\<", ""); // start of word
    needle->replace("\\>", ""); // end of word
    //qDebug() << "NEEDLE " << needle0 << needle;
}

void EmacsKeysHandler::Private::search(const QString &needle0, bool forward)
{
    showBlackMessage((forward ? '/' : '?') + needle0);
    QTextCursor orig = m_tc;
    QTextDocument::FindFlags flags = QTextDocument::FindCaseSensitively;
    if (!forward)
        flags |= QTextDocument::FindBackward;

    QString needle = needle0;
    vimPatternToQtPattern(&needle, &flags);

    if (forward)
        m_tc.movePosition(Right, MoveAnchor, 1);

    int oldLine = cursorLineInDocument() - cursorLineOnScreen();

    EDITOR(setTextCursor(m_tc));
    if (EDITOR(find(needle, flags))) {
        m_tc = EDITOR(textCursor());
        m_tc.setPosition(m_tc.anchor());
        // making this unconditional feels better, but is not "vim like"
        if (oldLine != cursorLineInDocument() - cursorLineOnScreen())
            scrollToLineInDocument(cursorLineInDocument() - linesOnScreen() / 2);
        highlightMatches(needle);
    } else {
        m_tc.setPosition(forward ? 0 : lastPositionInDocument() - 1);
        EDITOR(setTextCursor(m_tc));
        if (EDITOR(find(needle, flags))) {
            m_tc = EDITOR(textCursor());
            m_tc.setPosition(m_tc.anchor());
            if (oldLine != cursorLineInDocument() - cursorLineOnScreen())
                scrollToLineInDocument(cursorLineInDocument() - linesOnScreen() / 2);
            if (forward)
                showRedMessage(tr("search hit BOTTOM, continuing at TOP"));
            else
                showRedMessage(tr("search hit TOP, continuing at BOTTOM"));
            highlightMatches(needle);
        } else {
            m_tc = orig;
            showRedMessage(tr("E486: Pattern not found: ") + needle);
            highlightMatches(QString());
        }
    }
}

void EmacsKeysHandler::Private::highlightMatches(const QString &needle0)
{
    if (!hasConfig(ConfigHlSearch))
        return;
    if (needle0 == m_oldNeedle)
        return;
    m_oldNeedle = needle0;
    m_searchSelections.clear();

    if (!needle0.isEmpty()) {
        QTextCursor tc = m_tc;
        tc.movePosition(StartOfDocument, MoveAnchor);

        QTextDocument::FindFlags flags = QTextDocument::FindCaseSensitively;
        QString needle = needle0;
        vimPatternToQtPattern(&needle, &flags);


        EDITOR(setTextCursor(tc));
        while (EDITOR(find(needle, flags))) {
            tc = EDITOR(textCursor());
            QTextEdit::ExtraSelection sel;
            sel.cursor = tc;
            sel.format = tc.blockCharFormat();
            sel.format.setBackground(QColor(177, 177, 0));
            m_searchSelections.append(sel);
            tc.movePosition(Right, MoveAnchor);
            EDITOR(setTextCursor(tc));
        }
    }
    updateSelection();
}

void EmacsKeysHandler::Private::moveToFirstNonBlankOnLine()
{
    QTextDocument *doc = m_tc.document();
    const QTextBlock &block = m_tc.block();
    int firstPos = block.position();
    for (int i = firstPos, n = firstPos + block.length(); i < n; ++i) {
        if (!doc->characterAt(i).isSpace()) {
            setPosition(i);
            return;
        }
    }
    setPosition(block.position());
}

void EmacsKeysHandler::Private::indentRegion(QChar typedChar)
{
    //int savedPos = anchor();
    int beginLine = lineForPosition(anchor());
    int endLine = lineForPosition(position());
    if (beginLine > endLine)
        qSwap(beginLine, endLine);

    int amount = 0;
    emit q->indentRegion(&amount, beginLine, endLine, typedChar);

    setPosition(firstPositionInLine(beginLine));
    moveToFirstNonBlankOnLine();
    setTargetColumn();
    setDotCommand("%1==", endLine - beginLine + 1);
}

void EmacsKeysHandler::Private::shiftRegionRight(int repeat)
{
    int beginLine = lineForPosition(anchor());
    int endLine = lineForPosition(position());
    if (beginLine > endLine)
        qSwap(beginLine, endLine);
    int len = config(ConfigShiftWidth).toInt() * repeat;
    QString indent(len, ' ');
    int firstPos = firstPositionInLine(beginLine);

    for (int line = beginLine; line <= endLine; ++line) {
        setPosition(firstPositionInLine(line));
        m_tc.insertText(indent);
    }

    setPosition(firstPos);
    moveToFirstNonBlankOnLine();
    setTargetColumn();
    setDotCommand("%1>>", endLine - beginLine + 1);
}

void EmacsKeysHandler::Private::shiftRegionLeft(int repeat)
{
    int beginLine = lineForPosition(anchor());
    int endLine = lineForPosition(position());
    if (beginLine > endLine)
        qSwap(beginLine, endLine);
    int shift = config(ConfigShiftWidth).toInt() * repeat;
    int tab = config(ConfigTabStop).toInt();
    int firstPos = firstPositionInLine(beginLine);

    for (int line = beginLine; line <= endLine; ++line) {
        int pos = firstPositionInLine(line);
        setPosition(pos);
        setAnchor(pos);
        QString text = m_tc.block().text();
        int amount = 0;
        int i = 0;
        for (; i < text.size() && amount <= shift; ++i) {
            if (text.at(i) == ' ')
                amount++;
            else if (text.at(i) == '\t')
                amount += tab; // FIXME: take position into consideration
            else
                break;
        }
        setPosition(pos + i);
        text = removeSelectedText();
        setPosition(pos);
    }

    setPosition(firstPos);
    moveToFirstNonBlankOnLine();
    setTargetColumn();
    setDotCommand("%1<<", endLine - beginLine + 1);
}

void EmacsKeysHandler::Private::moveToTargetColumn()
{
    const QTextBlock &block = m_tc.block();
    int col = m_tc.position() - m_tc.block().position();
    if (col == m_targetColumn)
        return;
    //qDebug() << "CORRECTING COLUMN FROM: " << col << "TO" << m_targetColumn;
    if (m_targetColumn == -1 || m_tc.block().length() <= m_targetColumn)
        m_tc.setPosition(block.position() + block.length() - 1, KeepAnchor);
    else
        m_tc.setPosition(m_tc.block().position() + m_targetColumn, KeepAnchor);
}

/* if simple is given:
 *  class 0: spaces
 *  class 1: non-spaces
 * else
 *  class 0: spaces
 *  class 1: non-space-or-letter-or-number
 *  class 2: letter-or-number
 */
static int charClass(QChar c, bool simple)
{
    if (simple)
        return c.isSpace() ? 0 : 1;
    if (c.isLetterOrNumber() || c.unicode() == '_')
        return 2;
    return c.isSpace() ? 0 : 1;
}

void EmacsKeysHandler::Private::moveToWordBoundary(bool simple, bool forward)
{
    int repeat = count();
    QTextDocument *doc = m_tc.document();
    int n = forward ? lastPositionInDocument() - 1 : 0;
    int lastClass = -1;
    while (true) {
        QChar c = doc->characterAt(m_tc.position() + (forward ? 1 : -1));
        //qDebug() << "EXAMINING: " << c << " AT " << position();
        int thisClass = charClass(c, simple);
        if (thisClass != lastClass && lastClass != 0)
            --repeat;
        if (repeat == -1)
            break;
        lastClass = thisClass;
        if (m_tc.position() == n)
            break;
        forward ? moveRight() : moveLeft();
    }
    setTargetColumn();
}

void EmacsKeysHandler::Private::handleFfTt(int key)
{
    // m_subsubmode \in { 'f', 'F', 't', 'T' }
    bool forward = m_subsubdata == 'f' || m_subsubdata == 't';
    int repeat = count();
    QTextDocument *doc = m_tc.document();
    QTextBlock block = m_tc.block();
    int n = block.position();
    if (forward)
        n += block.length();
    int pos = m_tc.position();
    while (true) {
        pos += forward ? 1 : -1;
        if (pos == n)
            break;
        int uc = doc->characterAt(pos).unicode();
        if (uc == ParagraphSeparator)
            break;
        if (uc == key)
            --repeat;
        if (repeat == 0) {
            if (m_subsubdata == 't')
                --pos;
            else if (m_subsubdata == 'T')
                ++pos;

            if (forward)
                m_tc.movePosition(Right, KeepAnchor, pos - m_tc.position());
            else
                m_tc.movePosition(Left, KeepAnchor, m_tc.position() - pos);
            break;
        }
    }
    setTargetColumn();
}

void EmacsKeysHandler::Private::moveToNextWord(bool simple)
{
    // FIXME: 'w' should stop on empty lines, too
    int repeat = count();
    int n = lastPositionInDocument() - 1;
    int lastClass = charClass(characterAtCursor(), simple);
    while (true) {
        QChar c = characterAtCursor();
        int thisClass = charClass(c, simple);
        if (thisClass != lastClass && thisClass != 0)
            --repeat;
        if (repeat == 0)
            break;
        lastClass = thisClass;
        moveRight();
        if (m_tc.position() == n)
            break;
    }
    setTargetColumn();
}

void EmacsKeysHandler::Private::moveToMatchingParanthesis()
{
    bool moved = false;
    bool forward = false;

    emit q->moveToMatchingParenthesis(&moved, &forward, &m_tc);

    if (moved && forward) {
       if (m_submode == NoSubMode || m_submode == ZSubMode || m_submode == CapitalZSubMode || m_submode == RegisterSubMode)
            m_tc.movePosition(Left, KeepAnchor, 1);
    }
    setTargetColumn();
}

int EmacsKeysHandler::Private::cursorLineOnScreen() const
{
    if (!editor())
        return 0;
    QRect rect = EDITOR(cursorRect());
    return rect.y() / rect.height();
}

int EmacsKeysHandler::Private::linesOnScreen() const
{
    if (!editor())
        return 1;
    QRect rect = EDITOR(cursorRect());
    return EDITOR(height()) / rect.height();
}

int EmacsKeysHandler::Private::columnsOnScreen() const
{
    if (!editor())
        return 1;
    QRect rect = EDITOR(cursorRect());
    // qDebug() << "WID: " << EDITOR(width()) << "RECT: " << rect;
    return EDITOR(width()) / rect.width();
}

int EmacsKeysHandler::Private::cursorLineInDocument() const
{
    return m_tc.block().blockNumber();
}

int EmacsKeysHandler::Private::cursorColumnInDocument() const
{
    return m_tc.position() - m_tc.block().position();
}

int EmacsKeysHandler::Private::linesInDocument() const
{
    return m_tc.isNull() ? 0 : m_tc.document()->blockCount();
}

void EmacsKeysHandler::Private::scrollToLineInDocument(int line)
{
    // FIXME: works only for QPlainTextEdit
    QScrollBar *scrollBar = EDITOR(verticalScrollBar());
    //qDebug() << "SCROLL: " << scrollBar->value() << line;
    scrollBar->setValue(line);
}

void EmacsKeysHandler::Private::scrollUp(int count)
{
    scrollToLineInDocument(cursorLineInDocument() - cursorLineOnScreen() - count);
}

int EmacsKeysHandler::Private::lastPositionInDocument() const
{
    QTextBlock block = m_tc.document()->lastBlock();
    return block.position() + block.length();
}

QString EmacsKeysHandler::Private::lastSearchString() const
{
     return m_searchHistory.empty() ? QString() : m_searchHistory.back();
}

QString EmacsKeysHandler::Private::selectedText() const
{
    QTextCursor tc = m_tc;
    tc.setPosition(m_anchor, KeepAnchor);
    return tc.selection().toPlainText();
}

int EmacsKeysHandler::Private::firstPositionInLine(int line) const
{
    return m_tc.document()->findBlockByNumber(line - 1).position();
}

int EmacsKeysHandler::Private::lastPositionInLine(int line) const
{
    QTextBlock block = m_tc.document()->findBlockByNumber(line - 1);
    return block.position() + block.length() - 1;
}

int EmacsKeysHandler::Private::lineForPosition(int pos) const
{
    QTextCursor tc = m_tc;
    tc.setPosition(pos);
    return tc.block().blockNumber() + 1;
}

void EmacsKeysHandler::Private::enterVisualMode(VisualMode visualMode)
{
    setAnchor();
    m_visualMode = visualMode;
    m_marks['<'] = m_tc.position();
    m_marks['>'] = m_tc.position();
    updateMiniBuffer();
    updateSelection();
}

void EmacsKeysHandler::Private::leaveVisualMode()
{
    m_visualMode = NoVisualMode;
    updateMiniBuffer();
    updateSelection();
}

QWidget *EmacsKeysHandler::Private::editor() const
{
    return m_textedit
        ? static_cast<QWidget *>(m_textedit)
        : static_cast<QWidget *>(m_plaintextedit);
}

void EmacsKeysHandler::Private::undo()
{
    int current = m_tc.document()->revision();
    //endEditBlock();
    EDITOR(undo());
    //beginEditBlock();
    int rev = m_tc.document()->revision();
    if (current == rev)
        showBlackMessage(tr("Already at oldest change"));
    else
        showBlackMessage(QString());
    if (m_undoCursorPosition.contains(rev))
        m_tc.setPosition(m_undoCursorPosition[rev]);
}

void EmacsKeysHandler::Private::redo()
{
    int current = m_tc.document()->revision();
    //endEditBlock();
    EDITOR(redo());
    //beginEditBlock();
    int rev = m_tc.document()->revision();
    if (rev == current)
        showBlackMessage(tr("Already at newest change"));
    else
        showBlackMessage(QString());
    if (m_undoCursorPosition.contains(rev))
        m_tc.setPosition(m_undoCursorPosition[rev]);
}

QString EmacsKeysHandler::Private::removeSelectedText()
{
    //qDebug() << "POS: " << position() << " ANCHOR: " << anchor() << m_tc.anchor();
    int pos = m_tc.position();
    if (pos == anchor())
        return QString();
    m_tc.setPosition(anchor(), MoveAnchor);
    m_tc.setPosition(pos, KeepAnchor);
    QString from = m_tc.selection().toPlainText();
    m_tc.removeSelectedText();
    setAnchor();
    return from;
}

void EmacsKeysHandler::Private::enterInsertMode()
{
    EDITOR(setCursorWidth(m_cursorWidth));
    EDITOR(setOverwriteMode(false));
    m_mode = InsertMode;
    m_lastInsertion.clear();
}

void EmacsKeysHandler::Private::enterCommandMode()
{
    EDITOR(setCursorWidth(m_cursorWidth));
    EDITOR(setOverwriteMode(true));
    m_mode = CommandMode;
}

void EmacsKeysHandler::Private::enterExMode()
{
    EDITOR(setCursorWidth(0));
    EDITOR(setOverwriteMode(false));
    m_mode = ExMode;
}

void EmacsKeysHandler::Private::recordJump()
{
    m_jumpListUndo.append(position());
    m_jumpListRedo.clear();
    UNDO_DEBUG("jumps: " << m_jumpListUndo);
}

void EmacsKeysHandler::Private::recordNewUndo()
{
    //endEditBlock();
    UNDO_DEBUG("---- BREAK ----");
    //beginEditBlock();
}

void EmacsKeysHandler::Private::insertAutomaticIndentation(bool goingDown)
{
    if (!hasConfig(ConfigAutoIndent))
        return;
    QTextBlock block = goingDown ? m_tc.block().previous() : m_tc.block().next();
    QString text = block.text();
    int pos = 0, n = text.size();
    while (pos < n && text.at(pos).isSpace())
        ++pos;
    text.truncate(pos);
    // FIXME: handle 'smartindent' and 'cindent'
    m_tc.insertText(text);
    m_justAutoIndented = text.size();
}

bool EmacsKeysHandler::Private::removeAutomaticIndentation()
{
    if (!hasConfig(ConfigAutoIndent) || m_justAutoIndented == 0)
        return false;
    m_tc.movePosition(StartOfLine, KeepAnchor);
    m_tc.removeSelectedText();
    m_lastInsertion.chop(m_justAutoIndented);
    m_justAutoIndented = 0;
    return true;
}

void EmacsKeysHandler::Private::handleStartOfLine()
{
    if (hasConfig(ConfigStartOfLine))
        moveToFirstNonBlankOnLine();
}

void EmacsKeysHandler::Private::replay(const QString &command, int n)
{
    //qDebug() << "REPLAY: " << command;
    m_inReplay = true;
    for (int i = n; --i >= 0; ) {
        foreach (QChar c, command) {
            //qDebug() << "  REPLAY: " << QString(c);
            handleKey(c.unicode(), c.unicode(), QString(c));
        }
    }
    m_inReplay = false;
}

///////////////////////////////////////////////////////////////////////
//
// EmacsKeysHandler
//
///////////////////////////////////////////////////////////////////////

EmacsKeysHandler::EmacsKeysHandler(QWidget *widget, QObject *parent)
    : QObject(parent), d(new Private(this, widget))
{}

EmacsKeysHandler::~EmacsKeysHandler()
{
    delete d;
}

bool EmacsKeysHandler::eventFilter(QObject *ob, QEvent *ev)
{
    bool active = theEmacsKeysSetting(ConfigUseEmacsKeys)->value().toBool();

    if (active && ev->type() == QEvent::KeyPress && ob == d->editor()) {
        QKeyEvent *kev = static_cast<QKeyEvent *>(ev);
        KEY_DEBUG("KEYPRESS" << kev->key());
        EventResult res = d->handleEvent(kev);
        // returning false core the app see it
        //KEY_DEBUG("HANDLED CODE:" << res);
        //return res != EventPassedToCore;
        //return true;
        return res == EventHandled;
    }

    if (active && ev->type() == QEvent::ShortcutOverride && ob == d->editor()) {
        QKeyEvent *kev = static_cast<QKeyEvent *>(ev);
        if (d->wantsOverride(kev)) {
            KEY_DEBUG("OVERRIDING SHORTCUT" << kev->key());
            ev->accept(); // accepting means "don't run the shortcuts"
            return true;
        }
        KEY_DEBUG("NO SHORTCUT OVERRIDE" << kev->key());
        return true;
    }

    return QObject::eventFilter(ob, ev);
}

void EmacsKeysHandler::installEventFilter()
{
    d->installEventFilter();
}

void EmacsKeysHandler::setupWidget()
{
    d->setupWidget();
}

void EmacsKeysHandler::restoreWidget()
{
    d->restoreWidget();
}

void EmacsKeysHandler::handleCommand(const QString &cmd)
{
    d->handleCommand(cmd);
}

void EmacsKeysHandler::setCurrentFileName(const QString &fileName)
{
   d->m_currentFileName = fileName;
}

QWidget *EmacsKeysHandler::widget()
{
    return d->editor();
}

} // namespace Internal
} // namespace EmacsKeys
