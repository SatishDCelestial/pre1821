#pragma once

class ParseWorkItem
{
  public:
	ParseWorkItem(LPCTSTR jobName) : mJobName(jobName)
	{
	}
	virtual ~ParseWorkItem()
	{
	}

	virtual void DoParseWork() = 0;
	virtual bool CanRunNow() const
	{
		return true;
	}
	virtual bool ShouldRunAtShutdown() const
	{
		return false;
	}
	virtual WTString GetJobName() const
	{
		return mJobName;
	}

  protected:
	WTString mJobName;
};

class FunctionWorkItem : public ParseWorkItem
{
  public:
	explicit FunctionWorkItem(std::function<void()> f) : ParseWorkItem("FunctionWrapper"), mF(f)
	{
	}

	virtual void DoParseWork()
	{
		mF();
	}

  private:
	std::function<void()> mF;
};
