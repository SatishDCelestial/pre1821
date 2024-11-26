#pragma once

#include "PooledThreadBase.h"
#include "Log.h"
#include "Lock.h"

class ProjectLoader;
class VsSolutionInfo;
typedef ProjectLoader* ProjectLoaderPtr;

class ProjectLoader : public PooledThreadBase
{
	friend class ProjectLoaderRef;

  public:
	ProjectLoader(std::shared_ptr<VsSolutionInfo> si) : PooledThreadBase("ProjectLoader", true), mVsSolInfo(si)
	{
		_ASSERTE(g_pl == nullptr);
		Log2("ProjectLoader ctor");
		// Release is done at end of Run() (or below if StartThread fails)
		AddRef();

		AutoLockCs l(sLock);
		g_pl = this;

		if (!StartThread())
		{
			g_pl = nullptr;
			Release();
		}
	}

	bool Stop();
	virtual void Run();

	virtual ~ProjectLoader()
	{
		_ASSERTE(g_pl != this);
	}

  private:
	static ProjectLoaderPtr GetGlobalRef()
	{
		AutoLockCs l(sLock);
		if (!g_pl)
			return nullptr;

		g_pl->AddRef();
		return g_pl;
	}

	void ReleaseRef()
	{
		AutoLockCs l(sLock);
		if (!this)
			return;

		Release();
	}

	void ClearGlobalIfThis()
	{
		AutoLockCs l(sLock);
		if (this == g_pl)
			g_pl = nullptr;
	}

  private:
	std::shared_ptr<VsSolutionInfo> mVsSolInfo;
	static CCriticalSection sLock;
	static ProjectLoaderPtr g_pl;
};

class ProjectLoaderRef
{
  public:
	ProjectLoaderRef() : mPl(ProjectLoader::GetGlobalRef())
	{
	}

	~ProjectLoaderRef()
	{
		mPl->ReleaseRef();
	}

	ProjectLoaderPtr operator->()
	{
		return mPl;
	}

	operator bool() const
	{
		return mPl != nullptr;
	}

  private:
	ProjectLoaderPtr mPl;

	ProjectLoaderRef(const ProjectLoaderPtr);
	ProjectLoaderRef(const ProjectLoaderRef&);
};
