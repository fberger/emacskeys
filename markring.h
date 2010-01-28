#ifndef MARKRING_H
#define MARKRING_H

#include <QList>

#include "mark.h"



class MarkRing
{
public:
  MarkRing();
  void addMark(int position);
  Mark getPreviousMark();
  Mark getMostRecentMark();
  
private:
  QList<Mark> ring;
  QList<Mark>::Iterator iter;
};

#endif
