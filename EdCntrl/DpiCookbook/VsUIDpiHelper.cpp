/*
	Copyright(c) Microsoft Corporation.
	All rights reserved.

	Microsoft Limited Public License (MS - LPL)
	This license governs use of the accompanying software. If you use the software, you accept this license. If you do not accept the license, do not use the software.
	1. Definitions
	The terms “reproduce,” “reproduction,” “derivative works,” and “distribution” have the same meaning here as under U.S.copyright law.
	A “contribution” is the original software, or any additions or changes to the software.
	A “contributor” is any person that distributes its contribution under this license.
	“Licensed patents” are a contributor’s patent claims that read directly on its contribution.
	2. Grant of Rights
		(A) Copyright Grant - Subject to the terms of this license, including the license conditions and limitations in section 3, each contributor grants you a non-exclusive, worldwide, royalty-free copyright license to reproduce its contribution, prepare derivative works of its contribution, and distribute its contribution or any derivative works that you create.
		(B) Patent Grant - Subject to the terms of this license, including the license conditions and limitations in section 3, each contributor grants you a non-exclusive, worldwide, royalty-free license under its licensed patents to make, have made, use, sell, offer for sale, import, and/or otherwise dispose of its contribution in the software or derivative works of the contribution in the software.
	3. Conditions and Limitations
		(A) No Trademark License - This license does not grant you rights to use any contributors’ name, logo, or trademarks.
		(B) If you bring a patent claim against any contributor over patents that you claim are infringed by the software, your patent license from such contributor to the software ends automatically.
		(C) If you distribute any portion of the software, you must retain all copyright, patent, trademark, and attribution notices that are present in the software.
		(D) If you distribute any portion of the software in source code form, you may do so only under this license by including a complete copy of this license with your distribution.If you distribute any portion of the software in compiled or object code form, you may only do so under a license that complies with this license.
		(E) The software is licensed “as-is.” You bear the risk of using it. The contributors give no express warranties, guarantees or conditions.You may have additional consumer rights under your local laws which this license cannot change.To the extent permitted under your local laws, the contributors exclude the implied warranties of merchantability, fitness for a particular purpose and non-infringement.
		(F) Platform Limitation - The licenses granted in sections 2(A) & 2(B) extend only to the software or derivative works that you create that run on a Microsoft Windows operating system product.
*/

#include "StdAfx.h"
#include "VsUIDpiHelper.h"
#include "vsassert.h"
#include "atlgdi.h"
#include <vector>
#include <atlstr.h>
#include <atlpath.h>
#include "..\DevShellAttributes.h"
#include "..\MenuXP\Tools.h"
#include "..\LOG.H"
#include "CWndDpiAware.h"
#include "EDCNT.H"
#include <hash_map>
#include "PROJECT.H"
#include "..\common\ThreadStatic.h"

#ifdef _DEBUG
#undef VSASSERT
#define VSASSERT(x, y)	_ASSERTE(x)
#endif // _DEBUG

#define IfFailRetNull(var)          { if (!var) return NULL; }
#define IfNullRetNull(var)          { if (var == NULL) return NULL; }
#define IfNullRet(var)              { if (var == NULL) return; }
#define IfNullRetX(var,retval)      { if (var == NULL) return retval; }
#define IfNullAssertRet(var, msg)   { if (!var) { VSFAIL(msg); return; } }
#define IfNullAssertRetNull(var, msg) { if (!var) { VSFAIL(msg); return nullptr; } }

#define REGKEY_GENERAL L"General"
#define REGKEY_IMAGESCALING L"ImageScaling%d"

using namespace Gdiplus;
using namespace std;

static bool IsImageScalingSupported()
{
	static bool once = true;
	static bool sImgScalingSupported = false;
	if (once)
	{
		once = false;
		sImgScalingSupported = ::GetWinVersion() >= wvWin7;
	}
	return sImgScalingSupported;
}

static HWND DispatchingWindow()
{
	_AFX_THREAD_STATE* pThreadState = _afxThreadState.GetData();

	if (pThreadState)
		return pThreadState->m_lastSentMsg.hwnd;

	return nullptr;
}

