#include "StdAfxEd.h"
#include "ImageListManager.h"
#include "resource.h"
#include "DevShellAttributes.h"
#include "vsshell110.h"
#include "IdeSettings.h"
#include "VaService.h"
#include "ColorListControls.h"
#include "Settings.h"
#include "DpiCookbook\VsUIDpiHelper.h"
#include "FastBitmapReader.h"
#include "PROJECT.H"
#include "VAThemeUtils.h"
#include "DebugStream.h"
#include "Expansion.h"
#include "FileVerInfo.h"

//#ifdef _WIN64
#include "vsshell140.h"
#include "KnownMonikers.h"
using namespace Microsoft::VisualStudio::Imaging;
//#endif // _WIN64

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

ImageListManager* gImgListMgr = nullptr;
int g_IconIdx_VaOffset = 0;

COLORREF
ImageListManager::GetBackgroundColor(ImageListManager::BackgroundType bt)
{
	COLORREF bgClr;
	switch (bt)
	{
	case ImageListManager::bgCombo:
		bgClr = CVS2010Colours::GetVS2010Colour(VSCOLOR_COMBOBOX_POPUP_BACKGROUND_BEGIN);
		break;
	case ImageListManager::bgComboDropdown:
		bgClr = CVS2010Colours::GetVS2010Colour(VSCOLOR_DROPDOWN_POPUP_BACKGROUND_BEGIN);
		break;
	case ImageListManager::bgList:
		bgClr = g_IdeSettings->GetEnvironmentColor(L"Window", false);
		break;
	case ImageListManager::bgMenu:
		bgClr = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuBackgroundGradientBegin", FALSE);
		break;
	case ImageListManager::bgOsWindow:
		bgClr = ::GetSysColor(COLOR_WINDOW);
		break;
	case ImageListManager::bgTooltip:
		bgClr = g_IdeSettings->GetEnvironmentColor(L"CommandBarMenuBackgroundGradientBegin", FALSE);
		break;
	case ImageListManager::bgTree:
		bgClr = g_IdeSettings->GetThemeTreeColor(L"Background", false);
		break;
	default:
		_ASSERTE(!"unhandled BackgroundType value in ThemeImageList");
		bgClr = 0xffffff;
		break;
	}

	return bgClr;
}

struct ImageListManager::ImgListState
{
	std::shared_ptr<CBitmap> mVsRawIcons = std::make_shared<CBitmap>();
	COLORREF mVsTransparentColor = CLR_NONE;
	std::shared_ptr<CBitmap> mVaRawIcons = std::make_shared<CBitmap>();
	COLORREF mVaTransparentColor = CLR_NONE;

	// mUnscaledUnthemedImageList is 32bit argb imagelist before any theming scaling
	std::shared_ptr<CImageList> mUnscaledUnthemedImageList;
	std::map<ImgListKey, std::shared_ptr<CImageList>> mThemedImageLists;
};

#define COLORREF2RGB(clr) (clr & 0xff00) | ((clr >> 16) & 0xff) | ((clr << 16) & 0xff0000)

void MakeBitmapTransparent(CBitmap& bitmap, COLORREF clrTransparent)
{
	CDC srcDC;
	srcDC.CreateCompatibleDC(nullptr);

	CDC dstDC;
	dstDC.CreateCompatibleDC(nullptr);

	if (!srcDC || !dstDC)
		return;

	// Get bitmap size
	BITMAP bm;
	bitmap.GetBitmap(&bm);

	// create a BITMAPINFO with minimal initialization for the CreateDIBSection
	BITMAPINFO bmInfo;
	ZeroMemory(&bmInfo, sizeof(bmInfo));
	bmInfo.bmiHeader.biSize = sizeof(bmInfo.bmiHeader);
	bmInfo.bmiHeader.biWidth = bm.bmWidth;
	bmInfo.bmiHeader.biHeight = bm.bmHeight;
	bmInfo.bmiHeader.biPlanes = 1;
	bmInfo.bmiHeader.biBitCount = 32;
	bmInfo.bmiHeader.biCompression = BI_RGB;

	UINT* pPixels = nullptr;
	HBITMAP dstBitmap = CreateDIBSection(dstDC, &bmInfo, DIB_RGB_COLORS, (void**)&pPixels, nullptr, 0);
	if (dstBitmap && pPixels)
	{
		HGDIOBJ prevSrcObj = srcDC.SelectObject(bitmap);
		HGDIOBJ prevDstObj = dstDC.SelectObject(dstBitmap);

		dstDC.BitBlt(0, 0, bm.bmWidth, bm.bmHeight, &srcDC, 0, 0, SRCCOPY);

		clrTransparent = COLORREF2RGB(clrTransparent);

		for (int i = ((bm.bmWidth * bm.bmHeight) - 1); i >= 0; i--)
		{
			_ASSERTE(!(pPixels[i] & 0xff000000));
			if (pPixels[i] == clrTransparent)
			{
				pPixels[i] = 0x00ffffff;
			}
			else
			{
				pPixels[i] |= 0xff000000;
			}
		}

		srcDC.SelectObject(prevSrcObj);
		dstDC.SelectObject(prevDstObj);

		bitmap.DeleteObject();
		bitmap.Attach(dstBitmap);

		VsUI::GdiplusImage iOrig;
		iOrig.Attach(bitmap);
		if (iOrig.GetBitmap()->GetPixelFormat() != PixelFormat32bppARGB)
		{
			MessageBox(nullptr, "Not PixelFormat32bppARGB", "Not PixelFormat32bppARGB", MB_OK);
		}
	}
}

ImageListManager::ImageListManager() : mState(new ImgListState())
{
	_ASSERTE(!gImgListMgr);

	UINT ilFlags = 0;

	if (gShellAttr->IsDevenv12OrHigher() || gShellAttr->IsCppBuilder())
	{
		// 32bpp ARGB; LoadBitmap does not load completely properly, causes problems with Gdi+
		mState->mVaRawIcons->Attach(::LoadImage(AfxFindResourceHandle(MAKEINTRESOURCE(IDB_VAICONS11), RT_BITMAP),
		                                        MAKEINTRESOURCE(IDB_VAICONS11), IMAGE_BITMAP, 0, 0,
		                                        LR_CREATEDIBSECTION));
		mState->mVsRawIcons->Attach(::LoadImage(AfxFindResourceHandle(MAKEINTRESOURCE(VSNET_SYMBOLS12), RT_BITMAP),
		                                        MAKEINTRESOURCE(VSNET_SYMBOLS12), IMAGE_BITMAP, 0, 0,
		                                        LR_CREATEDIBSECTION));
	}
	else if (gShellAttr->IsDevenv11())
	{
		// 32bpp ARGB; LoadBitmap does not load completely properly, causes problems with Gdi+
		mState->mVaRawIcons->Attach(::LoadImage(AfxFindResourceHandle(MAKEINTRESOURCE(IDB_VAICONS11), RT_BITMAP),
		                                        MAKEINTRESOURCE(IDB_VAICONS11), IMAGE_BITMAP, 0, 0,
		                                        LR_CREATEDIBSECTION));
		mState->mVsRawIcons->Attach(::LoadImage(AfxFindResourceHandle(MAKEINTRESOURCE(VSNET_SYMBOLS11), RT_BITMAP),
		                                        MAKEINTRESOURCE(VSNET_SYMBOLS11), IMAGE_BITMAP, 0, 0,
		                                        LR_CREATEDIBSECTION));
	}
	else if (gShellAttr->IsDevenv8OrHigher())
	{
		// 24bpp RGB with back color
		mState->mVaRawIcons->LoadBitmap(IDB_VAICONS);
		MakeBitmapTransparent(*mState->mVaRawIcons, RGB(0, 255, 0));

		mState->mVsRawIcons->LoadBitmap(VSNET_SYMBOLS8);
		MakeBitmapTransparent(*mState->mVsRawIcons, RGB(255, 0, 255));
	}
	else
	{
		// [case: 90226] For unknown reasons, VC6-VS2005 don't like
		// imagelists with 32-bit argb images. They are not drawn transparently.
		// Using a mask works, but complicates scaling, so it is disabled.

		// xxbpp RGB with back color
		mState->mVaRawIcons->LoadBitmap(IDB_VAICONS);
		//		MakeBitmapTransparent(vaIcons, RGB(0, 255, 0));
		mState->mVaTransparentColor = RGB(0, 255, 0);

		mState->mVsRawIcons->LoadBitmap(VSNET_SYMBOLS);
		//		MakeBitmapTransparent(vsSymbols, RGB(0, 255, 0));
		mState->mVsTransparentColor = RGB(0, 255, 0);

		ilFlags = ILC_MASK;
	}

	std::shared_ptr<CImageList> pImageList(new CImageList());
	BOOL retval = pImageList->Create(16, 16, ILC_COLOR32 | ilFlags, 0, 0);
	if (!retval)
		vLog("ERROR: ILM create failed 0x%lx", GetLastError());

	int intRetval;

	intRetval = pImageList->Add(mState->mVaRawIcons.get(), mState->mVaTransparentColor);

	g_IconIdx_VaOffset = pImageList->GetImageCount();
	vCatLog("LowLevel", "g_IconIdx_VaOffset %d", g_IconIdx_VaOffset);

	intRetval = pImageList->Add(mState->mVsRawIcons.get(), mState->mVsTransparentColor);

	mState->mUnscaledUnthemedImageList = pImageList;

	HIMAGELIST tmp = ImageList_Duplicate(*pImageList);
	if (tmp)
	{
		pImageList.reset(new CImageList());
		retval = pImageList->Attach(tmp);
		if (!retval)
			vLog("ERROR: ILM attach failed 0x%lx", GetLastError());
	}
	else
		vLog("ERROR: ILM duplicate failed 0x%lx", GetLastError());

	// case 142324
	// speedup startup in vs2010+ as we don't scale and theme all image lists twice
	bool theme = gShellAttr && gShellAttr->IsDevenv11OrHigher() && Psettings && Psettings->mEnableIconTheme;

	Update((theme && g_IdeSettings) ? g_IdeSettings->GetThemeID() : GUID_NULL);
}

