#ifndef MARK_H
#define MARK_H

struct Mark 
{
  Mark(unsigned int line, unsigned int column)
    : line(line), column(column), valid(true)
  {
  }
  Mark()
    : valid(false)
  {
  }
  bool operator ==(const Mark& mark)
  {
    return valid == mark.valid && line == mark.line && column == mark.column;
  }
  bool operator !=(const Mark& mark)
  {
    return !(*this == mark);
  }
  unsigned int line;
  unsigned int column;
  bool valid;
};

#endif
