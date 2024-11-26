#pragma once

#include "WTString.h"
#include "Lock.h"

class Guesses
{
  public:
	Guesses() : mGuessCount(0)
	{
	}
	~Guesses()
	{
	}

	DWORD GetCount() const
	{
		AutoLockCs l(mLock);
		return mGuessCount;
	}

	WTString GetGuesses() const
	{
		AutoLockCs l(mLock);
		return mGuesses;
	}

	WTString GetMostLikelyGuess() const
	{
		AutoLockCs l(mLock);
		return mMostLikelyGuess;
	}

	bool Contains(const WTString& guess) const
	{
		AutoLockCs l(mLock);
		//#ifdef _DEBUG
		//		if(mGuesses.Find(guess.c_str()) != -1 && !ContainsWholeWord(guess))
		//		{
		//			// In some cases checking strings and others, we check for CompletionSetEntry with special
		// characters in them
		//			// The special chars break ContainsWholeWord, so these enteries get added twice
		//			// TODO: Need to cleanup these cases...
		//			_asm nop;
		//		}
		//#endif // _DEBUG
		return mGuesses.Find(guess.c_str()) != -1;
	}

	bool ContainsWholeWord(const WTString& guess) const
	{
		AutoLockCs l(mLock);
		return strstrWholeWord(mGuesses, guess) != NULL;
	}

	void SetMostLikely(WTString mostLikely)
	{
		AutoLockCs l(mLock);
		mMostLikelyGuess = mostLikely;
	}

	DWORD AddTopGuess(WTString guess)
	{
		AutoLockCs l(mLock);
		// Add to top even if it is already there
		mGuesses = guess + mGuesses;
		++mGuessCount;
		return mGuessCount;
	}
	DWORD AddGuess(WTString guess)
	{
		AutoLockCs l(mLock);
		if (!ContainsWholeWord(guess))
		{
			mGuesses += guess;
			++mGuessCount;
		}
		return mGuessCount;
	}

	void Reset()
	{
		AutoLockCs l(mLock);
		mGuessCount = 0;
		mGuesses.Empty();
	}

	void ClearMostLikely()
	{
		AutoLockCs l(mLock);
		mMostLikelyGuess.Empty();
	}

	// so that callers can lock blocks rather than individual calls
	CCriticalSection& GetLock()
	{
		return mLock;
	}

  private:
	WTString mGuesses;
	WTString mMostLikelyGuess;
	DWORD mGuessCount;
	mutable CCriticalSection mLock;
};

extern Guesses g_Guesses;