ImageListManager::~ImageListManager()
{
	if (this == gImgListMgr)
		gImgListMgr = nullptr;
}

void ImageListManager::Update(const GUID& themeId)
{
	if (!mState)
		return;

	// case 142324 - for DPI unaware context we get only default DPI here
	std::vector<UINT> dpis = VsUI::DpiHelper::GetDPIList();
	std::set<BackgroundType> bgTypes;

	if (!IsEqualGUID(mThemeId, themeId)) // case: 146280 - storing theme ID gives us more control
	{
		// theme has changed, update all
		for (const auto& kvp : mState->mThemedImageLists)
			bgTypes.insert(kvp.first.bgType);

		mState->mThemedImageLists.clear();
		mThemeId = themeId;
	}
	else
	{
		// case 142324
		// remove lists for old DPI
		// we must use UI thread, so update only necessary

		std::map<ImgListKey, std::shared_ptr<CImageList>> preservedLists;

		for (const auto& kvp : mState->mThemedImageLists)
		{
			auto it = std::find(dpis.cbegin(), dpis.cend(), kvp.first.dpi);
			if (dpis.cend() != it)
			{
				preservedLists.insert(kvp); // preserve list if DPI is found
			}

			bgTypes.insert(kvp.first.bgType);
		}

		mState->mThemedImageLists.swap(preservedLists);
	}

	// clang-format off
	bool theme = 
		gShellAttr && gShellAttr->IsDevenv11OrHigher() && 
		Psettings && Psettings->mEnableIconTheme &&
		!IsEqualGUID(themeId, GUID_NULL);
	// clang-format on

	// Update image lists for current DPIs
	for (BackgroundType btIndx : bgTypes)
	{
		COLORREF bgClr = theme ? GetBackgroundColor(btIndx) : CLR_NONE;
		for (UINT dpi : dpis)
		{
			InitList(ImgListKey(btIndx, dpi, bgClr), themeId, false); // false to prevent updates of existing lists
		}
	}
}

void ImageListManager::Update()
{
	// case 146280 - theme ID always should be active theme or GUID_NULL
	_ASSERTE(!g_IdeSettings || IsEqualGUID(mThemeId, g_IdeSettings->GetThemeID()));
	Update(mThemeId);
}

CImageList* ImageListManager::InitList(const ImgListKey& key, GUID themeId, bool forceUpdate)
{
	// case 146280 - theme ID always should be active theme or GUID_NULL
	_ASSERTE(IsEqualGUID(themeId, GUID_NULL) || !g_IdeSettings || IsEqualGUID(mThemeId, g_IdeSettings->GetThemeID()));

	if (!forceUpdate)
	{
		auto it = mState->mThemedImageLists.find(key);
		if (it != mState->mThemedImageLists.end())
			return it->second.get();
	}

	auto dpiScope = VsUI::DpiHelper::SetDefaultForDPI(key.dpi);

	std::shared_ptr<CImageList> tmp(new CImageList());

	BOOL retval = tmp->Create(mState->mUnscaledUnthemedImageList.get());
	if (!retval)
	{
		vLog("ERROR: ILM::TU create failed 0x%lx", GetLastError());
		return nullptr;
	}

//#ifdef _WIN64
	auto replace_images = [tmp, &key]() {
		MonikerImageFunc replace_func = [tmp](const ImageMoniker& mon, CBitmap& bmp, bool isIconIdx, int index) {
			if (!isIconIdx)
				index += ICONIDX_COUNT;

			tmp->Replace(index, &bmp, nullptr);
		};

		ForEachMonikerImage(replace_func, key.bgClr, true, true);
	};
//#endif // _WIN64

	if (IsEqualGUID(themeId, GUID_NULL) || !Psettings || !Psettings->mEnableIconTheme)
	{
		ScaleImageList(*tmp, CLR_NONE);

#if defined(_WIN64) && !defined(VA_CPPUNIT)
		replace_images();
#endif // _WIN64

		mState->mThemedImageLists[key] = tmp;
		return tmp.get();
	}
	else
	{
		bool preVS2022Mode = false;
#if !defined(VA_CPPUNIT)
		preVS2022Mode = gShellAttr->IsDevenv14OrHigher() && !gShellAttr->IsDevenv17OrHigher() && gPkgServiceProvider;
		if (preVS2022Mode)
		{
			int images[2] = {
			    ICONIDX_CHECKALL,
			    ICONIDX_UNCHECKALL};

			for (auto imgId : images)
			{
				// get the pointer to moniker by image ID
				// this is not necessary in case we have moniker already
				auto pMon = ImageListManager::GetMoniker(imgId, true, true);
				if (pMon)
				{
					// use moniker to get bitmap
					CBitmap bmp;
					if (SUCCEEDED(ImageListManager::GetMonikerImage(bmp, *pMon, key.bgClr, 96)))
					{
						// replace image in list
						tmp->Replace(imgId, &bmp, nullptr);
					}
				}
			}
		}
#endif // VA_CPPUNIT

		if(preVS2022Mode || ThemeImageList(*tmp, key.bgClr))
		{
			ScaleImageList(*tmp, key.bgClr);

#if defined(_WIN64) && !defined(VA_CPPUNIT)
			replace_images();
#endif // _WIN64

			mState->mThemedImageLists[key] = tmp;
			return tmp.get();
		}
		else
		{
			vLogUnfiltered("ERROR: ILM::TU Theming failed");
			return nullptr;
		}
	}
}

void ImageListManager::ThemeUpdated()
{
	if (!gShellAttr || !gShellAttr->IsDevenv11OrHigher() || !g_IdeSettings || !Psettings ||
	    !Psettings->mEnableIconTheme)
	{
		// images aren't theme dependent in older IDEs
		return;
	}

	CatLog("LowLevel", "ILM::TU");

	Update(g_IdeSettings->GetThemeID());
}

CImageList* ImageListManager::GetImgList(const ImgListKey& key)
{
	return InitList(key, mThemeId, false);
}

CImageList* ImageListManager::GetImgList(BackgroundType bt, UINT dpi, COLORREF bgClr)
{
	if (!dpi)
		dpi = (UINT)VsUI::DpiHelper::GetDeviceDpiX();

	auto key = ImgListKey(bt, dpi, bgClr);

	return InitList(key, mThemeId, false);
}

CImageList* ImageListManager::GetImgList(HWND hWnd, UINT dpi /*= 0*/)
{
	if (!dpi)
		dpi = VsUI::CDpiAwareness::GetDpiForWindow(hWnd, false);

	ImageListManager::BackgroundType bt;
	if (GetWindowBackgroundType(bt, hWnd))
	{
		return GetImgList(bt, dpi, IsEqualGUID(mThemeId, GUID_NULL) ? CLR_NONE : GetBackgroundColor(bt));
	}

	return nullptr;
}

CImageList* ImageListManager::GetImgList(BackgroundType bt, UINT dpi /*= 0*/)
{
	auto bgClr = IsEqualGUID(mThemeId, GUID_NULL) ? CLR_NONE : GetBackgroundColor(bt);
	return GetImgList(bt, dpi, bgClr);
}

UINT ImageListManager::GetImgListDPI(const CImageList& il)
{
	for (const auto& kvp : mState->mThemedImageLists)
	{
		if (kvp.second && kvp.second->m_hImageList == (HIMAGELIST)il)
		{
			return kvp.first.dpi;
		}
	}

	return 0;
}

template <class WINDOW, class... ARGTYPES>
CImageList* SetImageListForDPI(ImageListManager* mngr, ImageListManager::BackgroundType bt, WINDOW& wnd,
                               ARGTYPES&&... _Args)
{
	UINT dpi = 0;

	auto thrdState = AfxGetThreadState();

	if (thrdState &&
	    thrdState->m_msgCur.hwnd == wnd.m_hWnd &&
	    thrdState->m_msgCur.message == WM_DPICHANGED &&
	    thrdState->m_msgCur.wParam)
	{
		dpi = (UINT)HIWORD(thrdState->m_msgCur.wParam);
	}

	if (!dpi)
		dpi = VsUI::CDpiAwareness::GetDpiForWindow(wnd);

	VADEBUGPRINT("#DPI SetImgList DPI: " << dpi << " WHND: " << wnd.m_hWnd);

	ImageListManager::SetWindowBackgroundType(wnd, bt);

	auto list = mngr->GetImgList(bt, dpi);
	if (list)
		return wnd.SetImageList(list, _Args...);

	return nullptr;
}

CImageList* ImageListManager::SetImgListForDPI(CTreeCtrl& tree, BackgroundType bt, int tvsil)
{
	return ::SetImageListForDPI<CTreeCtrl>(this, bt, tree, tvsil);
}

