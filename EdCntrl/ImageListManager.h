#pragma once

#include <memory>
#include "DpiCookbook/VsUIDpiHelper.h"
#include "DebugStream.h"

//#ifdef _WIN64
#ifndef __ImageParameters140_h__
struct __MIDL___MIDL_itf_ImageParameters140_0000_0000_0002;
typedef __MIDL___MIDL_itf_ImageParameters140_0000_0000_0002 ImageMoniker;
#endif
//#endif

class ImageListManager
{
  public:
	enum BackgroundType
	{
		bgNone = -1,
		bgMenu,
		bgFirst = bgMenu,
		bgList,
		bgCombo,
		bgComboDropdown,
		bgTree,
		bgOsWindow,
		bgTooltip,
		bgCount
	};

	struct ImgListKey
	{
		BackgroundType bgType;
		UINT dpi;
		COLORREF bgClr;

		ImgListKey(BackgroundType type, UINT dpi, COLORREF bgClr)
		{
			this->bgType = type;
			this->dpi = dpi;
			this->bgClr = bgClr;
		}

		bool operator<(ImgListKey const& right) const
		{
			if (bgType != right.bgType)
				return bgType < right.bgType;

			if (bgClr != right.bgClr)
				return bgClr < right.bgClr;

			return dpi < right.dpi;
		}
	};

  private:
	CImageList* InitList(const ImgListKey& key, GUID themeId, bool forceUpdate);
	CImageList* GetImgList(ImageListManager::BackgroundType bt, UINT dpi, COLORREF bgClr);
	CImageList* GetImgList(const ImgListKey& key);

  public:
	ImageListManager();
	~ImageListManager();

	void Update(const GUID& themeId);
	void Update();
	void ThemeUpdated();

	COLORREF GetBackgroundColor(ImageListManager::BackgroundType bt);
	bool ThemeImageList(CImageList& il, COLORREF bgClr);
	int ScaleImageList(CImageList& il, COLORREF bgClr);

//#ifdef _WIN64
	using MonikerImageFunc =
	    std::function<void(const ImageMoniker& moniker, CBitmap& bmp, bool is_iconIdx, int local_index)>;
	static int ForEachMonikerImage(const MonikerImageFunc& func, COLORREF bgClr, bool ideIcons = true,
	                               bool vaIcons = false, bool opaque = false);

	static int GetMonikerImageFromResourceId(CBitmap& bmpOut, int rscId, COLORREF bgClr, UINT dpi,
	                                         bool grayScale = false, bool highContrast = false, bool opaque = false,
	                                         RECT* margin = nullptr);
	static int GetMonikerImage(CBitmap& bmpOut, const ImageMoniker& moniker, COLORREF bgClr, UINT dpi,
	                           bool grayScale = false, bool highContrast = false, bool opaque = false,
	                           RECT* margin = nullptr);

	static int BitmapFromVsUIWin32Bitmap(const IVsUIWin32Bitmap* uiBmp, CBitmap& bmpOut, COLORREF bgClr = CLR_NONE,
	                                     RECT* margin = nullptr);

	static const ImageMoniker* GetMoniker(int index, bool ideIcons = true, bool vaIcons = false);

	static bool ImageMonikerDraw(int img, HDC hdcDst, COLORREF bgColor, int x, int y)
	{
		return ImageMonikerDraw(img, hdcDst, bgColor, CPoint(x, y));
	}
	static bool ImageMonikerDraw(int img, HDC hdcDst, COLORREF bgColor, const POINT& pt);

//#endif
	// - if srcList equals dstList, content is scaled in place
	// - function initializes dstList if it is not already valid
	// - dstList is assumed to be compatible with images being inserted,
	//   pass uninitialized CImageList if you don't know the destination dimensions
	// - the call must be done within CDpiHandlerScope, otherwise expect assertion
	int ScaleImageListEx(CImageList& srcList, CImageList& dstList, COLORREF bgClr);

	// returns imagelist specific for DPI and background type specified
	CImageList* GetImgList(ImageListManager::BackgroundType bt, UINT dpi = 0);

	// returns imagelist specific for DPI and background type specified by SetWindowBackgroundType
	CImageList* GetImgList(HWND hWnd, UINT dpi = 0);

	template <class WINDOW, class... ARGTYPES>
	CImageList* SetImgList(WINDOW& wnd, ImageListManager::BackgroundType bt, ARGTYPES&&... _Args)
	{
		VADEBUGPRINT("#DPI SetImgList class: " << typeid(WINDOW).name());
		return SetImgListForDPI(wnd, bt, _Args...);
	}

	UINT GetImgListDPI(const CImageList& il);

	template <class WINDOW>
	bool TryUpdateImageListsForDPI(WINDOW& wnd, UINT dpiOverride = 0)
	{
		return TryUpdateImageListsForDPIDynamic(wnd, dpiOverride);
	}

	bool UpdateImageListsForDPI(CComboBoxEx& wnd, UINT dpiOverride = 0);
	bool UpdateImageListsForDPI(CHeaderCtrl& wnd, UINT dpiOverride = 0);
	bool UpdateImageListsForDPI(CTabCtrl& wnd, UINT dpiOverride = 0);
	bool UpdateImageListsForDPI(CTreeCtrl& tree, UINT dpiOverride = 0);
	bool UpdateImageListsForDPI(CListCtrl& list, UINT dpiOverride = 0);

	CImageList* SetImgListForDPI(CComboBoxEx& wnd, ImageListManager::BackgroundType bt);
	CImageList* SetImgListForDPI(CHeaderCtrl& wnd, ImageListManager::BackgroundType bt);
	CImageList* SetImgListForDPI(CTabCtrl& wnd, ImageListManager::BackgroundType bt);
	CImageList* SetImgListForDPI(CTreeCtrl& tree, ImageListManager::BackgroundType bt, int tvsil);
	CImageList* SetImgListForDPI(CListCtrl& list, ImageListManager::BackgroundType bt, int lvsil);

	static void SetWindowBackgroundType(HWND hWnd, ImageListManager::BackgroundType bt);
	static bool GetWindowBackgroundType(ImageListManager::BackgroundType& bt, HWND hWnd);

	void GetRawBitmaps(CBitmap** vsIcons, CBitmap** vaIcons, COLORREF& vsTrans, COLORREF& vaTrans);

  private:
	bool TryUpdateImageListsForDPIDynamic(CWnd& wnd, UINT dpiOverride = 0);

	struct ImgListState;
	std::unique_ptr<ImgListState> mState;
	GUID mThemeId = GUID_NULL; // case: 146280 - storing theme ID gives us more control
};

bool ExtractImageFromAlphaList(CImageList& lstImages, int nImage, CDC* dc, COLORREF bgClr, CBitmap& destBitmap);
bool ExtractImagesFromAlphaList(CImageList& lstImages, int imgCnt, CDC* dc, COLORREF bgClr, CBitmap& destBitmap);
void ChangeBitmapColour(HBITMAP bitmap, COLORREF srcclr, COLORREF destclr);
void MakeBitmapTransparent(CBitmap& bitmap, COLORREF clrTransparent);

extern ImageListManager* gImgListMgr;
extern int g_IconIdx_VaOffset;
