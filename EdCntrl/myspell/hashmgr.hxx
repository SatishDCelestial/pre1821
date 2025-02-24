#ifndef _HASHMGR_HXX_
#define _HASHMGR_HXX_

#include "htypes.hxx"

class HashMgr
{
  int             tablesize;
  struct hentry * tableptr;

public:
  HashMgr(const wchar_t * tpath);
  ~HashMgr();

  struct hentry * lookup(const char *) const;
  int hash(const char *) const;
  struct hentry * walk_hashtable(int & col, struct hentry * hp) const;
  int add_word(const char * word, int wl, const char * ap, int al);

private:
  HashMgr( const HashMgr & ); // not implemented
  HashMgr &operator=( const HashMgr & ); // not implemented
  int load_tables(const wchar_t * tpath);

};

#endif
