#include "stdafxed.h"
#include "Project.h"
#include "FDictionary.h"
#include "VA_MRUs.h"
#include "File.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

template <class ItemType> void VASavedList<ItemType>::SaveToFile()
{
	if (m_filename.IsEmpty())
		return;

	try // catch any file errors...
	{
		CFileW of;
		if (of.Open(m_filename, CFileW::modeWrite | CFileW::modeCreate | CFileW::shareCompat))
		{
			CStringW tmp;
			uint idx = 0;
			for (typename BASE::iterator it = BASE::begin(); it != BASE::end() && idx < kMaxListSize; it++, ++idx)
			{
				tmp = *it;
				tmp += L"\n";
				of.Write((void*)(LPCWSTR)tmp, tmp.GetLength() * sizeof(WCHAR));
			}
		}
	}
	catch (...)
	{
	}
}

template<>
void VASavedList<WTString>::SaveToFile()
{
	if (m_filename.IsEmpty())
		return;

	try // catch any file errors...
	{
		CFileW of;
		if (of.Open(m_filename, CFileW::modeWrite | CFileW::modeCreate | CFileW::shareCompat))
		{
			CStringW tmp;
			uint idx = 0;
			for (typename BASE::iterator it = BASE::begin(); it != BASE::end() && idx < kMaxListSize; it++, ++idx)
			{
				tmp = (*it).Wide();
				tmp += L"\n";
				of.Write((void*)(LPCWSTR)tmp, tmp.GetLength() * sizeof(WCHAR));
			}
		}
	}
	catch (...)
	{
	}
}

template <class ItemType> void VASavedList<ItemType>::LoadFromFile(const CStringW& filename)
{
	if (m_filename == filename)
		return; // Already loaded

	SaveToFile();
	BASE::clear();
	m_filename = filename;

	try // catch any file errors...
	{
		CStringW fileContents;
		::ReadFileUtf16(m_filename, fileContents);
		int pos = 0;

		for (uint idx = 0; idx < kMaxListSize; ++idx)
		{
			int nextPos = fileContents.Find(L'\n', pos);
			if (-1 == nextPos)
			{
				if (pos != 0)
					BASE::push_back(ItemType(fileContents.Mid(pos)));
				break;
			}

			BASE::push_back(ItemType(fileContents.Mid(pos, nextPos - pos)));
			pos = nextPos + 1;
		}
	}
	catch (...)
	{
	}
}

void VA_MRUs::LoadProject(const CStringW& project)
{
	if (m_project.GetLength())
		SaveALL();
	m_project = project;
	if (m_project.GetLength())
	{
		const CStringW mruDir(VaDirs::GetUserDir() + L"MRU2");
		::CreateDir(mruDir);

		CStringW tfile;
		CString__FormatW(tfile, L"%s\\mru_%08x_MIF.wtxt", (const wchar_t*)mruDir, WTHashKeyW(project));
		m_MIF.LoadFromFile(tfile);

		CString__FormatW(tfile, L"%s\\mru_%08x_FIS.wtxt", (const wchar_t*)mruDir, WTHashKeyW(project));
		m_FIS.LoadFromFile(tfile);
	}
}

void VA_MRUs::SaveALL()
{
	if (m_project.GetLength())
	{
		m_MIF.SaveToFile();
		m_FIS.SaveToFile();
	}
}
