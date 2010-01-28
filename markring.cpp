#include "markring.h"
#include "mark.h"

MarkRing::MarkRing()
  : iter(ring.begin())
{

}

void MarkRing::addMark(int position)
{
  Mark mark(position);
  if (ring.isEmpty() || ring.first() != mark) {
    ring.prepend(mark);
  }
  // shrink ring to default emacs max size
  while (ring.count() > 16) {
    ring.pop_back();
  }
  iter = ring.begin();
}

Mark MarkRing::getPreviousMark()
{
  if (ring.isEmpty()) {
    return Mark();
  }
  else if (++iter == ring.end()) {
    iter = ring.begin();
  }
  return *iter;
}

Mark MarkRing::getMostRecentMark()
{
  return ring.isEmpty() ? Mark() : ring.first();
}
