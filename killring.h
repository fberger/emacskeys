#ifndef KILLRING_H
#define KILLRING_H

#include <QObject>
#include <QStringList>

class QWidget;

class KillRing : public QObject
{
  Q_OBJECT

public:
  KillRing();
  void setCurrentYankView(QWidget* view);
  QWidget* currentYankView() const;
  void add(const QString& text);
  QString next();
  void ignoreNextClipboardChange();
  static KillRing* instance();

private slots:
  void clipboardDataChanged();

private:
  QStringList ring;
  QWidget* currentView;
  QStringList::ConstIterator iter;
  bool ignore;
};

#endif