CImageList* ImageListManager::SetImgListForDPI(CListCtrl& list, BackgroundType bt, int lvsil)
{
	return ::SetImageListForDPI<CListCtrl>(this, bt, list, lvsil);
}

CImageList* ImageListManager::SetImgListForDPI(CComboBoxEx& wnd, BackgroundType bt)
{
	return ::SetImageListForDPI<CComboBoxEx>(this, bt, wnd);
}

CImageList* ImageListManager::SetImgListForDPI(CHeaderCtrl& wnd, BackgroundType bt)
{
	return ::SetImageListForDPI<CHeaderCtrl>(this, bt, wnd);
}

CImageList* ImageListManager::SetImgListForDPI(CTabCtrl& wnd, BackgroundType bt)
{
	return ::SetImageListForDPI<CTabCtrl>(this, bt, wnd);
}

void ImageListManager::SetWindowBackgroundType(HWND hWnd, BackgroundType bt)
{
	mySetProp(hWnd, "_imgListBT", (HANDLE)(intptr_t)(1 + bt));
}

bool ImageListManager::GetWindowBackgroundType(BackgroundType& bt, HWND hWnd)
{
	auto btInt = (int)(intptr_t)myGetProp(hWnd, "_imgListBT");
	if (btInt)
	{
		bt = (BackgroundType)(btInt - 1);
		return true;
	}

	return false;
}

template <class WINDOW, class... ARGTYPES>
bool UpdateImageListForDPI(ImageListManager* mngr, UINT dpi, WINDOW& wnd, ARGTYPES&&... _Args)
{
	VADEBUGPRINT("#DPI UpdateImageListForDPI: " << dpi);

	auto oldList = wnd.GetImageList(_Args...); // update only existing
	if (oldList)
	{
		auto newList = mngr->GetImgList(wnd, dpi);
		if (newList)
		{
			wnd.SetImageList(newList, _Args...);
			return true;
		}
	}

	return false;
}

bool ImageListManager::UpdateImageListsForDPI(CTreeCtrl& wnd, UINT dpiOverride /*= 0*/)
{
	UINT dpi = dpiOverride ? dpiOverride : VsUI::CDpiAwareness::GetDpiForWindow(wnd);
	bool result = false;
	result |= ::UpdateImageListForDPI(this, dpi, wnd, TVSIL_NORMAL);
	result |= ::UpdateImageListForDPI(this, dpi, wnd, TVSIL_STATE);
	return result;
}

bool ImageListManager::UpdateImageListsForDPI(CListCtrl& wnd, UINT dpiOverride /*= 0*/)
{
	UINT dpi = dpiOverride ? dpiOverride : VsUI::CDpiAwareness::GetDpiForWindow(wnd);
	bool result = false;
	result |= ::UpdateImageListForDPI(this, dpi, wnd, LVSIL_NORMAL);
	result |= ::UpdateImageListForDPI(this, dpi, wnd, LVSIL_SMALL);
	result |= ::UpdateImageListForDPI(this, dpi, wnd, LVSIL_STATE);
	return result;
}

bool ImageListManager::UpdateImageListsForDPI(CComboBoxEx& wnd, UINT dpiOverride /*= 0*/)
{
	UINT dpi = dpiOverride ? dpiOverride : VsUI::CDpiAwareness::GetDpiForWindow(wnd);
	return ::UpdateImageListForDPI(this, dpi, wnd);
}

bool ImageListManager::UpdateImageListsForDPI(CHeaderCtrl& wnd, UINT dpiOverride /*= 0*/)
{
	UINT dpi = dpiOverride ? dpiOverride : VsUI::CDpiAwareness::GetDpiForWindow(wnd);
	return ::UpdateImageListForDPI(this, dpi, wnd);
}

bool ImageListManager::UpdateImageListsForDPI(CTabCtrl& wnd, UINT dpiOverride /*= 0*/)
{
	UINT dpi = dpiOverride ? dpiOverride : VsUI::CDpiAwareness::GetDpiForWindow(wnd);
	return ::UpdateImageListForDPI(this, dpi, wnd);
}

bool ImageListManager::TryUpdateImageListsForDPIDynamic(CWnd& wnd, UINT dpiOverride)
{
	if (wnd.IsKindOf(RUNTIME_CLASS(CTreeCtrl)))
		return UpdateImageListsForDPI(*(CTreeCtrl*)&wnd, dpiOverride);
	else if (wnd.IsKindOf(RUNTIME_CLASS(CListCtrl)))
		return UpdateImageListsForDPI(*(CListCtrl*)&wnd, dpiOverride);
	else if (wnd.IsKindOf(RUNTIME_CLASS(CComboBoxEx)))
		return UpdateImageListsForDPI(*(CComboBoxEx*)&wnd, dpiOverride);
	else if (wnd.IsKindOf(RUNTIME_CLASS(CHeaderCtrl)))
		return UpdateImageListsForDPI(*(CHeaderCtrl*)&wnd, dpiOverride);
	else if (wnd.IsKindOf(RUNTIME_CLASS(CTabCtrl)))
		return UpdateImageListsForDPI(*(CTabCtrl*)&wnd, dpiOverride);

	return false;
}

void ImageListManager::GetRawBitmaps(CBitmap** vsIcons, CBitmap** vaIcons, COLORREF& vsTrans, COLORREF& vaTrans)
{
	*vsIcons = mState->mVsRawIcons.get();
	vsTrans = mState->mVsTransparentColor;

	*vaIcons = mState->mVaRawIcons.get();
	vaTrans = mState->mVaTransparentColor;
}

int ImageListManager::ScaleImageList(CImageList& il, COLORREF bgClr)
{
	CImageList scaledImgList;

	// don't Create(...) scaled - ScaleImageListEx does it for us

	auto rslt = ScaleImageListEx(il, scaledImgList, bgClr);

	if (rslt)
	{
		il.DeleteImageList();
		il.Attach(scaledImgList.Detach());
	}

	return rslt;
}

//#ifdef _WIN64
// clang-format off

static const GUID GUID_VaImageMoniker =
	{ 0x0F7606F3, 0x8EAD, 0x42ED, { 0x8D, 0x5A, 0xED, 0xF9, 0x08, 0x34, 0xB5, 0x0E } };

// VA Tool Icons (those used in VA Extension menu)
static const ImageMoniker VAMonikerTool_EnableDisable						= { GUID_VaImageMoniker, 1  };
static const ImageMoniker VAMonikerTool_FindNextByContext					= { GUID_VaImageMoniker, 2  };
static const ImageMoniker VAMonikerTool_FindPreviousByContext				= { GUID_VaImageMoniker, 3  };
static const ImageMoniker VAMonikerTool_FindSymbolDialog					= { GUID_VaImageMoniker, 4  };
static const ImageMoniker VAMonikerTool_GotoImplementation					= { GUID_VaImageMoniker, 5  };
static const ImageMoniker VAMonikerTool_InsertCodeTemplate					= { GUID_VaImageMoniker, 6  };
static const ImageMoniker VAMonikerTool_ListMethodsInCurrentFile			= { GUID_VaImageMoniker, 7  };
static const ImageMoniker VAMonikerTool_OpenContextMenu						= { GUID_VaImageMoniker, 8  };
static const ImageMoniker VAMonikerTool_OpenCorrespondingHorCPP				= { GUID_VaImageMoniker, 9  };
static const ImageMoniker VAMonikerTool_OpenFileInWorkspaceDialog			= { GUID_VaImageMoniker, 10 };
static const ImageMoniker VAMonikerTool_Options								= { GUID_VaImageMoniker, 11 };
static const ImageMoniker VAMonikerTool_Paste								= { GUID_VaImageMoniker, 12 };
static const ImageMoniker VAMonikerTool_ReparseCurrentFile					= { GUID_VaImageMoniker, 13 };
static const ImageMoniker VAMonikerTool_ScopeNext							= { GUID_VaImageMoniker, 14 };
static const ImageMoniker VAMonikerTool_ScopePrevious						= { GUID_VaImageMoniker, 15 };
static const ImageMoniker VAMonikerTool_SortSelectedLines					= { GUID_VaImageMoniker, 16 };
static const ImageMoniker VAMonikerTool_SpellCheck							= { GUID_VaImageMoniker, 17 };
static const ImageMoniker VAMonikerTool_SurroundSelectionWithBraces			= { GUID_VaImageMoniker, 18 };
static const ImageMoniker VAMonikerTool_SurroundSelectionWithComment		= { GUID_VaImageMoniker, 19 };
static const ImageMoniker VAMonikerTool_SurroundSelectionWithIfdefOrRegion	= { GUID_VaImageMoniker, 20 };
static const ImageMoniker VAMonikerTool_SurroundSelectionWithParentheses	= { GUID_VaImageMoniker, 21 };
static const ImageMoniker VAMonikerTool_ToggleColoring						= { GUID_VaImageMoniker, 22 };
static const ImageMoniker VAMonikerTool_ToggleRepairCase					= { GUID_VaImageMoniker, 23 };
static const ImageMoniker VAMonikerTool_ToggleSuggestions					= { GUID_VaImageMoniker, 24 };
static const ImageMoniker VAMonikerTool_ToggleUnderlining					= { GUID_VaImageMoniker, 25 };
static const ImageMoniker VAMonikerTool_VAViewFIW							= { GUID_VaImageMoniker, 27 };
static const ImageMoniker VAMonikerTool_VAViewSIW							= { GUID_VaImageMoniker, 28 };
static const ImageMoniker VAMonikerTool_NavigateBack						= { GUID_VaImageMoniker, 29 };
static const ImageMoniker VAMonikerTool_NavigateForward						= { GUID_VaImageMoniker, 30 };
static const ImageMoniker VAMonikerTool_FindReferences						= { GUID_VaImageMoniker, 35 };
static const ImageMoniker VAMonikerTool_VAView								= { GUID_VaImageMoniker, 36 };
static const ImageMoniker VAMonikerTool_VaOutline							= { GUID_VaImageMoniker, 37 };
static const ImageMoniker VAMonikerTool_VaHashtags							= { GUID_VaImageMoniker, 38 };

