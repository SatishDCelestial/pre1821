#ifndef _SUGGESTMGR_HXX_
#define _SUGGESTMGR_HXX_

#define MAXSWL 250
#define MAX_ROOTS 10
#define MAX_WORDS 500
#define MAX_GUESS 10

#define NGRAM_IGNORE_LENGTH 0
#define NGRAM_LONGER_WORSE  1
#define NGRAM_ANY_MISMATCH  2


#include "atypes.hxx"
#include "affixmgr.hxx"
#include "hashmgr.hxx"

class SuggestMgr
{
  char *          ctry;
  size_t          ctryl;
  AffixMgr*       pAMgr;
  size_t             maxSug;
  bool            nosplitsugs;

public:
  SuggestMgr(const char * tryme, size_t maxn, AffixMgr *aptr);
  ~SuggestMgr();

  int suggest(char** wlst, int ns, const char * word);
  int check(const char *, int);
  int ngsuggest(char ** wlst, char * word, HashMgr* pHMgr);

private:
   int replchars(char**, const char *, int);
   int mapchars(char**, const char *, int, int max_recursion_level);
   int map_related(const char *, int, char ** wlst, int, const mapentry*, int, int max_recursion_level);
   int forgotchar(char **, const char *, int);
   int swapchar(char **, const char *, int);
   int extrachar(char **, const char *, int);
   int badchar(char **, const char *, int);
   int twowords(char **, const char *, int);
   int ngram(int n, char * s1, const char * s2, int uselen);
   void bubblesort( char ** rwd, int * rsc, int n);
};

#endif

