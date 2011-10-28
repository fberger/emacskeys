#include "killring.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>

KillRing::KillRing()
  : currentView(0), iter(ring.begin()), ignore(false)
{
  connect(QApplication::clipboard(), SIGNAL(dataChanged()),
	  SLOT(clipboardDataChanged()));
}

KillRing* KillRing::instance()
{
  static KillRing* instance;
  if (!instance) {
    instance = new KillRing();
  }
  return instance;
}

void KillRing::ignoreNextClipboardChange()
{
  ignore = true;
}

void KillRing::add(const QString& text)
{
  if (text.isEmpty()) {
    return;
  }
  if (ignore) {
    ignore = false;
    return;
  }

  // original emacs implementation does not remove duplicates
  ring.removeAll(text);
  ring.prepend(text);
  // shrink ring to default emacs max size
  while (ring.count() > 60) {
    ring.pop_back();
  }
  iter = ring.begin();
}

QString KillRing::next()
{
  if (ring.isEmpty()) {
    return QString::null;
  }
  else if (++iter == ring.end()) {
    iter = ring.begin();
  }
  KillRing::instance()->ignoreNextClipboardChange();
  QApplication::clipboard()->setText(*iter);
  return *iter;
}

void KillRing::setCurrentYankView(QWidget* view)
{
  currentView = view;
}

QWidget* KillRing::currentYankView() const
{
  return currentView;
}

void KillRing::clipboardDataChanged()
{
  qDebug() << "clipboard changed " << QApplication::clipboard()->text()
	    << endl;
  // TODO handle mouse selection too, optionally
  QString text(QApplication::clipboard()->text());
  add(text);
}