// VA icons mainly corresponding to ICONIDX_ enum - see #VaIconMonikers array below
static const ImageMoniker VAMoniker_CPPHeader								= { GUID_VaImageMoniker, 100 };
static const ImageMoniker VAMoniker_ListItem								= { GUID_VaImageMoniker, 101 };
static const ImageMoniker VAMoniker_ListItemHashtags						= { GUID_VaImageMoniker, 102 };
static const ImageMoniker VAMoniker_NextDown								= { GUID_VaImageMoniker, 103 };
static const ImageMoniker VAMoniker_NextInList								= { GUID_VaImageMoniker, 104 };
static const ImageMoniker VAMoniker_PreviousInList							= { GUID_VaImageMoniker, 105 };
static const ImageMoniker VAMoniker_PreviousUp								= { GUID_VaImageMoniker, 106 };
static const ImageMoniker VAMoniker_RefsClone								= { GUID_VaImageMoniker, 107 };
static const ImageMoniker VAMoniker_RefsInherited							= { GUID_VaImageMoniker, 108 };
static const ImageMoniker VAMoniker_RefsFromAllProjects						= { GUID_VaImageMoniker, 109 };
static const ImageMoniker VAMoniker_Tomato									= { GUID_VaImageMoniker, 110 };
static const ImageMoniker VAMoniker_TomatoBackground						= { GUID_VaImageMoniker, 111 };
static const ImageMoniker VAMoniker_NonInheritedFirst						= { GUID_VaImageMoniker, 112 };
static const ImageMoniker VAMoniker_CommentLine								= { GUID_VaImageMoniker, 113 };
static const ImageMoniker VAMoniker_Delete									= { GUID_VaImageMoniker, 114 };
static const ImageMoniker VAMoniker_ScopeGlobal								= { GUID_VaImageMoniker, 115 };
static const ImageMoniker VAMoniker_ScopeProject							= { GUID_VaImageMoniker, 116 };
static const ImageMoniker VAMoniker_ScopeLocal								= { GUID_VaImageMoniker, 117 };
static const ImageMoniker VAMoniker_ScopeCurrent							= { GUID_VaImageMoniker, 118 };
static const ImageMoniker VAMoniker_ReferenceGotoDeclaration				= { GUID_VaImageMoniker, 119 };
static const ImageMoniker VAMoniker_ReferenceGotoDefinition					= { GUID_VaImageMoniker, 120 };
static const ImageMoniker VAMoniker_ReferenceAssign							= { GUID_VaImageMoniker, 121 };
static const ImageMoniker VAMoniker_Reference								= { GUID_VaImageMoniker, 122 };

// special moniker for unused indexes and other blank images
static const ImageMoniker VAMoniker_Empty									= { GUID_VaImageMoniker, 1000 };

// 2 rows define 1 type of icon =  6 entries per type moniker
ImageMoniker VSMonikers[] = {
	KnownMonikers::ClassPublic, KnownMonikers::ClassInternal, KnownMonikers::ClassInternal,
	KnownMonikers::ClassProtected, KnownMonikers::ClassPrivate, KnownMonikers::ClassShortcut,
	KnownMonikers::ConstantPublic, KnownMonikers::ConstantInternal, KnownMonikers::ConstantInternal,
	KnownMonikers::ConstantProtected, KnownMonikers::ConstantPrivate, KnownMonikers::ConstantShortcut,
	KnownMonikers::DelegatePublic, KnownMonikers::DelegateInternal, KnownMonikers::DelegateInternal,
	KnownMonikers::DelegateProtected, KnownMonikers::DelegatePrivate, KnownMonikers::DelegateShortcut,
	KnownMonikers::EnumerationPublic, KnownMonikers::EnumerationInternal, KnownMonikers::EnumerationInternal,
	KnownMonikers::EnumerationProtected, KnownMonikers::EnumerationPrivate, KnownMonikers::EnumerationShortcut,
	KnownMonikers::EnumerationItemPublic, KnownMonikers::EnumerationItemInternal, KnownMonikers::EnumerationItemInternal,
	KnownMonikers::EnumerationItemProtected, KnownMonikers::EnumerationItemPrivate, KnownMonikers::EnumerationItemShortcut,
	KnownMonikers::EventPublic, KnownMonikers::EventInternal, KnownMonikers::EventInternal,
	KnownMonikers::EventProtected, KnownMonikers::EventPrivate, KnownMonikers::EventShortcut,
	KnownMonikers::ExceptionPublic, KnownMonikers::ExceptionInternal, KnownMonikers::ExceptionInternal,
	KnownMonikers::ExceptionProtected, KnownMonikers::ExceptionPrivate, KnownMonikers::ExceptionShortcut,
	KnownMonikers::FieldPublic, KnownMonikers::FieldInternal, KnownMonikers::FieldInternal,
	KnownMonikers::FieldProtected, KnownMonikers::FieldPrivate, KnownMonikers::FieldShortcut,
	KnownMonikers::InterfacePublic, KnownMonikers::InterfaceInternal, KnownMonikers::InterfaceInternal,
	KnownMonikers::InterfaceProtected, KnownMonikers::InterfacePrivate, KnownMonikers::InterfaceShortcut,
	KnownMonikers::MacroPublic, KnownMonikers::MacroInternal, KnownMonikers::MacroInternal,
	KnownMonikers::MacroProtected, KnownMonikers::MacroPrivate, KnownMonikers::MacroShortcut,
	KnownMonikers::MapPublic, KnownMonikers::MapInternal, KnownMonikers::MapInternal,
	KnownMonikers::MapProtected, KnownMonikers::MapPrivate, KnownMonikers::MapShortcut,
	KnownMonikers::MapItemPublic, KnownMonikers::MapItemInternal, KnownMonikers::MapItemInternal,
	KnownMonikers::MapItemProtected, KnownMonikers::MapItemPrivate, KnownMonikers::MapItemShortcut,
	KnownMonikers::MethodPublic, KnownMonikers::MethodInternal, KnownMonikers::MethodInternal,
	KnownMonikers::MethodProtected, KnownMonikers::MethodPrivate, KnownMonikers::MethodShortcut,
	KnownMonikers::MethodPublic, KnownMonikers::MethodInternal, KnownMonikers::MethodInternal,
	KnownMonikers::MethodProtected, KnownMonikers::MethodPrivate, KnownMonikers::MethodShortcut,
	KnownMonikers::ModulePublic, KnownMonikers::ModuleInternal, KnownMonikers::ModuleInternal,
	KnownMonikers::ModuleProtected, KnownMonikers::ModulePrivate, KnownMonikers::ModuleShortcut,
	KnownMonikers::NamespacePublic, KnownMonikers::NamespaceInternal, KnownMonikers::NamespaceInternal,
	KnownMonikers::NamespaceProtected, KnownMonikers::NamespacePrivate, KnownMonikers::NamespaceShortcut,
	KnownMonikers::OperatorPublic, KnownMonikers::OperatorInternal, KnownMonikers::OperatorInternal,
	KnownMonikers::OperatorProtected, KnownMonikers::OperatorPrivate, KnownMonikers::OperatorShortcut,
	KnownMonikers::PropertyPublic, KnownMonikers::PropertyInternal, KnownMonikers::PropertyInternal,
	KnownMonikers::PropertyProtected, KnownMonikers::PropertyPrivate, KnownMonikers::PropertyShortcut,
	KnownMonikers::StructurePublic, KnownMonikers::StructureInternal, KnownMonikers::StructureInternal,
	KnownMonikers::StructureProtected, KnownMonikers::StructurePrivate, KnownMonikers::StructureShortcut,
	KnownMonikers::TemplatePublic, KnownMonikers::TemplateInternal, KnownMonikers::TemplateInternal,
	KnownMonikers::TemplateProtected, KnownMonikers::TemplatePrivate, KnownMonikers::TemplateShortcut,
	KnownMonikers::TypeDefinitionPublic, KnownMonikers::TypeDefinitionInternal, KnownMonikers::TypeDefinitionInternal,
	KnownMonikers::TypeDefinitionProtected, KnownMonikers::TypeDefinitionPrivate, KnownMonikers::TypeDefinitionShortcut,
	KnownMonikers::TypePublic, KnownMonikers::TypeInternal, KnownMonikers::TypeInternal,
	KnownMonikers::TypeProtected, KnownMonikers::TypePrivate, KnownMonikers::TypeShortcut,
	KnownMonikers::UnionPublic, KnownMonikers::UnionInternal, KnownMonikers::UnionInternal,
	KnownMonikers::UnionProtected, KnownMonikers::UnionPrivate, KnownMonikers::UnionShortcut,
	KnownMonikers::FieldPublic, KnownMonikers::FieldInternal, KnownMonikers::FieldInternal,
	KnownMonikers::FieldProtected, KnownMonikers::FieldPrivate, KnownMonikers::FieldShortcut,
	KnownMonikers::ValueTypePublic, KnownMonikers::ValueTypeInternal, KnownMonikers::ValueTypeInternal,
	KnownMonikers::ValueTypeProtected, KnownMonikers::ValueTypePrivate, KnownMonikers::ValueTypeShortcut,
	KnownMonikers::ObjectPublic, KnownMonikers::ObjectInternal, KnownMonikers::ObjectInternal,
	KnownMonikers::ObjectProtected, KnownMonikers::ObjectPrivate, KnownMonikers::ObjectShortcut,
	KnownMonikers::MethodPublic, KnownMonikers::MethodInternal, KnownMonikers::MethodInternal,
	KnownMonikers::MethodProtected, KnownMonikers::MethodPrivate, KnownMonikers::MethodShortcut,
	KnownMonikers::FieldPublic, KnownMonikers::FieldInternal, KnownMonikers::FieldInternal,
	KnownMonikers::FieldProtected, KnownMonikers::FieldPrivate, KnownMonikers::FieldShortcut,
	KnownMonikers::StructurePublic, KnownMonikers::StructureInternal, KnownMonikers::StructureInternal,
	KnownMonikers::StructureProtected, KnownMonikers::StructurePrivate, KnownMonikers::StructureShortcut,
	KnownMonikers::NamespacePublic, KnownMonikers::NamespaceInternal, KnownMonikers::NamespaceInternal,
	KnownMonikers::NamespaceProtected, KnownMonikers::NamespacePrivate, KnownMonikers::NamespaceShortcut,
	KnownMonikers::InterfacePublic, KnownMonikers::InterfaceInternal, KnownMonikers::InterfaceInternal,
	KnownMonikers::InterfaceProtected, KnownMonikers::InterfacePrivate, KnownMonikers::InterfaceShortcut
};

