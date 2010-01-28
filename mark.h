#ifndef MARK_H
#define MARK_H

struct Mark 
{
  Mark(int position)
    : valid(true), position(position)
  {
  }
  Mark()
    : valid(false)
  {
  }
  bool operator ==(const Mark& mark)
  {
    return valid == mark.valid && position == mark.position;
  }
  bool operator !=(const Mark& mark)
  {
    return !(*this == mark);
  }
  bool valid;
  int position;
};

#endif