namespace VsUI
{

CDpiHelper::CDpiHelper(int iDeviceDpiX, int iDeviceDpiY, int iLogicalDpiX, int iLogicalDpiY) :
    m_DeviceDpiX(iDeviceDpiX), m_DeviceDpiY(iDeviceDpiY), m_LogicalDpiX(iLogicalDpiX), m_LogicalDpiY(iLogicalDpiY), m_PreferredScalingMode(ImageScalingMode::Default)
{
}

// Get device DPI.
int CDpiHelper::GetDeviceDpiX() const
{ 
    return m_DeviceDpiX; 
}

int CDpiHelper::GetDeviceDpiY() const 
{ 
    return m_DeviceDpiY; 
}

// Get logical DPI.
int CDpiHelper::GetLogicalDpiX() const 
{ 
    return m_LogicalDpiX; 
}

int CDpiHelper::GetLogicalDpiY() const 
{ 
    return m_LogicalDpiY; 
}

bool CDpiHelper::IsScalingRequired() const
{
    return (m_DeviceDpiX != m_LogicalDpiX || m_DeviceDpiY != m_LogicalDpiY); 
}

// Return horizontal and vertical scaling factors
double CDpiHelper::DeviceToLogicalUnitsScalingFactorX() const
{
    return (double)m_LogicalDpiX / m_DeviceDpiX;
}

double CDpiHelper::DeviceToLogicalUnitsScalingFactorY() const
{
    return (double)m_LogicalDpiY / m_DeviceDpiY;
}

double CDpiHelper::LogicalToDeviceUnitsScalingFactorX() const
{
    return (double)m_DeviceDpiX / m_LogicalDpiX;
}

double CDpiHelper::LogicalToDeviceUnitsScalingFactorY() const
{
    return (double)m_DeviceDpiY / m_LogicalDpiY;
}

// Converts between logical and device units.
int CDpiHelper::LogicalToDeviceUnitsX(int x) const 
{ 
    return MulDiv(x, m_DeviceDpiX, m_LogicalDpiX); 
}

int CDpiHelper::ImgLogicalToDeviceUnitsX(int x) const 
{ 
	if (::IsImageScalingSupported())
		return MulDiv(x, m_DeviceDpiX, m_LogicalDpiX); 
	return x;
}

int CDpiHelper::LogicalToDeviceUnitsY(int y) const
{ 
    return MulDiv(y, m_DeviceDpiY, m_LogicalDpiY); 
}

int CDpiHelper::ImgLogicalToDeviceUnitsY(int y) const
{ 
	if (::IsImageScalingSupported())
	    return MulDiv(y, m_DeviceDpiY, m_LogicalDpiY); 
	return y;
}

// Converts between device and logical units.
int CDpiHelper::DeviceToLogicalUnitsX(int x) const 
{ 
    return MulDiv(x, m_LogicalDpiX, m_DeviceDpiX); 
}

int CDpiHelper::DeviceToLogicalUnitsY(int y) const 
{ 
    return MulDiv(y, m_LogicalDpiY, m_DeviceDpiY); 
}

// Converts from logical units to device units.
void CDpiHelper::LogicalToDeviceUnits(_Inout_ RECT * pRect, const POINT* pPivot /*= nullptr*/) const
{
    if (pRect != nullptr)
    {
		if (pPivot == nullptr || (pPivot->x == 0 && pPivot->y == 0))
		{
			pRect->left = LogicalToDeviceUnitsX(pRect->left);
			pRect->right = LogicalToDeviceUnitsX(pRect->right);
			pRect->top = LogicalToDeviceUnitsY(pRect->top);
			pRect->bottom = LogicalToDeviceUnitsY(pRect->bottom);
		}
		else
		{
			pRect->left = pPivot->x + LogicalToDeviceUnitsX(pRect->left - pPivot->x);
			pRect->right = pPivot->x + LogicalToDeviceUnitsX(pRect->right - pPivot->x);
			pRect->top = pPivot->y + LogicalToDeviceUnitsY(pRect->top - pPivot->y);
			pRect->bottom = pPivot->y + LogicalToDeviceUnitsY(pRect->bottom - pPivot->y);
		}
    }
}

void CDpiHelper::LogicalToDeviceUnits(_Inout_ POINT * pPoint) const
{
    if (pPoint != nullptr)
    {
        pPoint->x = LogicalToDeviceUnitsX(pPoint->x);
        pPoint->y = LogicalToDeviceUnitsY(pPoint->y);
    }
}

// Converts from device units to logical units.
void CDpiHelper::DeviceToLogicalUnits(_Inout_ RECT * pRect, const POINT* pPivot /*= nullptr*/) const
{
    if (pRect != nullptr)
    {
		if (pPivot == nullptr || (pPivot->x == 0 && pPivot->y == 0))
		{
			pRect->left = DeviceToLogicalUnitsX(pRect->left);
			pRect->right = DeviceToLogicalUnitsX(pRect->right);
			pRect->top = DeviceToLogicalUnitsY(pRect->top);
			pRect->bottom = DeviceToLogicalUnitsY(pRect->bottom);
		}
		else
		{
			pRect->left = pPivot->x + DeviceToLogicalUnitsX(pRect->left - pPivot->x);
			pRect->right = pPivot->x + DeviceToLogicalUnitsX(pRect->right - pPivot->x);
			pRect->top = pPivot->y + DeviceToLogicalUnitsY(pRect->top - pPivot->y);
			pRect->bottom = pPivot->y + DeviceToLogicalUnitsY(pRect->bottom - pPivot->y);
		}
	}
}

void CDpiHelper::DeviceToLogicalUnits(_Inout_ POINT * pPoint) const
{
    if (pPoint != nullptr)
    {
        pPoint->x = DeviceToLogicalUnitsX(pPoint->x);
        pPoint->y = DeviceToLogicalUnitsY(pPoint->y);
    }
}

// Convert a point size (1/72 of an inch) to raw pixels.
int CDpiHelper::PointsToDeviceUnits(int pt) const
{ 
    return MulDiv(pt, m_DeviceDpiY, 72); 
}

int CDpiHelper::PointsToDeviceUnits(double pt) const
{
	double dc = pt * (m_DeviceDpiY / 72.0);
	return (dc >= 0) ? (int)(dc + 0.5) : (int)(dc - 0.5);
}

// Determine the screen dimensions in logical units.
int CDpiHelper::LogicalScreenWidth() const
{ 
    return DeviceToLogicalUnitsX(GetSystemMetrics(SM_CXSCREEN)); 
}

int CDpiHelper::LogicalScreenHeight() const 
{ 
    return DeviceToLogicalUnitsY(GetSystemMetrics(SM_CYSCREEN)); 
}

// Determine if screen resolution meets minimum requirements in logical pixels.
bool CDpiHelper::IsResolutionAtLeast(int cxMin, int cyMin) const
{ 
    return (LogicalScreenWidth() >= cxMin) && (LogicalScreenHeight() >= cyMin); 
}

// Return the monitor information in logical units
BOOL CDpiHelper::GetLogicalMonitorInfo(_In_ HMONITOR hMonitor, _Out_ LPMONITORINFO lpmi) const
{
    if (GetMonitorInfo(hMonitor, lpmi))
    {
        DeviceToLogicalUnits(&lpmi->rcMonitor);
        DeviceToLogicalUnits(&lpmi->rcWork);
        return TRUE;
    }
    
    return FALSE;
}

// Returns the shell preferred scaling mode, depening on the DPI zoom level
ImageScalingMode CDpiHelper::GetDefaultScalingMode(int dpiScalePercent) const
{
    // We'll use NearestNeighbor for 100, 200, 400, etc scaling mode, where we get crisp/pixelated results without image distortions
    // We'll use Bicubic scaling for the rest except when the scale is actually for reducing the image (which we shouldn't have anyway), when Linear produces better results because it uses less neighboring pixels.
    // The algorithm matches GetDefaultBitmapScalingMode from the MPF's DpiHelper class
    // http://blogs.msdn.com/b/visualstudio/archive/2014/03/19/improving-high-dpi-support-for-visual-studio-2013.aspx
	if ((dpiScalePercent % 100) == 0)
	{
        return ImageScalingMode::NearestNeighbor;
    }
    else if (dpiScalePercent < 100)
    {
        return ImageScalingMode::HighQualityBilinear;
    }
    else
    {
        return ImageScalingMode::HighQualityBicubic;
    }
}

// Returns the user preference for scaling mode by reading it from registry 
// or returns default scaling mode if the user doesn't want to override
ImageScalingMode CDpiHelper::GetUserScalingMode(int dpiScalePercent, ImageScalingMode defaultScalingMode) const
{
    ImageScalingMode scalingMode = defaultScalingMode;

	CString keyName;
    CString__FormatA(keyName, "%s\\General", (LPCTSTR)gShellAttr->GetBaseShellKeyName());

    CRegKey hKeyGeneral = NULL;
    if (ERROR_SUCCESS == hKeyGeneral.Open(HKEY_CURRENT_USER, keyName, KEY_READ))
    {
        TCHAR szValueName[30];
        _stprintf_s(szValueName, "ImageScaling%d", dpiScalePercent);

//        DWORD dwType = 0;
        DWORD dwData = 0;
//        DWORD cbDataLength = sizeof(dwData);
        if (ERROR_SUCCESS == hKeyGeneral.QueryDWORDValue(szValueName, dwData))
        {
            if (dwData == (DWORD)ImageScalingMode::BorderOnly || 
                dwData == (DWORD)ImageScalingMode::NearestNeighbor || 
                dwData == (DWORD)ImageScalingMode::Bilinear || 
                dwData == (DWORD)ImageScalingMode::Bicubic ||
                dwData == (DWORD)ImageScalingMode::HighQualityBilinear || 
                dwData == (DWORD)ImageScalingMode::HighQualityBicubic)
            {
                scalingMode = (ImageScalingMode)dwData;
            }
            else 
            {
                VSASSERT(dwData == (DWORD)ImageScalingMode::Default, "Invalid override scaling mode value");
            }
        }
    }

    return scalingMode;
}

// Gets the interpolation mode from the specified scaling mode
InterpolationMode CDpiHelper::GetInterpolationMode(_In_ ImageScalingMode scalingMode)
{
    switch (scalingMode)
    {
    case ImageScalingMode::Bilinear:
        {
            // Same as InterpolationModeLowQuality and InterpolationModeDefault
            return InterpolationModeBilinear;
        }
    case ImageScalingMode::Bicubic:
        {
            return InterpolationModeBicubic;
        }
    case ImageScalingMode::HighQualityBilinear:
        {
            return InterpolationModeHighQualityBilinear;
        }
    case ImageScalingMode::HighQualityBicubic: 
        {
            // Same as InterpolationModeHighQuality
            return InterpolationModeHighQualityBicubic;
        }
    case ImageScalingMode::BorderOnly: __fallthrough;
    case ImageScalingMode::NearestNeighbor: 
        {
            return InterpolationModeNearestNeighbor;
        }
    default:
        {
            VSFAIL("Unknown scaling mode, please add an explicit case. Falling back to use default interpolation.");
            __fallthrough;
        }
    case ImageScalingMode::Default:
        {
            return GetInterpolationMode(GetPreferredScalingMode()); 
        }
    }
}

// Gets the actual scaling mode to be used from the suggested scaling mode
ImageScalingMode CDpiHelper::GetActualScalingMode(_In_ ImageScalingMode scalingMode)
{
    // If a scaling mode other than default is specified, use that
    if (scalingMode != ImageScalingMode::Default)
    {
        return scalingMode;
    }

    // Otherwise return the shell preferred scaling mode for the current DPI zoom level or a possible user override
    return GetPreferredScalingMode();
}

// Returns the preferred scaling mode for current DPI zoom level (either shell preferred mode, or a user-override)
ImageScalingMode CDpiHelper::GetPreferredScalingMode()
{
    // If we haven't initialized yet the scaling mode
    if (m_PreferredScalingMode == ImageScalingMode::Default)
    {
        // Get the current zoom level
        int dpiScalePercent = (int)(LogicalToDeviceUnitsScalingFactorX() * 100);
        // Get the shell preferred scaling mode depending on the zoom level
        ImageScalingMode defaultScalingMode = GetDefaultScalingMode(dpiScalePercent);
        // Allow the user to override
        m_PreferredScalingMode = GetUserScalingMode(dpiScalePercent, defaultScalingMode);
    }

    return m_PreferredScalingMode;
}


// Convert GdiplusImage from logical to device units
void CDpiHelper::LogicalToDeviceUnits(_Inout_ std::unique_ptr<VsUI::GdiplusImage> * pImage, ImageScalingMode scalingMode, Color clrBackground)
{
    IfNullAssertRet(pImage, "No image given to convert");

    // If no scaling is required, the image can be used in current size
    if (!IsScalingRequired())
        return;
	if (!::IsImageScalingSupported())
		return;

    // Create the new image for the device, cloning the current one if necessary
    unique_ptr<VsUI::GdiplusImage> pDeviceImage = CreateDeviceFromLogicalImage(&**pImage, scalingMode, clrBackground);
    // If we failed to create the new image, return
    IfNullAssertRet(pDeviceImage.get(), "Failed to create scaled image");
    
    // Finally, replace the original image with the device image. Our pointer will take ownership and will release the original GDI+ Bitmap
    *pImage = std::move(pDeviceImage);
}
 
// Creates new GdiplusImage from logical to device units
unique_ptr<VsUI::GdiplusImage> CDpiHelper::CreateDeviceFromLogicalImage(_In_ VsUI::GdiplusImage* pImage, ImageScalingMode scalingMode, Color clrBackground)
{
    IfNullAssertRetNull(pImage, "No image given to convert");

    // Get the original/logical bitmap
    Bitmap* pBitmap = pImage->GetBitmap();
    IfNullAssertRetNull(pBitmap, "No image given to convert");
    
    // Create a memory image scaled for size
    int deviceWidth = LogicalToDeviceUnitsX((int)pBitmap->GetWidth());
    int deviceHeight = LogicalToDeviceUnitsY((int)pBitmap->GetHeight());
    
    unique_ptr<VsUI::GdiplusImage> pDeviceImage(new VsUI::GdiplusImage());
    pDeviceImage->Create( deviceWidth, deviceHeight, pBitmap->GetPixelFormat() );
       
    if (!pDeviceImage->IsLoaded())
    {
        VSFAIL("Failed to create scaled image, out of memory?");
        return nullptr;
    }
    
    // Get a Graphics object for the device image on which we can paint 
    unique_ptr<Graphics> pGraphics(pDeviceImage->GetGraphics());
    if (pGraphics.get() == nullptr)
    {
        VSFAIL("Failed to obtain image Graphics");
        return nullptr;
    }
    
    // Set the interpolation mode. 
    InterpolationMode interpolationMode = GetInterpolationMode(scalingMode);
    pGraphics->SetInterpolationMode(interpolationMode);

	// GDI+ draws pixels differently than GDI, so we have to use
	// PixelOffsetModeHalf to get bitmap correctly drawn.
	pGraphics->SetPixelOffsetMode(PixelOffsetModeHalf);

	// Set the scaling factor to the global transform
	// It is better than drawing bitmap from source to destination rectangles, 
	// as method does calculate own transformation which is based on rectangles,
	// but there is some bug in GDI+ so we use the correct transform directly. 
	pGraphics->ScaleTransform(
		(REAL)deviceWidth/(REAL)pBitmap->GetWidth(),
		(REAL)deviceHeight/(REAL)pBitmap->GetHeight(),
		MatrixOrderAppend);

    // Clear the background (used when scaling mode is not nearest neighbor)
    pGraphics->Clear(clrBackground);

	// Tiling FlipXY causes that pixels used to interpolate pixels 
	// on boundaries are taken from mirrored bitmap rather than from background. 
	ImageAttributes ia;
	ia.SetWrapMode(WrapModeTileFlipXY, clrBackground);

	// Same as source, scaling is provided by transform
	Rect destRect(0, 0, (int)pBitmap->GetWidth(), (int)pBitmap->GetHeight()); 

	// Draw the scaled bitmap in the device image
	// Note that: Dest / Src == 1
	pGraphics->DrawImage(pBitmap, destRect, 
		0, 0, (int)pBitmap->GetWidth(), (int)pBitmap->GetHeight(), UnitPixel, &ia);

    // Return the new image
    return pDeviceImage;
}
void CDpiHelper::LogicalToDeviceUnits(_Inout_ HBITMAP * pImage, ImageScalingMode scalingMode, Color clrBackground)
{
    IfNullAssertRet(pImage, "No image given to convert");

    // If no scaling is required, the image can be used in current size
    if (!IsScalingRequired())
        return;
	if (!::IsImageScalingSupported())
		return;

    try
    {
	    // Create a new HBITMAP for the device units
	    HBITMAP hDeviceImage = CreateDeviceFromLogicalImage(*pImage, scalingMode, clrBackground);
	    // If the device image could not be created, return and keep using the original
	    IfNullAssertRet(hDeviceImage, "Failed to create scaled image");
	
	    // Delete the original image and return the converted image
	    DeleteObject(*pImage);
	    *pImage = hDeviceImage;
    }
    catch (...)
    {
		vLog("ERROR: exception caught in CDpiHelper::LogicalToDeviceUnits");
    }
}

HBITMAP CDpiHelper::CreateDeviceFromLogicalImage(HBITMAP _In_ hImage, ImageScalingMode scalingMode, Color clrBackground)
{
    IfNullAssertRetNull(hImage, "No image given to convert");

    // Instead of doing HBITMAP resizing with StretchBlt from one memory DC into other memory DC and HALFTONE StretchBltMode
    // which uses nearest neighbor resize algorithm (fast but results in pixelation), we'll use a GdiPlus image to do the resize, 
    // which allows specifying the interpolation mode for the resize resulting in smoother result.
    std::unique_ptr<VsUI::GdiplusImage> gdiplusImage = std::make_unique<VsUI::GdiplusImage>();

    // Attaching the bitmap uses Bitmap.FromHBITMAP which does not take ownership of the HBITMAP passed as argument.
    // DeleteObject still needs to be used on the hImage but that should happen after the Bitmap object is deleted or goes out of scope.
    // The caller will have to DeleteObject both the HBITMAP they passed in this function and the new HBITMAP we'll be returning when we detach the GDI+ Bitmap
    gdiplusImage->Attach(hImage);

#ifdef DEBUG
    static bool fDebugDPIHelperScaling = false;
    WCHAR rgTempFolder[MAX_PATH];
    static int imgIndex = 1;
    CStringW strFileName;
    CPathT<CStringW> pathTempFile;

    if (fDebugDPIHelperScaling)
    {
        if (!GetTempPathW(_countof(rgTempFolder), rgTempFolder))
            *rgTempFolder = L'\0';
    
        CString__FormatW(strFileName, L"DPIHelper_%05d_Before.png", imgIndex);

        pathTempFile.Combine(rgTempFolder, strFileName);
        gdiplusImage->Save(pathTempFile);
    }
#endif

    Bitmap* pBitmap = gdiplusImage->GetBitmap();
    PixelFormat format = pBitmap->GetPixelFormat();
    const Color *pclrActualBackground = &clrBackground; 
//    InterpolationMode interpolationMode = GetInterpolationMode(scalingMode);
    ImageScalingMode actualScalingMode = GetActualScalingMode(scalingMode);

    if (actualScalingMode != ImageScalingMode::NearestNeighbor)
    {
        // Modify the image. If the image is 24bpp or lower, convert to 32bpp so we can use alpha values
        if (format != PixelFormat32bppARGB)
        {
            pBitmap->ConvertFormat(PixelFormat32bppARGB, DitherTypeNone, PaletteTypeCustom, nullptr/*ColorPalette*/, 0 /*alphaThresholdPercent - all opaque*/);
        }

        // Now that we have 32bpp image, let's play with the pixels
        // Detect magenta or near-green in the image and use that as background
        VsUI::GdiplusImage::ProcessBitmapBits(pBitmap, [&](ARGB * pPixelData) 
        {
            if (clrBackground.GetValue() != TransparentColor.GetValue())
            {
                if (*pPixelData == clrBackground.GetValue())
                {
                    *pPixelData = TransparentHaloColor.GetValue();
                    pclrActualBackground = &clrBackground;
                }
            }
            else
            {
                if (*pPixelData == MagentaColor.GetValue())
                {
                    *pPixelData = TransparentHaloColor.GetValue();
                    pclrActualBackground = &MagentaColor;
                }
                else if (*pPixelData == NearGreenColor.GetValue())
                {
                    *pPixelData = TransparentHaloColor.GetValue();
                    pclrActualBackground = &MagentaColor;
                }
            }
        });
    }

    // Convert the GdiPlus image if necessary
    LogicalToDeviceUnits(&gdiplusImage, scalingMode, TransparentHaloColor);

    // Get again the bitmap, after the resize
    pBitmap = gdiplusImage->GetBitmap();

    if (actualScalingMode != ImageScalingMode::NearestNeighbor)
    {
        // Now that the bitmap is scaled up, convert back the pixels. 
        // Anything that is not fully opaque, make it clrActualBackground
        VsUI::GdiplusImage::ProcessBitmapBits(pBitmap, [&](ARGB * pPixelData) 
        {
            if ((*pPixelData & ALPHA_MASK) != 0xFF000000)
            {
                *pPixelData = pclrActualBackground->GetValue();
            }
        });

        // Convert back to original format
        if (format != PixelFormat32bppARGB)
        {
            pBitmap->ConvertFormat(format, DitherTypeNone, PaletteTypeCustom, nullptr/*ColorPalette*/, 0 /*alphaThresholdPercent - all opaque*/);
        }
    }

#ifdef DEBUG
    if (fDebugDPIHelperScaling)
    {
        CString__FormatW(strFileName, L"DPIHelper_%05d_After.png", imgIndex++);
        pathTempFile.Combine(rgTempFolder, strFileName);
        gdiplusImage->Save(pathTempFile);
    }
#endif
  
    // Get the converted image handle - this returns a new HBITMAP that will need to be deleted when no longer needed
    // Detach using TransparentColor (transparent-black). If the result bitmap is to be used with AlphaBlend, that function 
    // keeps the background if the transparent pixels are black
    HBITMAP hBmpResult = gdiplusImage->Detach( TransparentColor );

    // When the image has 32bpp RGB format, when we call GDI+ to return an HBITMAP for the image, the result is actually
    // an ARGB bitmap (with alpha bytes==0xFF instead of reserved=0x00). Many GDI functions work with it fine, but 
    // adding it to an imagelist with ImageList_AddMasked will produce the wrong result, because the clrTransparent color 
    // won't match any background pixels due to the alpha byte value. So we need to zero-out out those bytes... 
    // If the bitmap was scaled with a bicubic/bilinear interpolation, the colors are interpolated with the clrBackground 
    // which may be transparent, so the resultant image will have alpha channel of interest, and we'll return the image as is.
    if (format == PixelFormat32bppRGB)
    {
        BITMAP bmp = {0};
        if (GetObject(hBmpResult, sizeof(bmp), &bmp) == sizeof(bmp) && bmp.bmBits != nullptr)
        {
            RGBQUAD* pPixels = reinterpret_cast<RGBQUAD*>(bmp.bmBits);

            for (int i=0; i< bmp.bmWidth * bmp.bmHeight; i++)
            {
                pPixels[i].rgbReserved = 0;
            }
        }
    }

    // Return the created image
    return hBmpResult;
}

// void CDpiHelper::LogicalToDeviceUnits(_Inout_ HIMAGELIST * pImageList, ImageScalingMode scalingMode)
// {
//     IfNullAssertRet(pImageList, "No imagelist given to convert");
// 
//     // If no scaling is required, the image can be used in current size
//     if (!IsScalingRequired())
//         return;
// 	if (!::IsImageScalingSupported())
// 		return;
// 
//     // Create a new HIMAGELIST for the device units
//     HIMAGELIST hDeviceImageList = CreateDeviceFromLogicalImage(*pImageList, scalingMode);
//     // If the device image could not be created, return and keep using the original
//     IfNullAssertRet(hDeviceImageList, "Failed to create scaled imagelist");
// 
//     // Delete the original image and return the converted image
//     ImageList_Destroy(*pImageList);
//     *pImageList = hDeviceImageList;
// }
// 
// HIMAGELIST CDpiHelper::CreateDeviceFromLogicalImage(HIMAGELIST _In_ hImageList, ImageScalingMode scalingMode)
// {
//     IfNullAssertRetNull(hImageList, "No imagelist given to convert");
// 
//     // If no scaling is required, return an image copy
//     if (!IsScalingRequired())
//         return ImageList_Duplicate(hImageList);
// 	if (!::IsImageScalingSupported())
// 		return;
// 
//     int nCount = ImageList_GetImageCount(hImageList);
// 
//     int cxImage = 0;
//     int cyImage = 0;
//     IfFailRetNull( ImageList_GetIconSize(hImageList, &cxImage, &cyImage) );
// 
//     int cxImageDevice = LogicalToDeviceUnitsX(cxImage);
//     int cyImageDevice = LogicalToDeviceUnitsY(cyImage);
// 
//     // Create the new device imagelist. Use ILC_COLOR24 instead of ILC_COLOR32 because the images we're adding with
//     // ImageList_AddMasked don't have alpha channel (have 0 bytes), which would otherwise be interpreted by imagelist themeing code
//     // imagelist shell theming code as being completely transparent (and losing color information), later resulting in black pixels when the themed imagelist is drawn
//     bool fImageListComplete = false;
//     HIMAGELIST hImageListDevice = ImageList_Create(cxImageDevice, cyImageDevice, ILC_COLOR24 | ILC_MASK, nCount /*cInitial*/, 0 /*cGrow*/);
//     IfNullRetNull(hImageListDevice);
//     SCOPE_GUARD({ 
//        if (!fImageListComplete) 
//            ImageList_Destroy(hImageListDevice);
//     });
// 
//     ImageList_SetBkColor(hImageListDevice, ImageList_GetBkColor(hImageList));
//     
//     if (nCount != 0)
//     {
//         CWinClientDC dcScreen(NULL);
//         IfNullRetNull(dcScreen);
// 
//         CWinManagedDC dcMemoryLogical(CreateCompatibleDC(dcScreen));
//         IfNullRetNull(dcMemoryLogical);
// 
//         HIMAGELIST hImageListNoTransparency = NULL;
//         SCOPE_GUARD({ 
//             if (hImageListNoTransparency)
//                ImageList_Destroy(hImageListNoTransparency);
//         });
//         
//         // If the source imagelist uses ILC_COLOR32, the color bitmap may have partial transparent pixels
//         // If we were to paint them on a Magenta background for our ILC_COLOR24 output bitmap, those pixels 
//         // would get a magenta tint. To get rid of the partial transparency, we'll create first a 24bpp 
//         // Imagelist with background of Halo color, and we'll copy the images from the original list.
//         // The imagelist background color is used for interpolation of partial transparent pixels.
// 
//         IMAGEINFO imageInfo = {0};
//         if (ImageList_GetImageInfo(hImageList, 0, &imageInfo))
//         {
//             BITMAPINFO bi = {0};
//             bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
//             
//             // Call GetDIBits without the underlying array to determine bitmap attributes
//             if (GetDIBits(dcScreen, imageInfo.hbmImage, /* uStartScan */ 0, /* cScanLines */ 0, /* lpvBits */ nullptr, &bi, DIB_RGB_COLORS))
//             {
//                 if (bi.bmiHeader.biBitCount == 32)
//                 {
//                     VSASSERT(imageInfo.hbmMask != NULL, "The imagelist contains 32bpp image with no mask, not supported yet. The results will be incorrect.");
//                     hImageListNoTransparency = ImageList_Create(cxImage, cyImage, ILC_COLOR24 | ILC_MASK, nCount /*cInitial*/, 0 /*cGrow*/);
//                     IfNullRetNull(hImageListNoTransparency);
// 
//                     ImageList_SetBkColor(hImageListNoTransparency, HaloColor.ToCOLORREF());
//                     
//                     for (int iImage = 0; iImage < nCount; iImage++)
//                     {
//                         // Unfortunately ImageList_Copy can only copy within same imagelist, 
//                         // so we have to extract icons one by one and add into the other imagelist
//                         HICON hIcon = ImageList_GetIcon(hImageList, iImage, 0);
//                         IfNullRetNull(hIcon);
//                         SCOPE_GUARD( DestroyIcon(hIcon); );
// 
//                         if (ImageList_AddIcon(hImageListNoTransparency, hIcon) == -1) 
//                             return NULL;
//                     }                    
// 
//                     // Set the background color, so further draw operations will use the mask
//                     ImageList_SetBkColor(hImageListNoTransparency, CLR_NONE);
//                 }
//             }
//         }
// 
//         HIMAGELIST hImageListToDraw = hImageListNoTransparency ? hImageListNoTransparency : hImageList;
// 
//         // Use Magenta for transparency
//         const Color& clrTransparency = MagentaColor;
// 
//         CWinManagedBrush brTransparent;
//         brTransparent.CreateSolidBrush(clrTransparency.ToCOLORREF());
//         IfNullRetNull(brTransparent);
// 
//         RECT rcImage = { 0, 0, cxImage, cyImage};
// 
//         for (int iImage = 0; iImage < nCount; iImage++)
//         {
//             CWinManagedBitmap bmpMemory;
//             bmpMemory.CreateCompatibleBitmap(dcScreen, cxImage, cyImage);
//             IfNullRetNull(bmpMemory);
// 
//             // Select the logical bitmap
//             dcMemoryLogical.SelectBitmap(bmpMemory);
// 
//             // Draw image by image in dcMemoryLogical
//             IfFailRetNull( dcMemoryLogical.FillRect(&rcImage, brTransparent) );
//             IfFailRetNull( ImageList_Draw(hImageListToDraw, iImage, dcMemoryLogical, 0, 0, ILD_NORMAL) );
// 
//             // Restore the original bitmap in the DC
//             dcMemoryLogical.SelectBitmap(dcMemoryLogical.m_hOriginalBitmap);
// 
//             // Now scale the image according with the current DPI 
//             HBITMAP hbmp = bmpMemory.Detach();
//             LogicalToDeviceUnits(&hbmp, scalingMode, clrTransparency);
//             bmpMemory.Attach(hbmp);
// 
//             // Add the device image to the new imagelist
//             if (ImageList_AddMasked(hImageListDevice, bmpMemory, clrTransparency.ToCOLORREF()) == -1)
//                 return NULL;
//         }
//     }
// 
//     // Flag that scop guard should not delete the image we'll be returning
//     fImageListComplete = true;
//     return hImageListDevice;
// }
// 
// void CDpiHelper::LogicalToDeviceUnits(_Inout_ HICON * pIcon, _In_opt_ const SIZE * pLogicalSize) const
// {
//     IfNullAssertRet(pIcon, "No icon given to convert");
// 
//     // If no scaling is required, the image can be used in current size
//     if (!IsScalingRequired())
//         return;
// 	if (!::IsImageScalingSupported())
// 		return;
// 
//     SIZE iconSize = {0};
//     if (!pLogicalSize)
//     {
//         // First, figure out the image size
//         pLogicalSize = &iconSize;
//         if (!GetIconSize(*pIcon, &iconSize))
//         {
//             return;
//         }
//     }
// 
//     *pIcon = CreateDeviceImageOrReuseIcon(*pIcon, false /*fAlwaysCreate*/, pLogicalSize);
// }
// 
// HICON CDpiHelper::CreateDeviceFromLogicalImage(_In_ HICON hIcon, _In_opt_ const SIZE * pLogicalSize) const
// {
//     IfNullAssertRetNull(hIcon, "No icon given to convert");
// 
//     SIZE iconSize = {0};
//     if (!pLogicalSize)
//     {
//         // First, figure out the image size
//         pLogicalSize = &iconSize;
//         if (!GetIconSize(hIcon, &iconSize))
//         {
//             return DuplicateIcon(NULL, hIcon);
//         }
//     }
// 
//     return CreateDeviceImageOrReuseIcon(hIcon, true /*fAlwaysCreate*/, pLogicalSize);
// }
// 
// bool CDpiHelper::GetIconSize(_In_ HICON hIcon, _Out_ SIZE * pSize) const
// {
//     bool fGotSize  = false;
// 
//     ICONINFO iconInfo = {0};
//     if (GetIconInfo(hIcon, &iconInfo))
//     {
//         BITMAP bmIconsBitmap = {0};
//         if ( ::GetObject(iconInfo.hbmColor,sizeof(bmIconsBitmap), &bmIconsBitmap) )
//         {
//             pSize->cx = bmIconsBitmap.bmWidth;
//             pSize->cy = bmIconsBitmap.bmHeight;
//             fGotSize = true;
//         }
// 
//         ::DeleteObject(iconInfo.hbmMask);
//         ::DeleteObject(iconInfo.hbmColor);
//     }
// 
//     return fGotSize;
// }
// 
// HICON CDpiHelper::CreateDeviceImageOrReuseIcon(_In_ HICON hIcon, bool fAlwaysCreate, const SIZE * pIconSize) const
// {
//     int cxIcon = LogicalToDeviceUnitsX(pIconSize->cx);
//     int cyIcon = LogicalToDeviceUnitsX(pIconSize->cy);
// 
//     UINT flags = fAlwaysCreate ? 0 : (LR_COPYDELETEORG | LR_COPYRETURNORG);
// 
//     HICON hDeviceIcon = static_cast<HICON>(::CopyImage(hIcon, IMAGE_ICON, cxIcon, cyIcon, flags | LR_COPYFROMRESOURCE));
//     if (hDeviceIcon == NULL)
//     {
//         // Couldn't load from resource (the image was not shared), try stretching the current image
//         hDeviceIcon = static_cast<HICON>(::CopyImage(hIcon, IMAGE_ICON, cxIcon, cyIcon, flags));
//         if (hDeviceIcon == NULL)
//         {
//             hDeviceIcon = fAlwaysCreate ? DuplicateIcon(NULL, hIcon) : hIcon;
//         }
//     }
// 
//     return hDeviceIcon;
// }

bool DpiHelper::m_fInitialized = false;
int  DpiHelper::m_DeviceDpiX = k_DefaultLogicalDpi;
int  DpiHelper::m_DeviceDpiY = k_DefaultLogicalDpi;

// helper for current window


// Thread protection on accessing the DPI Helpers vector
CComAutoCriticalSection DpiHelper::s_critSection;

vector<unique_ptr<CDpiHelper>> DpiHelper::s_helpers;

ThreadStatic<CDpiHelper*> s_current;
ThreadStatic<CDpiHelperScope*> s_currentScope;

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	std::set<UINT>& dpis = *(std::set<UINT>*)dwData;