// #VaIconMonikers
ImageMoniker VaIconMonikers[] =
{
	VAMoniker_Tomato,									//	ICONIDX_TOMATO = 0,
	KnownMonikers::RecursivelyCheckAll,					//	ICONIDX_CHECKALL
	KnownMonikers::RecursivelyUncheckAll,				//	ICONIDX_UNCHECKALL
	VAMonikerTool_FindSymbolDialog,						//	ICONIDX_SIW,
	VAMonikerTool_OpenFileInWorkspaceDialog,			//	ICONIDX_FIW,
	VAMoniker_Empty,									//	ICONIDX_UNUSED_3,
	VAMonikerTool_OpenCorrespondingHorCPP,				//	ICONIDX_OPEN_OPPOSITE,
	VAMonikerTool_InsertCodeTemplate,					//	ICONIDX_VATEMPLATE,
	VAMoniker_Empty,									//	ICONIDX_UNUSED_4,
	VAMoniker_Empty,									//	ICONIDX_UNUSED_5,
	VAMonikerTool_ReparseCurrentFile,					//	ICONIDX_REPARSE,
	VAMoniker_NonInheritedFirst,						//	ICONIDX_SHOW_NONINHERITED_ITEMS_FIRST,
	VAMoniker_Empty,									//	ICONIDX_UNUSED_6,
	VAMoniker_Empty,									//	ICONIDX_UNUSED_7,
	VAMoniker_ScopeGlobal,								//	ICONIDX_SCOPESYS,						
	VAMoniker_ScopeProject,								//	ICONIDX_SCOPEPROJECT,					
	VAMoniker_ScopeLocal,								//	ICONIDX_SCOPELOCAL,						
	VAMoniker_ScopeCurrent,								//	ICONIDX_SCOPECUR,						

	VAMonikerTool_FindReferences,						//	ICONIDX_REFERENCE_FIND_REF,
	VAMoniker_Empty,									//	ICONIDX_UNUSED_8,
	VAMoniker_Empty,									//	ICONIDX_UNUSED_9,
	VAMoniker_ReferenceGotoDeclaration,					//	ICONIDX_REFERENCE_GOTO_DECL,
	VAMoniker_ReferenceAssign,							//	ICONIDX_REFERENCEASSIGN,				
	VAMoniker_Reference,								//	ICONIDX_REFERENCE,						
	VAMoniker_ReferenceGotoDefinition,					//	ICONIDX_REFERENCE_GOTO_DEF,
	KnownMonikers::Rename,								//	ICONIDX_REFACTOR_RENAME,
	KnownMonikers::ExtractMethod,						//	ICONIDX_REFACTOR_EXTRACT_METHOD,
	KnownMonikers::EncapsulateField,					//	ICONIDX_REFACTOR_ENCAPSULATE_FIELD,
	KnownMonikers::ModifyMethod,						//	ICONIDX_REFACTOR_CHANGE_SIGNATURE,			#MonikerIconUnrelated
	KnownMonikers::AddNamespace,						//	ICONIDX_REFACTOR_INSERT_USING_STATEMENT,	#MonikerIconUnrelated
	VAMoniker_Empty,									//	ICONIDX_BLANK,
	VAMoniker_ListItem,									//	ICONIDX_BULLET,
	VAMoniker_ListItemHashtags,							//	ICONIDX_COMMENT_BULLET,

	VAMoniker_TomatoBackground,							//	ICONIDX_TOMATO_BACKGROUND,
	KnownMonikers::FolderClosed,						//	ICONIDX_FILE_FOLDER
	KnownMonikers::CPPFileNode,							//	ICONIDX_FILE_CPP,
	VAMoniker_CPPHeader,								//	ICONIDX_FILE_H,
	KnownMonikers::CSFileNode,							//	ICONIDX_FILE_CS,
	KnownMonikers::TextFile,							//	ICONIDX_FILE_TXT,
	KnownMonikers::Document,							//	ICONIDX_FILE,
	KnownMonikers::HTMLFile,							//	ICONIDX_FILE_HTML,
	KnownMonikers::VBFileNode,							//	ICONIDX_FILE_VB,
	KnownMonikers::EditDocument,						//	ICONIDX_MODFILE,
	KnownMonikers::ModifyMethod,						//	ICONIDX_MODMETHOD,

	VAMoniker_Empty,									//	ICONIDX_UNUSED_11,
	KnownMonikers::Cut,									//	ICONIDX_CUT,
	KnownMonikers::Copy,								//	ICONIDX_COPY,
	KnownMonikers::Paste,								//	ICONIDX_PASTE,
	VAMoniker_Delete,									//	ICONIDX_DELETE,							
	VAMoniker_Empty,									//	ICONIDX_UNUSED_12,
	KnownMonikers::KeywordSnippet,						//	ICONIDX_SCOPE_SUGGEST_V11,				#MonikerIconUnrelated
	KnownMonikers::ApplyCodeChanges,					//	ICONIDX_EXPANSION,						#MonikerIconUnrelated
	KnownMonikers::Lock,								//	ICONIDX_RESWORD,						#MonikerIconUnrelated
	KnownMonikers::QuestionMark,						//	ICONIDX_SUGGESTION,						#MonikerIconUnrelated

	VAMoniker_CommentLine,								//	ICONIDX_SNIPPET_COMMENTLINE,			
	VAMonikerTool_SurroundSelectionWithBraces,			//	ICONIDX_SNIPPET_BRACKETS,
	VAMonikerTool_SurroundSelectionWithComment,			//	ICONIDX_SNIPPET_COMMENTBLOCK,
	VAMonikerTool_SurroundSelectionWithIfdefOrRegion,	//	ICONIDX_SNIPPET_IFDEF,
	VAMonikerTool_SurroundSelectionWithParentheses,		//	ICONIDX_SNIPPET_PARENS,

#ifdef _WIN64
#ifndef VA_CPPUNIT
	KnownMonikers::BreakpointAvailable,					//	ICONIDX_BREAKPOINT_ADD,					#MonikerIconUnrelated	#MonikerIcon17
	KnownMonikers::ClearBreakpointGroup,				//	ICONIDX_BREAKPOINT_REMOVE_ALL,			#MonikerIconUnrelated
	KnownMonikers::DisableAllBreakpoints,				//	ICONIDX_BREAKPOINT_DISABLE_ALL,			#MonikerIconUnrelated
	KnownMonikers::BreakpointDisabled,					//	ICONIDX_BREAKPOINT_DISABLE,				#MonikerIconUnrelated
	KnownMonikers::Asterisk,							//	ICONIDX_REFERENCE_CREATION, // #63
	KnownMonikers::ClearBookmark,						//	ICONIDX_BOOKMARK_REMOVE,
	KnownMonikers::OverridingOverridden,				//	ICONIDX_REFERENCEASSIGN_OVERRIDDEN,		#MonikerIconUnrelated	#MonikerIcon17
	KnownMonikers::Overridden,							//	ICONIDX_REFERENCE_OVERRIDDEN,			#MonikerIconUnrelated	#MonikerIcon17
	KnownMonikers::Snippet,								//	ICONIDX_VS11_SNIPPET
#endif
#endif // _WIN64
};

#ifdef _WIN64
#ifndef VA_CPPUNIT
static_assert((long long)ICONIDX_COUNT == (long long)(sizeof(VaIconMonikers) / sizeof(*VaIconMonikers)));
#endif
#endif // _WIN64

std::map<int, ImageMoniker> VABmpIdToMonikerMap = {
	{ IDB_REFSNEXT,				VAMoniker_NextDown				},
	{ IDB_REFSPREV,				VAMoniker_PreviousUp			},
	{ IDB_REFSNEXTINGROUP,		VAMoniker_NextInList			},
	{ IDB_REFSPREVINGROUP,		VAMoniker_PreviousInList		},
	{ IDB_TOGGLE_GROUPBYFILE,	KnownMonikers::CategorizedView	},
	{ IDB_REFSREFRESH,			KnownMonikers::Refresh			},
	{ IDB_REFSFIND,				KnownMonikers::Search			},
	{ IDB_TOGGLE_INHERITANCE,	VAMoniker_RefsInherited			},
	{ IDB_TOGGLE_ALLPROJECTS,	VAMoniker_RefsFromAllProjects	},
	{ IDB_REFSCLONE,			VAMoniker_RefsClone				},
};

// clang-format on

const ImageMoniker* ImageListManager::GetMoniker(int index, bool ideIcons /*= true*/, bool vaIcons /*= false*/)
{
	if (ideIcons && index >= g_IconIdx_VaOffset &&
	    index <= g_IconIdx_VaOffset + (long long)(sizeof(VSMonikers) / sizeof(*VSMonikers)))
	{
		return &VSMonikers[index - g_IconIdx_VaOffset];
	}

	if (vaIcons && index >= 0 && index <= (long long)(sizeof(VaIconMonikers) / sizeof(*VaIconMonikers)))
	{
		return &VaIconMonikers[index];
	}

	VADEBUGPRINT("#ICO Unresolved Moniker: " << index);

	return nullptr;
}

bool ImageListManager::ImageMonikerDraw(int img, HDC hdcDst, COLORREF bgColor, const POINT& pt)
{
	auto mon = GetMoniker(img, true, true);
	if (mon != nullptr)
	{
		CBitmap monImg;
		if (SUCCEEDED(GetMonikerImage(monImg, *mon, bgColor, 0)) && monImg.m_hObject)
		{
			return ThemeUtils::DrawImage(hdcDst, monImg, pt);
		}
	}
	return false;
}

int ImageListManager::ForEachMonikerImage(const ImageListManager::MonikerImageFunc& func, COLORREF bgClr,
                                          bool ideIcons /*= true*/, bool vaIcons /* = false*/, bool opaque /*= false*/)
{
	if (!ideIcons && !vaIcons)
	{
		_ASSERTE(!"ImageListManager::FillWithMonikerImages nothing to add!");
		return 0;
	}

	int count = 0;

	try
	{
		CComPtr<IVsImageService2> spImgSvc;
#ifndef VA_CPPUNIT
		if (SUCCEEDED(gPkgServiceProvider->QueryService(SID_SVsImageService, &spImgSvc)) && spImgSvc)
#else
		if (false)
#endif // VA_CPPUNIT
		{
			ImageAttributes attr = {0};
			attr.StructSize = sizeof(attr);
			attr.Format = DF_Win32;
			// IT_Bitmap for HBITMAP, IT_Icon for HICON, IT_ImageList for HIMAGELIST
			attr.ImageType = IT_Bitmap;
			attr.LogicalWidth = 16;
			attr.LogicalHeight = 16;
			attr.Dpi = VsUI::DpiHelper::GetDeviceDpiY();
			// Desired RGBA color, if you don't use this, don't set IAF_Background below
			attr.Background = bgClr;
			attr.Flags = (ImageAttributesFlags)(IAF_RequiredFlags | IAF_Background);
	
			auto process_moniker = [&](const ImageMoniker& mon, bool is_vaIcon, int local_index) {
				try
				{
					CComPtr<IVsUIObject> uiObj;
					if (SUCCEEDED(spImgSvc->GetImage(mon, attr, &uiObj)) && uiObj)
					{
						CComVariant dataVar;
						if (SUCCEEDED(uiObj->get_Data(&dataVar)) && dataVar.pdispVal)
						{
							CComQIPtr<IVsUIWin32Bitmap> pUIBmp(dataVar.pdispVal);
							if (pUIBmp)
							{
								CBitmap bmp;
								bool detach = false;
		
								if (opaque)
								{
									BitmapFromVsUIWin32Bitmap(pUIBmp, bmp, bgClr, nullptr);
								}
		
								if (!bmp.m_hObject)
								{
									INT_PTR hBmp = 0;
									if (pUIBmp && SUCCEEDED(pUIBmp->GetHBITMAP(&hBmp)))
									{
										detach = !!bmp.Attach((HBITMAP)hBmp);
									}
								}
		
								if (bmp.m_hObject)
								{
									func(mon, bmp, is_vaIcon, local_index);
		
									count++;
		
									if (detach)
										bmp.Detach();
								}
							}
						}
					}
				}
				catch (...)
				{
					vLog("ERROR: ImageListManager::ForEachMonikerImage failed on %d", local_index);
				}
			};
	
			if (vaIcons)
			{
				int i = 0;
				for (const ImageMoniker& mon : VaIconMonikers)
				{
					process_moniker(mon, true, i);
					i++;
				}
			}
	
			if (ideIcons)
			{
				int i = 0;
				for (const ImageMoniker& mon : VSMonikers)
				{
					process_moniker(mon, false, i);
					i++;
				}
			}
		}
	}
	catch (...)
	{
		vLog("ERROR: ImageListManager::ForEachMonikerImage failed");
	}

	return count;
}

int ImageListManager::GetMonikerImageFromResourceId(CBitmap& bmpOut, int rscId, COLORREF bgClr, UINT dpi,
                                                    bool grayScale /*= false*/, bool highContrast /*= false*/,
                                                    bool opaque /*= false*/, RECT* margin /*= nullptr*/)
{
	auto map_entry = VABmpIdToMonikerMap.find(rscId);
	if (map_entry == VABmpIdToMonikerMap.cend())
		return E_FAIL;

	return GetMonikerImage(bmpOut, map_entry->second, bgClr, dpi, grayScale, highContrast, opaque, margin);
}

int ImageListManager::GetMonikerImage(CBitmap& bmpOut, const ImageMoniker& moniker, COLORREF bgClr, UINT dpi,
                                      bool grayScale /*= false*/, bool highContrast /*= false*/,
                                      bool opaque /*= false*/, RECT* margin /*= nullptr*/)
{
	HRESULT hr = E_FAIL;

	try
	{
		CComPtr<IVsImageService2> spImgSvc;
#ifndef VA_CPPUNIT
		if (SUCCEEDED(hr = gPkgServiceProvider->QueryService(SID_SVsImageService, &spImgSvc)) && spImgSvc)
#else
		if (false)
#endif
		{
			ImageAttributes attr = {0};
			attr.StructSize = sizeof(attr);
			attr.Format = DF_Win32;
			// IT_Bitmap for HBITMAP, IT_Icon for HICON, IT_ImageList for HIMAGELIST
			attr.ImageType = IT_Bitmap;
			attr.LogicalWidth = 16;
			attr.LogicalHeight = 16;
			attr.Dpi = dpi ? (int)dpi : VsUI::DpiHelper::GetDeviceDpiY();
			attr.Flags = (ImageAttributesFlags)(IAF_RequiredFlags);
	
			if (bgClr != CLR_NONE)
			{
				attr.Background = bgClr;
				attr.Flags |= IAF_Background;
			}
	
			if (grayScale)
			{
				attr.Flags |= IAF_Grayscale;
			}
	
			if (highContrast)
			{
				attr.Flags |= IAF_HighContrast;
			}
	
			CComPtr<IVsUIObject> uiObj;
			if (SUCCEEDED(hr = spImgSvc->GetImage(moniker, attr, &uiObj)) && uiObj)
			{
				if (!uiObj)
				{
					_ASSERTE(
					    !"Moniker does not point to valid image!\nClear build cache or fix imagemanifest and rebuild.");
					return E_FAIL;
				}
	
				CComVariant dataVar;
				if (SUCCEEDED(hr = uiObj->get_Data(&dataVar)) && dataVar.pdispVal)
				{
					CComQIPtr<IVsUIWin32Bitmap> pUIBmp(dataVar.pdispVal);
					if (pUIBmp)
					{
						return BitmapFromVsUIWin32Bitmap(pUIBmp, bmpOut, opaque ? bgClr : CLR_NONE, margin);
					}
				}
			}
		}
	}
	catch (...)
	{
		vLog("ERROR: ImageListManager::GetMonikerImage failed");
	}

	return hr;
}