	if (hMonitor)
	{
		UINT dpiX, dpiY;
		if (SUCCEEDED(VsUI::CDpiAwareness::GetDpiForMonitor(hMonitor, &dpiX, &dpiY)))
			dpis.insert(dpiX);
	}

	return true;
}

void DpiHelper::Initialize()
{
    if (!m_fInitialized)
    {
		// Don't use GetDpiForSystem here, because it is not the constant we need. 
		// We need the DPI which is used by DCs across this process. 
		UINT dpiX, dpiY;
		if (SUCCEEDED(VsUI::CDpiAwareness::GetDeviceDpi(&dpiX, &dpiY)))
		{
			m_DeviceDpiX = (int)dpiX;
			m_DeviceDpiY = (int)dpiY;
		}
		else
		{
			CWinClientDC dcScreen(nullptr);
			if (dcScreen)
			{
				m_DeviceDpiX = dcScreen.GetDeviceCaps(LOGPIXELSX);
				m_DeviceDpiY = dcScreen.GetDeviceCaps(LOGPIXELSY);
			}
			else
			{
				m_DeviceDpiX = k_DefaultLogicalDpi;
				m_DeviceDpiY = k_DefaultLogicalDpi;
			}
		}

		if (VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
		{
			// not simple to use make_unique because CDpiHelper intentionally  
			// has private constructor to avoid creating of unhandled helpers
			s_helpers.push_back(
				std::unique_ptr<CDpiHelper>(
				new CDpiHelper(
				m_DeviceDpiX, m_DeviceDpiY,
				k_DefaultLogicalDpi, k_DefaultLogicalDpi)));

			SetCurrent(s_helpers.back().get());
		}

		m_fInitialized = true;
    }
}

VsUI::CDpiHelper* DpiHelper::GetDefaultHelper(bool current /*= true*/)
{
	if (current && VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
	{
		auto currentHelper = GetCurrent();
		if (currentHelper)
			return currentHelper;
	}

    static CDpiHelper* s_pDefaultHelper = nullptr;
    if (s_pDefaultHelper == nullptr)
    {
        s_pDefaultHelper = GetForZoom(100);
        VSASSERT(s_pDefaultHelper != nullptr, "Cannot create DPI scaling helper for default 96dpi");
    }
    return s_pDefaultHelper;
}

ATL::CComAutoCriticalSection DpiHelper::s_handlersCritSection;
std::vector<IDpiHandler*> DpiHelper::s_handlers;

void DpiHelper::AddHandler(IDpiHandler* handler)
{
	CComCritSecLock<CComCriticalSection> lock(s_handlersCritSection);
	s_handlers.push_back(handler);
}

void DpiHelper::RemoveHandler(IDpiHandler* handler)
{
	CComCritSecLock<CComCriticalSection> lock(s_handlersCritSection);
	auto it = std::find(s_handlers.cbegin(), s_handlers.cend(), handler);
	if (it != s_handlers.cend())
		s_handlers.erase(it);
}

IDpiHandler* DpiHelper::FindHandler(HWND hWnd, bool fromParent /*= true*/)
{
	CComCritSecLock<CComCriticalSection> lock(s_handlersCritSection);

	for (auto handler : s_handlers)
	{
		HWND handlerWnd = handler->GetDpiHWND();
		
		if (!handlerWnd)
			continue;

		if (handlerWnd == hWnd || (fromParent && ::IsChild(handlerWnd, hWnd)))
		{
			return handler;
		}
	}

	return nullptr;
}

// Returns a CDpiHelper that can scale images created for the specified DPI zoom factor, or nullptr if we run out of memory
VsUI::CDpiHelper * DpiHelper::GetForZoom(int zoomPercents)
{
	// Protect multi-threaded access to the helpers map
	CComCritSecLock<CComCriticalSection> lock(s_critSection);

	// Also do the initialization within the critical section
	Initialize();

	return GetHelper(
		m_DeviceDpiX, m_DeviceDpiY,
		MulDiv(k_DefaultLogicalDpi, zoomPercents, 100),
		MulDiv(k_DefaultLogicalDpi, zoomPercents, 100));
}

VsUI::CDpiHelper * DpiHelper::GetHelper(int deviceDpiX, int deviceDpiY, int logicalDpiX, int logicalDpiY)
{
    // Protect multi-threaded access to the helpers map
    CComCritSecLock<CComCriticalSection> lock(s_critSection);

    // Also do the initialization within the critical section
    Initialize();

	for (size_t i = 0; i < s_helpers.size(); i++)
	{
		const auto & pHelper = s_helpers[i];

		if (pHelper->GetDeviceDpiX() == deviceDpiX &&   // those two mostly define helper, 
			pHelper->GetLogicalDpiY() == logicalDpiY && // so compare them first

			pHelper->GetDeviceDpiY() == deviceDpiY &&
			pHelper->GetLogicalDpiX() == logicalDpiX)
		{
			return pHelper.get();
		}
	}

    try
    {
		// not simple to use make_unique because CDpiHelper intentionally  
		// has private constructor to avoid creating of unhandled helpers
		s_helpers.push_back(
			std::unique_ptr<CDpiHelper>(
			new CDpiHelper(
			deviceDpiX, deviceDpiY,
			logicalDpiX, logicalDpiY)));
	
		return s_helpers.back().get();
	}
    catch (const bad_alloc&)
    {
        return nullptr;
    }
}

VsUI::CDpiHelper* DpiHelper::GetForDPI(int deviceDpi, int logicalDpi /*= k_DefaultLogicalDpi*/)
{
	return GetHelper(deviceDpi, deviceDpi, logicalDpi, logicalDpi);
}

VsUI::CDpiHelper* DpiHelper::GetForWindow(HWND hWnd, bool fromHandler /*= true*/)
{
	if (!VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled() || !IsWindow(hWnd))
		return GetDefaultHelper(false);

	if (fromHandler)
	{
		auto handler = FindHandler(hWnd);
		if (handler)
		{
			return handler->GetDpiHelper();
		}
	}

	return GetForDPI((int)CDpiAwareness::GetDpiForWindow(hWnd));
}

VsUI::CDpiHelper* DpiHelper::GetForMonitor(HMONITOR hMonitor)
{
	if (!VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
		return GetDefaultHelper(false);

	if (!hMonitor)
		hMonitor = ::MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

	return GetForDPI((int)CDpiAwareness::GetDpiForMonitor(hMonitor));
}

VsUI::CDpiHelper* DpiHelper::GetDefault()
{
	return GetDefaultHelper(false);
}

VsUI::CDpiHelper* DpiHelper::GetScreenHelper(HWND hWnd)
{
	if (VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
	{
		UINT wndDpiX, wndDpiY;
		if (SUCCEEDED(VsUI::CDpiAwareness::GetDpiForWindow(hWnd, &wndDpiX, &wndDpiY)))
		{
			return GetHelper((int)wndDpiX, (int)wndDpiY, k_DefaultLogicalDpi, k_DefaultLogicalDpi);
		}
	}

	return GetDefaultHelper(false);
}

VsUI::CDpiHelper* DpiHelper::GetScreenHelper(LPCRECT rect)
{
	if (VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
	{
		HMONITOR monitor = ::MonitorFromRect(rect, MONITOR_DEFAULTTOPRIMARY);
		if (monitor)
		{
			UINT monDpiX, monDpiY;
			if (SUCCEEDED(VsUI::CDpiAwareness::GetDpiForMonitor(monitor, &monDpiX, &monDpiY)))
			{
				return GetHelper((int)monDpiX, (int)monDpiY, k_DefaultLogicalDpi, k_DefaultLogicalDpi);
			}
		}
	}

	return GetDefaultHelper(false);
}

// Get device DPI.
int DpiHelper::GetDeviceDpiX() 
{ 
    IfNullRetX(GetDefaultHelper(), m_DeviceDpiY);
    return GetDefaultHelper()->GetDeviceDpiX();
}

int DpiHelper::GetDeviceDpiY() 
{ 
    IfNullRetX(GetDefaultHelper(), m_DeviceDpiY);
    return GetDefaultHelper()->GetDeviceDpiY();
}

// Get logical DPI.
int DpiHelper::GetLogicalDpiX() 
{ 
    IfNullRetX(GetDefaultHelper(), m_DeviceDpiX);
    return GetDefaultHelper()->GetLogicalDpiX();
}

int DpiHelper::GetLogicalDpiY() 
{ 
    IfNullRetX(GetDefaultHelper(), m_DeviceDpiY);
    return GetDefaultHelper()->GetLogicalDpiY();
}

// Return whether scaling is required
bool DpiHelper::IsScalingRequired()
{
    IfNullRetX(GetDefaultHelper(), false);
    return GetDefaultHelper()->IsScalingRequired();
}

bool DpiHelper::IsScalingSupported()
{
	return ::IsImageScalingSupported();
}

// Return horizontal and vertical scaling factors
double DpiHelper::DeviceToLogicalUnitsScalingFactorX()
{
    IfNullRetX(GetDefaultHelper(), 1);
    return GetDefaultHelper()->DeviceToLogicalUnitsScalingFactorX();
}

double DpiHelper::DeviceToLogicalUnitsScalingFactorY()
{
    IfNullRetX(GetDefaultHelper(), 1);
    return GetDefaultHelper()->DeviceToLogicalUnitsScalingFactorY();
}

double DpiHelper::LogicalToDeviceUnitsScalingFactorX()
{
    IfNullRetX(GetDefaultHelper(), 1);
    return GetDefaultHelper()->LogicalToDeviceUnitsScalingFactorX();
}

double DpiHelper::LogicalToDeviceUnitsScalingFactorY()
{
    IfNullRetX(GetDefaultHelper(), 1);
    return GetDefaultHelper()->LogicalToDeviceUnitsScalingFactorY();
}

// Converts between logical and device units.
int DpiHelper::ImgLogicalToDeviceUnitsX(int x) 
{ 
    IfNullRetX(GetDefaultHelper(), x);
    return GetDefaultHelper()->ImgLogicalToDeviceUnitsX(x);
}

int DpiHelper::ImgLogicalToDeviceUnitsY(int y) 
{ 
    IfNullRetX(GetDefaultHelper(), y);
    return GetDefaultHelper()->ImgLogicalToDeviceUnitsY(y);
}

int DpiHelper::LogicalToDeviceUnitsX(int x) 
{ 
    IfNullRetX(GetDefaultHelper(), x);
    return GetDefaultHelper()->LogicalToDeviceUnitsX(x);
}

int DpiHelper::LogicalToDeviceUnitsY(int y) 
{ 
    IfNullRetX(GetDefaultHelper(), y);
    return GetDefaultHelper()->LogicalToDeviceUnitsY(y);
}

// Converts between device and logical units.
int DpiHelper::DeviceToLogicalUnitsX(int x) 
{ 
    IfNullRetX(GetDefaultHelper(), x);
    return GetDefaultHelper()->DeviceToLogicalUnitsX(x);
}

int DpiHelper::DeviceToLogicalUnitsY(int y) 
{ 
    IfNullRetX(GetDefaultHelper(), y);
    return GetDefaultHelper()->DeviceToLogicalUnitsY(y);
}

// Converts from logical units to device units.
void DpiHelper::LogicalToDeviceUnits(_Inout_ RECT * pRect)
{
    IfNullRet(GetDefaultHelper());
    return GetDefaultHelper()->LogicalToDeviceUnits(pRect);
}

void DpiHelper::LogicalToDeviceUnits(_Inout_ POINT * pPoint)
{
    IfNullRet(GetDefaultHelper());
    return GetDefaultHelper()->LogicalToDeviceUnits(pPoint);
}

// Converts from device units to logical units.
void DpiHelper::DeviceToLogicalUnits(_Inout_ RECT * pRect)
{
    IfNullRet(GetDefaultHelper());
    return GetDefaultHelper()->DeviceToLogicalUnits(pRect);
}

void DpiHelper::DeviceToLogicalUnits(_Inout_ POINT * pPoint)
{
    IfNullRet(GetDefaultHelper());
    return GetDefaultHelper()->DeviceToLogicalUnits(pPoint);
}

// Convert a point size (1/72 of an inch) to raw pixels.
int DpiHelper::PointsToDeviceUnits(int pt) 
{ 
    IfNullRetX(GetDefaultHelper(), 0);
    return GetDefaultHelper()->PointsToDeviceUnits(pt);
}

int HDPIAPI DpiHelper::PointsToDeviceUnits(double pt)
{
	IfNullRetX(GetDefaultHelper(), 0);
	return GetDefaultHelper()->PointsToDeviceUnits(pt);
}

// Determine the screen dimensions in logical units.
int DpiHelper::LogicalScreenWidth() 
{ 
    IfNullRetX(GetDefaultHelper(), 0);
    return GetDefaultHelper()->LogicalScreenWidth();
}

int DpiHelper::LogicalScreenHeight() 
{ 
    IfNullRetX(GetDefaultHelper(), 0);
    return GetDefaultHelper()->LogicalScreenHeight();
}

// Determine if screen resolution meets minimum requirements in logical pixels.
bool DpiHelper::IsResolutionAtLeast(int cxMin, int cyMin)
{ 
    IfNullRetX(GetDefaultHelper(), false);
    return GetDefaultHelper()->IsResolutionAtLeast(cxMin, cyMin);
}

CDpiHelper* HDPIAPI DpiHelper::SetCurrent(CDpiHelper* helper)
{
	if (!VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
		return GetDefaultHelper(false);

	auto old = s_current();
	s_current() = helper;

	if (old)
		return old;

	return GetDefaultHelper(false);
}

VsUI::CDpiHelper* HDPIAPI
DpiHelper::GetCurrent()
{
	if (!VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
		return GetDefaultHelper(false);

	auto current = s_current();

	if (current)
		return current;

	return GetDefaultHelper(false);
}

CDpiHelper* HDPIAPI DpiHelper::SetCurrentForWindow(HWND hWnd /*= nullptr*/, bool findHandler /*= true*/)
{
	_ASSERTE(::IsWindow(hWnd));

	if (!VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
		return GetDefaultHelper(false);

	if (!hWnd || !::IsWindow(hWnd))
	{
		hWnd = DispatchingWindow();

		if (!hWnd || !::IsWindow(hWnd))
		{
			return GetDefaultHelper(false);
		}
	}

	// We could maybe omit this, but it allows us 
	// to have special helper for special cases like it was for minihelp
	// Handler is useful in cases when DPI is taken from WM_DPICHANGED message.
	// In such cases DPI from HWND is still old one.
	// Removing this could break something that already works and is tested.
    // see comments in case: 142313
    if (findHandler)
    {
		auto handler = FindHandler(hWnd);
		if (handler)
		{
			return SetCurrent(handler->GetDpiHelper());
		}
    }

	int dpi = (int)CDpiAwareness::GetDpiForWindow(hWnd);

	auto current = GetCurrent();
	if (current && current->GetDeviceDpiX() == dpi)
	{
		return current;
	}

	return SetCurrent(GetForDPI(dpi));
}

VsUI::CDpiHelper* HDPIAPI DpiHelper::SetCurrentForDPI(UINT dpi)
{
	if (!VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
		return GetDefaultHelper(false);

	auto current = GetCurrent();
	if (current && current->GetDeviceDpiX() == (int)dpi)
	{
		return current;
	}

	return SetCurrent(GetForDPI((int)dpi));
}

CDpiHelper* HDPIAPI DpiHelper::SetCurrentForMonitor(HMONITOR hMon)
{
	if (!VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
		return GetDefaultHelper(false);

	if (hMon == nullptr)
		hMon = ::MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

	int dpi = (int)CDpiAwareness::GetDpiForMonitor(hMon);

	auto current = GetCurrent();
	if (current && current->GetDeviceDpiX() == dpi)
	{
		return current;
	}

	return SetCurrent(GetForDPI(dpi));
}

std::unique_ptr<CDpiHelperScope> HDPIAPI DpiHelper::SetDefaultForDPI(UINT dpi)
{
	return std::make_unique<CDpiHelperScope>(dpi);
}

std::unique_ptr<CDpiHelperScope> HDPIAPI DpiHelper::SetDefaultForWindow(HWND hWnd /*= nullptr*/, bool findHandler/* = true*/)
{
	return std::make_unique<CDpiHelperScope>(hWnd, findHandler);
}

std::unique_ptr<CDpiHelperScope> HDPIAPI DpiHelper::SetDefaultForWindow(CWnd* pWnd /*= nullptr*/, bool findHandler /*= true*/)
{
	if (pWnd == nullptr)
		return SetDefaultForWindow((HWND)nullptr, findHandler);
	else
		return SetDefaultForWindow(pWnd->GetSafeHwnd(), findHandler);
}

std::unique_ptr<VsUI::CDpiHelperScope> HDPIAPI DpiHelper::SetDefaultDirect(CDpiHelper* helper)
{
	return std::make_unique<CDpiHelperScope>(helper);
}

// std::unique_ptr<CDpiHelperScope> HDPIAPI DpiHelper::SetDefaultForWindowFromDC(HDC dc /*= nullptr*/)
// {
// 	if (dc)
// 		return SetDefaultForWindow(::WindowFromDC(dc));
// 	else
// 		return SetDefaultForWindow((HWND)nullptr);
// }

std::unique_ptr<VsUI::CDpiHelperScope> HDPIAPI DpiHelper::SetDefaultForMonitor(HMONITOR hMon)
{
	VSASSERT(hMon, "Passed monitor handle is NULL");
	return std::make_unique<CDpiHelperScope>(hMon);
}

std::unique_ptr<VsUI::CDpiHelperScope> HDPIAPI DpiHelper::SetDefaultForMonitor(const POINT & pt, DWORD flags /*= MONITOR_DEFAULTTONEAREST*/)
{
	return SetDefaultForMonitor(::MonitorFromPoint(pt, flags));
}

std::unique_ptr<VsUI::CDpiHelperScope> HDPIAPI DpiHelper::SetDefaultForMonitor(const RECT & rect, DWORD flags /*= MONITOR_DEFAULTTONEAREST*/)
{
	return SetDefaultForMonitor(::MonitorFromRect(&rect, flags));
}

int DpiHelper::GetSystemMetrics(int nIndex)
{
	return VsUI::CDpiAwareness::GetSystemMetricsForDPI(nIndex, (UINT)GetDeviceDpiX());
}

std::vector<UINT> DpiHelper::GetDPIList()
{
	std::set<UINT> dpiSet;

	// return all DPIs only if it matters, otherwise return default (mainly for pre-vs2019 IDEs)
	if (VsUI::CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
	{
		::EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)& dpiSet);
	}
	
	// [case: 142324] include default DPI for System DPI aware windows and pre-vs2019 IDEs.
	// Default DPI image lists are used by windows unaffected by DPI changes. 
	// Note: For per monitor aware this is also necessary in case when system awareness is forced by user 
	// and user changes DPI of the default monitor. In such case DPI of the application differs from DPI of any monitor.
	dpiSet.insert((UINT)GetDeviceDpiX()); 

	return std::vector<UINT>(dpiSet.cbegin(), dpiSet.cend());
}

bool DpiHelper::IsMultiDpiEnvironment()
{
	if (!VsUI::CDpiAwareness::IsWindowsSupportedVersion())
		return false;

	std::set<UINT> dpiSet;
	dpiSet.insert((UINT)GetDeviceDpiX()); 
	::EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&dpiSet);
	return dpiSet.size() > 1;
}

bool HDPIAPI DpiHelper::IsWithinDpiScope()
{
    return CDpiHelperScope::GetActive() != nullptr;
}

// Return the monitor information in logical units
BOOL DpiHelper::GetLogicalMonitorInfo(_In_ HMONITOR hMonitor, _Out_ LPMONITORINFO lpmi)
{
    IfNullRetX(GetDefaultHelper(), FALSE);
    return GetDefaultHelper()->GetLogicalMonitorInfo(hMonitor, lpmi);
}

// Convert GdiplusImage from logical to device units
// void DpiHelper::LogicalToDeviceUnits(_Inout_ VsUI::GdiplusImage * pImage, ImageScalingMode scalingMode, Color clrBackground)
// {
// 	if (!::IsImageScalingSupported())
// 		return;
//     IfNullRet(GetDefaultHelper());
//     return GetDefaultHelper()->LogicalToDeviceUnits(pImage, scalingMode, clrBackground);
// }
 
// Creates new GdiplusImage from logical to device units
unique_ptr<VsUI::GdiplusImage> DpiHelper::CreateDeviceFromLogicalImage(_In_ VsUI::GdiplusImage* pImage, ImageScalingMode scalingMode, Color clrBackground)
{
    IfNullRetNull(GetDefaultHelper());
    return GetDefaultHelper()->CreateDeviceFromLogicalImage(pImage, scalingMode, clrBackground);
}

void DpiHelper::LogicalToDeviceUnits(_Inout_ HBITMAP * pImage, ImageScalingMode scalingMode, Color clrBackground)
{
	if (!::IsImageScalingSupported())
		return;
    IfNullRet(GetDefaultHelper());
    GetDefaultHelper()->LogicalToDeviceUnits(pImage, scalingMode, clrBackground);
}

void DpiHelper::LogicalToDeviceUnits(_Inout_ CBitmap& bitMap, ImageScalingMode scalingMode, Color clrBackground)
{
	if (!::IsImageScalingSupported())
		return;
    IfNullRet(GetDefaultHelper());
	HBITMAP tmp = (HBITMAP) bitMap.Detach();
    GetDefaultHelper()->LogicalToDeviceUnits(&tmp, scalingMode, clrBackground);
	bitMap.Attach(tmp);
}

HBITMAP DpiHelper::CreateDeviceFromLogicalImage(HBITMAP _In_ hImage, ImageScalingMode scalingMode, Color clrBackground)
{
    IfNullRetNull(GetDefaultHelper());
    return GetDefaultHelper()->CreateDeviceFromLogicalImage(hImage, scalingMode, clrBackground);
}

bool HDPIAPI DpiHelper::IsCurrent(const CDpiHelper* helper)
{
	return GetCurrent() == helper;
}

// void DpiHelper::LogicalToDeviceUnits(_Inout_ HIMAGELIST * pImageList, ImageScalingMode scalingMode)
// {
// 	if (!::IsImageScalingSupported())
// 		return;
//     IfNullRet(GetDefaultHelper());
//     GetDefaultHelper()->LogicalToDeviceUnits(pImageList, scalingMode);
// }
// 
// void DpiHelper::LogicalToDeviceUnits(_Inout_ CImageList * pImageList, ImageScalingMode scalingMode)
// {
// 	if (!::IsImageScalingSupported())
// 		return;
//     IfNullRet(GetDefaultHelper());
// 	HIMAGELIST tmp = (HIMAGELIST) pImageList->Detach();
//     GetDefaultHelper()->LogicalToDeviceUnits(&tmp, scalingMode);
// 	pImageList->Attach(tmp);
// }
// 
// HIMAGELIST DpiHelper::CreateDeviceFromLogicalImage(HIMAGELIST _In_ hImageList, ImageScalingMode scalingMode)
// {
//     IfNullRetNull(GetDefaultHelper());
//     return GetDefaultHelper()->CreateDeviceFromLogicalImage(hImageList, scalingMode);
// }
// 
// void DpiHelper::LogicalToDeviceUnits(_Inout_ HICON * pIcon, _In_opt_ const SIZE * pLogicalSize)
// {
// 	if (!::IsImageScalingSupported())
// 		return;
//     IfNullRet(GetDefaultHelper());
//     return GetDefaultHelper()->LogicalToDeviceUnits(pIcon, pLogicalSize);
// }
// 
// HICON DpiHelper::CreateDeviceFromLogicalImage(_In_ HICON hIcon, _In_opt_ const SIZE * pLogicalSize)
// {
//     IfNullRetNull(GetDefaultHelper());
//     return GetDefaultHelper()->CreateDeviceFromLogicalImage(hIcon, pLogicalSize);
// }

CDpiHelperScope::CDpiHelperScope(CDpiHelper* helper)
{
    m_prev_scope = s_currentScope.Get();
    s_currentScope.Set(this);
	m_curr = helper;
	m_prev = DpiHelper::SetCurrent(m_curr);
}

CDpiHelperScope::CDpiHelperScope(HWND hWnd, bool findHandler /*= true*/)
{
	m_prev_scope = s_currentScope.Get();
	s_currentScope.Set(this);
	m_prev = DpiHelper::SetCurrentForWindow(hWnd, findHandler);
	m_curr = DpiHelper::GetCurrent();
}

CDpiHelperScope::CDpiHelperScope(HDC hDC)
{
	m_prev_scope = s_currentScope.Get();
	s_currentScope.Set(this);
	m_prev = DpiHelper::SetCurrentForWindow(::WindowFromDC(hDC));
	m_curr = DpiHelper::GetCurrent();
}

CDpiHelperScope::CDpiHelperScope(HMONITOR hMon)
{
	m_prev_scope = s_currentScope.Get();
	s_currentScope.Set(this);
	m_prev = DpiHelper::SetCurrentForMonitor(hMon);
	m_curr = DpiHelper::GetCurrent();
}

CDpiHelperScope::CDpiHelperScope(UINT dpi)
{
	m_prev_scope = s_currentScope.Get();
	s_currentScope.Set(this);
	m_prev = DpiHelper::SetCurrentForDPI(dpi);
	m_curr = DpiHelper::GetCurrent();
}

CDpiHelperScope::~CDpiHelperScope()
{
	if (DpiHelper::IsCurrent(m_curr))
		DpiHelper::SetCurrent(m_prev);

    if (s_currentScope.Get() == this)
    {
		s_currentScope.Set(m_prev_scope);
    }
}

CDpiHelperScope* CDpiHelperScope::GetActive()
{
    return s_currentScope.Get();
}

} // namespace

#ifdef _DEBUG
CLINKAGE BOOL ENTRYPOINT VsEnsureDebuggerPresent()
{
	return TRUE;
}
#endif // _DEBUG