int ImageListManager::BitmapFromVsUIWin32Bitmap(const IVsUIWin32Bitmap* uiBmp, CBitmap& bmpOut,
                                                COLORREF bgClr /*= CLR_NONE*/, RECT* margin /*= nullptr*/)
{
	HRESULT hr = E_FAIL;

	try
	{
		IVsUIWin32Bitmap* pUIBmp = (IVsUIWin32Bitmap*)(uiBmp);
	
		INT_PTR hBmp = 0;
		if (pUIBmp && SUCCEEDED(hr = pUIBmp->GetHBITMAP(&hBmp)))
		{
			BITMAP bmp;
			if (::GetObject((HBITMAP)hBmp, sizeof(BITMAP), &bmp))
			{
				if (bgClr != CLR_NONE)
				{
					VsUI::GdiplusImage iSrc;
					iSrc.Attach((HBITMAP)hBmp);
	
					VsUI::GdiplusImage iDst;
	
					int bw = bmp.bmWidth;
					int bh = bmp.bmHeight;
					if (margin)
					{
						bw += VsUI::DpiHelper::LogicalToDeviceUnitsX(margin->left + margin->right);
						bh += VsUI::DpiHelper::LogicalToDeviceUnitsX(margin->top + margin->bottom);
					}
	
					iDst.Create(bw, bh, PixelFormat32bppARGB);
	
					Gdiplus::Color bg;
					bg.SetFromCOLORREF(bgClr);
	
					CAutoPtr<Gdiplus::Graphics> graphics(iDst.GetGraphics());
					graphics->Clear(bg);
					graphics->SetSmoothingMode(Gdiplus::SmoothingModeNone);
					graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
					graphics->SetCompositingMode(Gdiplus::CompositingModeSourceOver);
					graphics->SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
					if (margin)
					{
						graphics->DrawImage(iSrc, VsUI::DpiHelper::LogicalToDeviceUnitsX(margin->left),
						                    VsUI::DpiHelper::LogicalToDeviceUnitsX(margin->top));
					}
					else
					{
						graphics->DrawImage(iSrc, 0, 0);
					}
					graphics.Free();
	
					if (bmpOut.Attach(iDst.Detach()))
					{
						return S_OK;
					}
				}
				else
				{
					// use copy because IVsUIWin32Bitmap owns handle
					HBITMAP copyBmp =
					    (HBITMAP)::CopyImage((HBITMAP)hBmp, IMAGE_BITMAP, bmp.bmWidth, bmp.bmHeight, LR_CREATEDIBSECTION);
	
					if (copyBmp && bmpOut.Attach(copyBmp))
					{
						return S_OK;
					}
				}
			}
		}
	}
	catch (...)
	{
		vLog("ERROR: ImageListManager::BitmapFromVsUIWin32Bitmap failed");
	}

	return hr;
}

// int ImageListManager::FillWithMonikerImages(CImageList& il, COLORREF bgClr, bool replace)
// {
// 	StopWatch sw_0;
//
// 	int count_added = 0;
// 	CComPtr<IVsImageService2> spImgSvc;
// 	if (SUCCEEDED(gPkgServiceProvider->QueryService(SID_SVsImageService, &spImgSvc)))
// 	{
// 		ImageAttributes attr = {0};
// 		attr.StructSize = sizeof(attr);
// 		attr.Format = DF_Win32;
// 		// IT_Bitmap for HBITMAP, IT_Icon for HICON, IT_ImageList for HIMAGELIST
// 		attr.ImageType = IT_Bitmap;
// 		attr.LogicalWidth = 16;
// 		attr.LogicalHeight = 16;
// 		attr.Dpi = VsUI::DpiHelper::GetDeviceDpiY();
// 		// Desired RGBA color, if you don't use this, don't set IAF_Background below
// 		attr.Background = bgClr;
// 		attr.Flags = (ImageAttributesFlags)(IAF_RequiredFlags | IAF_Background);
//
// 		CDC cdc;
// 		cdc.CreateCompatibleDC(nullptr);
//
// 		std::shared_ptr<CImageList> tmp(replace ? new CImageList() : nullptr);
// 		if (tmp)
// 		{
// 			int scaledW = VsUI::DpiHelper::LogicalToDeviceUnitsX(16);
// 			int scaledH = VsUI::DpiHelper::LogicalToDeviceUnitsY(16);
// 			tmp->Create(scaledW, scaledH, ILC_COLOR32, 0, 0);
//
// 			for (int i = 0; i < g_IconIdx_VaOffset; i++)
// 			{
// 				CopyImageToAlphaList(il, *tmp, i, bgClr);
// 			}
// 		}
//
// 		auto process_img = [&](CBitmap * pBmp)
// 		{
// 			if (tmp)
// 				tmp->Add(pBmp, CLR_NONE);
// 			else
// 				il.Add(pBmp, CLR_NONE);
//
// 			count_added++;
// 		};
//
// 		auto & vec = mState->mVsImgCache[std::make_tuple(attr.Dpi, bgClr)];
// 		if (!vec.empty())
// 		{
// 			StopWatch sw;
// 			for (const auto & pBmp : vec)
// 			{
// 				process_img(pBmp.get());
// 			}
// 			VADEBUGPRINT("#HDI VEC - " << sw.ElapsedMilliseconds());
// 		}
// 		else
// 		{
// 			StopWatch sw;
// 			bool success = false;
// 			int i = 0;
// 			for (const ImageMoniker& mon : VSMonikers)
// 			{
// 				CComPtr<IVsUIObject> uiObj;
// 				if (SUCCEEDED(spImgSvc->GetImage(mon, attr, &uiObj)))
// 				{
// 					CComVariant dataVar;
// 					if (SUCCEEDED(uiObj->get_Data(&dataVar)))
// 					{
// 						CComQIPtr<IVsUIWin32Bitmap> pUIBmp(dataVar.pdispVal);
// 						if (pUIBmp)
// 						{
// 							INT_PTR hBmp = 0;
// 							if (pUIBmp && SUCCEEDED(pUIBmp->GetHBITMAP(&hBmp)))
// 							{
// 								auto pBmp = std::make_shared<CBitmap>();
// 								pBmp->Attach((HBITMAP)hBmp);
//
// // 								VsUI::GdiplusImage img;
// // 								img.Attach((HBITMAP)hBmp);
// // 								CStringW path;
// // 								path.Format(L"e:\\imgs\\%d.png", i);
// // 								img.Save(path);
//
// 								vec.emplace_back(pBmp);
// 								process_img(pBmp.get());
// 								success = true;
// 							}
// 						}
// 					}
// 				}
// 				_ASSERTE(success);
// 				i++;
// 			}
// 			VADEBUGPRINT("#HDI VS - " << sw.ElapsedMilliseconds());
// 		}
//
// 		if (tmp && il.m_hImageList)
// 		{
// 			int ilCount = il.GetImageCount();
// 			int tmpCount = tmp->GetImageCount();
//
// 			for (int i = tmpCount; i < ilCount; i++)
// 			{
// 				CopyImageToAlphaList(il, *tmp, i, bgClr);
// 			}
//
// 			HIMAGELIST old = il.Detach();
// 			il.Attach(tmp->Detach());
// 			tmp->Attach(old);
// 		}
// 	}
//
// 	VADEBUGPRINT("#HDI total - " << sw_0.ElapsedMilliseconds());
// 	return count_added;
// }

//#endif // _WIN64

int ImageListManager::ScaleImageListEx(CImageList& srcList, CImageList& dstList, COLORREF bgClr)
{
	// handle case when source equals destination
	if (&srcList == &dstList)
		return ScaleImageList(srcList, bgClr);

	if (!VsUI::DpiHelper::IsScalingSupported())
		return 0;
	if (!VsUI::DpiHelper::IsScalingRequired())
		return 0;

	// pre-vs2008, we don't use argb imagelists (we use a mask), which makes scaling difficult
	if (!gShellAttr->IsDevenv8OrHigher() && !gShellAttr->IsCppBuilder())
		return 0;

	// DPI helper scope must be set before calling this method
	_ASSERTE(VsUI::DpiHelper::IsWithinDpiScope());

	int scaledW = VsUI::DpiHelper::LogicalToDeviceUnitsX(16);
	int scaledH = VsUI::DpiHelper::LogicalToDeviceUnitsY(16);

	int unscaledW, unscaledH;
	if (!ImageList_GetIconSize(srcList.m_hImageList, &unscaledW, &unscaledH))
		return 0;

	// case: 146151 don't scale if size is correct
	if (unscaledW == scaledW && unscaledH == scaledH)
		return 0;

	int imgCount = srcList.GetImageCount();
	if (imgCount == 0)
		return 0;

	Gdiplus::Color gdiBgClr;
	gdiBgClr.SetFromCOLORREF(bgClr);

	CDC cdc;
	cdc.CreateCompatibleDC(nullptr);

	for (int i = 0; i < imgCount; i++)
	{
		// Need to create bitmap with alpha channel -- can't use CBitmap::Create
		VsUI::GdiplusImage img;
		img.Create(unscaledW, unscaledH, PixelFormat32bppARGB);

		CBitmap bmp;
		bmp.Attach(img.Detach()); // this gets us a real 32bit HBITMAP

		{
			cdc.SelectObject(bmp);
			cdc.FillSolidRect(0, 0, unscaledW, unscaledH, bgClr); // avoid artifacts!!!
			srcList.Draw(&cdc, i, {0, 0}, ILD_NORMAL);
		}

		VsUI::GdiplusImage iSrc;
		iSrc.Attach(bmp);

		// if imagelist image wasn't 32bit, then fix opacity problems
		{
			IMAGEINFO info;
			ZeroMemory(&info, sizeof(info));
			srcList.GetImageInfo(i, &info);
			VsUI::GdiplusImage iOrig;
			iOrig.Attach(info.hbmImage);
			if (iOrig.GetBitmap()->GetPixelFormat() != PixelFormat32bppARGB)
			{
				auto transparentBgClr =
				    Gdiplus::Color::MakeARGB(0, GetRValue(bgClr), GetGValue(bgClr), GetBValue(bgClr));
				iSrc.ProcessBitmapBits(iSrc.GetBitmap(), [&](Gdiplus::ARGB* pPixelData) {
					if (*pPixelData == 0)
						; // already transparent black, don't make opaque
					else if (*pPixelData == transparentBgClr)
						*pPixelData = 0x00000000; // make transparent black
					else
						*pPixelData |= 0xff000000; // make opaque
				});
			}
		}

		VsUI::GdiplusImage iDst;
		iDst.Create(scaledW, scaledH, PixelFormat32bppARGB);

		CAutoPtr<Gdiplus::Graphics> graphics(iDst.GetGraphics());

		// clang-format off
		// case: 146151 in case of downscaling use bicubic
		bool useBicubic = 
		    scaledW < unscaledW || (scaledW % unscaledW) != 0 || 
			scaledH < unscaledH || (scaledH % unscaledH) != 0;
		// clang-format on

		auto interpMode =
		    useBicubic ? Gdiplus::InterpolationModeHighQualityBicubic : Gdiplus::InterpolationModeNearestNeighbor;
		graphics->SetSmoothingMode(Gdiplus::SmoothingModeNone);
		graphics->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		graphics->SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
		graphics->ScaleTransform((Gdiplus::REAL)scaledW / (Gdiplus::REAL)unscaledW, // for exact pixel match
		                         (Gdiplus::REAL)scaledH / (Gdiplus::REAL)unscaledH, // for exact pixel match
		                         Gdiplus::MatrixOrderAppend);
		graphics->SetInterpolationMode(interpMode);
		graphics->DrawImage(iSrc, 0, 0);
		graphics.Free();

//#ifdef _WIN64
		if (useBicubic)
		{
			// case: 146151 sharpen icons so they don't look so blurry compared to high density icons
			auto radius = (Gdiplus::REAL)scaledW / (Gdiplus::REAL)unscaledW;
			iDst.Sharpen({{radius, 100.f}, {.5f, 100.f}}, true);
		}
//#endif

		CBitmap bmpDst;
		bmpDst.Attach(iDst.Detach(gdiBgClr));

		if (!dstList.m_hImageList)
		{
			dstList.Create(scaledW, scaledH, ILC_COLOR32, 0, 0);
		}

		dstList.Add(&bmpDst, CLR_NONE);
	}

	return imgCount;
}

bool ImageListManager::ThemeImageList(CImageList& il, COLORREF bgClr)
{
	static bool once = true;
	static bool sHasNewInterfaceMethod = true;
	if (once)
	{
		once = false;
		FileVersionInfo fvi;
#ifdef AVR_STUDIO
		if (!fvi.QueryFile(L"AtmelStudio.exe", FALSE) || fvi.GetFileVerMSHi() < 7)
		{
			sHasNewInterfaceMethod = false;
		}
#else
		if (!fvi.QueryFile(L"DevEnv.exe", FALSE) || fvi.GetFileVerMSHi() < 11 ||
		    (fvi.GetFileVerMSHi() == 11 && fvi.GetFileVerMSLo() == 0 && fvi.GetFileVerLSHi() <= 50706))
		{
			// IVsUiShell5 didn't have CreateThemedImageList in the RC
			sHasNewInterfaceMethod = false;
		}
#endif
	}

	if (sHasNewInterfaceMethod && gPkgServiceProvider && Psettings && Psettings->mEnableIconTheme)
	{
		CComPtr<IVsUIShell> uishell;
		if (SUCCEEDED(gPkgServiceProvider->QueryService(SID_SVsUIShell, IID_IVsUIShell, (void**)&uishell)) && uishell)
		{
			CComQIPtr<IVsUIShell5> uishell5(uishell);
			if (uishell5)
			{
				HANDLE themedIl = INVALID_HANDLE_VALUE;
				if (SUCCEEDED(uishell5->CreateThemedImageList(il.m_hImageList, bgClr, &themedIl)))
				{
					if (themedIl != INVALID_HANDLE_VALUE)
					{
						// replace the image list with the VS built one
						il.DeleteImageList();
						il.m_hImageList = (HIMAGELIST)themedIl;
						return true;
					}
				}
			}
		}
	}

	return false;
}

bool ExtractImageFromAlphaList(CImageList& lstImages, int nImage, CDC* dc, COLORREF bgClr, CBitmap& destBitmap)
{
	// http://forums.codeguru.com/archive/index.php/t-257564.html
	// http://www.codeproject.com/Articles/4673/Extracting-Single-Images-from-a-CImageList-object

	// Now we need to get some information about the image
	IMAGEINFO lastImage;
	lstImages.GetImageInfo(nImage, &lastImage);

	// Heres where it gets fun
	// Create a Compatible Device Context using
	// the valid DC of your calling window
	CDC dcMem;
	if (!dcMem.CreateCompatibleDC(dc))
	{
		vLog("ERROR: ExtractImg createDC failed 0x%lx", GetLastError());
		return false;
	}

	// This rect simply stored the size of the image we need
	CRect rect(lastImage.rcImage);

	// Using the bitmap passed in, Create a bitmap
	// compatible with the window DC
	// We also know that the bitmap needs to be a certain size.
	if (!destBitmap.CreateCompatibleBitmap(dc, rect.Width(), rect.Height()))
	{
		vLog("ERROR: ExtractImg createBmp failed 0x%lx", GetLastError());
		return false;
	}

	// Select the new destination bitmap into the DC we created above
	CBitmap* pBmpOld = dcMem.SelectObject(&destBitmap);
	if (!pBmpOld)
	{
		vLog("ERROR: ExtractImg select 1 failed 0x%lx", GetLastError());
		return false;
	}

	bool res = true;
	// This call apparently "draws" the bitmap from the list,
	// onto the new destination bitmap
	if (!lstImages.DrawIndirect(&dcMem, nImage, CPoint(0, 0), CSize(rect.Width(), rect.Height()), CPoint(0, 0),
	                            ILD_NORMAL, SRCCOPY, bgClr, CLR_DEFAULT, ILS_NORMAL, 0, CLR_DEFAULT))
	{
		vLog("ERROR: ExtractImg Draw failed 0x%lx", GetLastError());
		res = false;
	}

	// cleanup by reselecting the old bitmap object into the DC
	if (!dcMem.SelectObject(pBmpOld))
	{
		vLog("ERROR: ExtractImg select 2 failed 0x%lx", GetLastError());
		res = false;
	}
	return res;
}

bool ExtractImagesFromAlphaList(CImageList& lstImages, int imgCnt, CDC* dc, COLORREF bgClr, CBitmap& destBitmap)
{
	// Now we need to get some information about the images
	IMAGEINFO firstImage;
	lstImages.GetImageInfo(0, &firstImage);

	// Heres where it gets fun
	// Create a Compatible Device Context using
	// the valid DC of your calling window
	CDC dcMem;
	if (!dcMem.CreateCompatibleDC(dc))
	{
		vLog("ERROR: ExtractImgs createDC failed 0x%lx", GetLastError());
		return false;
	}

	// This rect simply stored the size of the image we need
	CRect rect(firstImage.rcImage);

	// Using the bitmap passed in, Create a bitmap
	// compatible with the window DC
	// We also know that the bitmap needs to be a certain size.
	if (!destBitmap.CreateCompatibleBitmap(dc, rect.Width() * imgCnt, rect.Height()))
	{
		vLog("ERROR: ExtractImgs createBmp failed 0x%lx", GetLastError());
		return false;
	}

	// Select the new destination bitmap into the DC we created above
	CBitmap* pBmpOld = dcMem.SelectObject(&destBitmap);
	if (!pBmpOld)
	{
		vLog("ERROR: ExtractImgs select 1 failed 0x%lx", GetLastError());
		return false;
	}

	bool res = true;
	for (int idx = 0; idx < imgCnt; ++idx)
	{
		// This call apparently "draws" the bitmap from the list,
		// onto the new destination bitmap
		if (!lstImages.DrawIndirect(&dcMem, idx, CPoint(idx * rect.Width(), 0), CSize(rect.Width(), rect.Height()),
		                            CPoint(0, 0), ILD_NORMAL, SRCCOPY, bgClr, CLR_DEFAULT, ILS_NORMAL, 0, CLR_DEFAULT))
		{
			vLog("ERROR: ExtractImgs Draw failed 0x%lx", GetLastError());
			res = false;
		}
	}

	// cleanup by reselecting the old bitmap object into the DC
	if (!dcMem.SelectObject(pBmpOld))
	{
		vLog("ERROR: ExtractImgs select 2 failed 0x%lx", GetLastError());
		res = false;
	}
	return res;
}

void ChangeBitmapColour(HBITMAP bitmap, COLORREF srcclr, COLORREF destclr)
{
	if (!bitmap)
		return;

	CDC dc;
	if (!dc.CreateCompatibleDC(NULL))
		return;

	BITMAP bm;
	memset(&bm, 0, sizeof(bm));
	if (!::GetObject(bitmap, sizeof(bm), &bm))
		return;

	if (!bm.bmWidth || !bm.bmHeight)
		return;

	HGDIOBJ hObj = dc.SelectObject(bitmap);
	COLORREF colorToChange = (srcclr != CLR_INVALID) ? srcclr : dc.GetPixel(0, 0);
	CFastBitmapReader fastReader(dc.GetSafeHdc(), bitmap);
	for (int y = 0; y < bm.bmHeight; y++)
	{
		for (int x = 0; x < bm.bmWidth; x++)
		{
			if (fastReader.FastGetPixel(x, y) == colorToChange)
			{
				dc.SetPixelV(x, y, destclr);
			}
		}
	}

	dc.SelectObject(hObj);
}
