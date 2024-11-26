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

#pragma once

#include "VsUIGdiplusImage.h"
#include <memory>
#include <vector>

#pragma warning(disable:4482)

class IDpiHandler;
class CDpiHandler;

namespace VsUI
{
    #define HDPIAPI __stdcall

    // NOTE: The image scaling modes available here for Win32 match the similar scaling modes for WinForms from 
    // Microsoft.VisualStudio.PlatformUI.DpiHelper class
    // If changes are made to algorithms in this native DpiHelper class, matching changes will have to be made to the managed class, too.
    enum /*class*/ ImageScalingMode
    {
        Default             = 0, // Let the shell pick what looks best depending on the current DPI zoom factor
        BorderOnly          = 1, // Keep the actual image unscaled, add a border around the image
        NearestNeighbor     = 2, // Sharp results, but pixelated, and possibly distorted unless multiple of 100% scaling
        Bilinear            = 3, // Smooth results, without distorsions, but fuzzy (GDI+ InterpolationModeBilinear) 
        Bicubic             = 4, // Smooth results, without distorsions, but fuzzy (GDI+ InterpolationModeBicubic)  
        HighQualityBilinear = 5, // Smooth results, without distorsions, but fuzzy (GDI+ InterpolationModeHighQualityBilinear)
        HighQualityBicubic  = 6, // Smooth results, without distorsions, but fuzzy. Some overshooting/oversharpening-like artifacts may be present (GDI+ InterpolationModeHighQualityBicubic)
    };

    class CDpiHelperScope;

    class CDpiHelper
    {
		friend class DpiHelper;

        // Constructor
        CDpiHelper(int iDeviceDpiX, int iDeviceDpiY, int iLogicalDpiX, int iLogicalDpiY);
	public:
        // Get device DPI.
        int HDPIAPI GetDeviceDpiX() const;
        int HDPIAPI GetDeviceDpiY() const;

        // Get logical DPI.
        int HDPIAPI GetLogicalDpiX() const;
        int HDPIAPI GetLogicalDpiY() const;

        // Returns whether scaling is required when converting between logical-device units
        bool HDPIAPI IsScalingRequired() const;
        
        // Return horizontal and vertical scaling factors
        double DeviceToLogicalUnitsScalingFactorX() const;
        double DeviceToLogicalUnitsScalingFactorY() const;
        double LogicalToDeviceUnitsScalingFactorX() const;
        double LogicalToDeviceUnitsScalingFactorY() const;

        // Converts between logical and device units.
		int HDPIAPI ImgLogicalToDeviceUnitsX(int x) const;
        int HDPIAPI ImgLogicalToDeviceUnitsY(int y) const;
        int HDPIAPI LogicalToDeviceUnitsX(int x) const;
        int HDPIAPI LogicalToDeviceUnitsY(int y) const;
    
        // Converts between device and logical units.
        int HDPIAPI DeviceToLogicalUnitsX(int x) const;
        int HDPIAPI DeviceToLogicalUnitsY(int y) const;

        // Converts from logical units to device units.
        void HDPIAPI LogicalToDeviceUnits(_Inout_ POINT * pPoint) const;
		void HDPIAPI LogicalToDeviceUnits(_Inout_ RECT* pRect, const POINT* pPivot = nullptr) const;

        // Converts from device units to logical units.
        void HDPIAPI DeviceToLogicalUnits(_Inout_ POINT * pPoint) const;
        void HDPIAPI DeviceToLogicalUnits(_Inout_ RECT * pRect, const POINT* pPivot = nullptr) const;
        
        // Converts (if necessary) the image from logical to device pixels. By default we use interpolation that gives smoother results when scaling up.
        // The functions will return the original image if no scaling is required due to high DPI modes.
        // Should scaling be necessary, the original image will be destroyed, a new scaled imaged be returned, and the caller will now control the lifetime of the scaled image.
        void HDPIAPI LogicalToDeviceUnits(_Inout_ std::unique_ptr<VsUI::GdiplusImage> * pImage, ImageScalingMode scalingMode = ImageScalingMode::Default, Gdiplus::Color clrBackground = TransparentColor);
        void HDPIAPI LogicalToDeviceUnits(_Inout_ HBITMAP * pImage, ImageScalingMode scalingMode = ImageScalingMode::Default, Gdiplus::Color clrBackground = TransparentColor);
//         void HDPIAPI LogicalToDeviceUnits(_Inout_ HIMAGELIST * pImageList, ImageScalingMode scalingMode = ImageScalingMode::Default);
//         void HDPIAPI LogicalToDeviceUnits(_Inout_ HICON * pIcon, _In_opt_ const SIZE * pLogicalSize = nullptr) const;

        // Creates and returns a new image suitable for display on device units. A clone image will be created when scaling is not necessary. The caller is reponsible of the lifetime of the returned image.
        std::unique_ptr<VsUI::GdiplusImage> HDPIAPI CreateDeviceFromLogicalImage(_In_ VsUI::GdiplusImage* pImage, ImageScalingMode scalingMode = ImageScalingMode::Default, Gdiplus::Color clrBackground = TransparentColor);
        HBITMAP HDPIAPI CreateDeviceFromLogicalImage(_In_ HBITMAP hImage, ImageScalingMode scalingMode = ImageScalingMode::Default, Gdiplus::Color clrBackground = TransparentColor);
        HIMAGELIST HDPIAPI CreateDeviceFromLogicalImage(_In_ HIMAGELIST hImageList, ImageScalingMode scalingMode = ImageScalingMode::Default);
        HICON HDPIAPI CreateDeviceFromLogicalImage(_In_ HICON hIcon, _In_opt_ const SIZE * pLogicalSize = nullptr) const;

        // Convert a point size (1/72 of an inch) to device units.
        int HDPIAPI PointsToDeviceUnits(int pt) const;
		int HDPIAPI PointsToDeviceUnits(double pt) const;

        // Determine the screen dimensions in logical units.
        int HDPIAPI LogicalScreenWidth() const;
        int HDPIAPI LogicalScreenHeight() const;
        
        // Return the monitor information in logical units
        BOOL HDPIAPI GetLogicalMonitorInfo(_In_ HMONITOR hMonitor, _Out_ LPMONITORINFO lpmi) const;

        // Determine if screen resolution meets minimum requirements in logical units.
        bool HDPIAPI IsResolutionAtLeast(int cxMin, int cyMin) const;

    protected:
        bool GetIconSize(_In_ HICON hIcon, _Out_ SIZE * pSize) const;
        HICON CreateDeviceImageOrReuseIcon(_In_ HICON hIcon, bool fAlwaysCreate, _In_ const SIZE * pIconSize) const;

        // Gets the interpolation mode from the specified scaling mode
        Gdiplus::InterpolationMode GetInterpolationMode(_In_ ImageScalingMode scalingMode);
        // Gets the actual scaling mode to be used from the suggested scaling mode
        ImageScalingMode GetActualScalingMode(_In_ ImageScalingMode scalingMode);
        // Get the scaling mode for the specified dpi zom factor
        ImageScalingMode GetDefaultScalingMode(int dpiScalePercent) const;
        // Returns the user preference for scaling mode or default, if the user doesn't want to override
        ImageScalingMode GetUserScalingMode(int dpiScalePercent, ImageScalingMode defaultScalingMode) const;
        // Returns the preferred scaling mode for current DPI zoom level (either shell preferred mode, or a user-override)
        ImageScalingMode GetPreferredScalingMode();

        int m_DeviceDpiX;
        int m_DeviceDpiY;
        int m_LogicalDpiX;
        int m_LogicalDpiY;

        // The shell preferred image scaling mode for current DPI zoom level
        ImageScalingMode m_PreferredScalingMode;
    };

    // The static functions in the DpiHelper class delegate the calls to the default CDpiHelper for 96dpi.
    // Definition: logical pixel = 1 pixel at 96 DPI
    class DpiHelper
    {
		friend class IDpiHandler;
		friend class CDpiHandler;
		friend class CDpiHelperScope;

		static const int k_DefaultLogicalDpi = 96;

		static CComAutoCriticalSection s_handlersCritSection;
		static std::vector<IDpiHandler*> s_handlers;

		static void AddHandler(IDpiHandler* handler);
		static void RemoveHandler(IDpiHandler* handler);
	
	public:
	    static IDpiHandler* FindHandler(HWND hWnd, bool fromParent = true);

        // Returns cached CDpiHelper that can scale images created for the specified DPI zoom factor
		static VsUI::CDpiHelper* GetForZoom(int zoomPercent);
		static VsUI::CDpiHelper* GetForDPI(int deviceDpi, int logicalDpi = k_DefaultLogicalDpi);
		static VsUI::CDpiHelper* GetForWindow(HWND hWnd, bool fromHandler = true);
		static VsUI::CDpiHelper* GetForMonitor(HMONITOR hMonitor);

		static VsUI::CDpiHelper* GetDefault();

		// Returns cached CDpiHelper for specific DPI values
		static VsUI::CDpiHelper * GetHelper(int deviceDpiX, int deviceDpiY, int logicalDpiX, int logicalDpiY);

		static VsUI::CDpiHelper* GetScreenHelper(HWND hWnd);
		static VsUI::CDpiHelper* GetScreenHelper(LPCRECT rect);

        // Get device DPI.
        static int HDPIAPI GetDeviceDpiX();
        static int HDPIAPI GetDeviceDpiY();

        // Get logical DPI.
        static int HDPIAPI GetLogicalDpiX();
        static int HDPIAPI GetLogicalDpiY();
        
        // Returns whether scaling is required when converting between logical-device units
        static bool HDPIAPI IsScalingRequired();
		static bool HDPIAPI IsScalingSupported();
        
        // Return horizontal and vertical scaling factors
        static double DeviceToLogicalUnitsScalingFactorX();
        static double DeviceToLogicalUnitsScalingFactorY();
        static double LogicalToDeviceUnitsScalingFactorX();
        static double LogicalToDeviceUnitsScalingFactorY();

        // Converts between logical and device units.
        static int HDPIAPI ImgLogicalToDeviceUnitsX(int x);
        static int HDPIAPI ImgLogicalToDeviceUnitsY(int y);
        static int HDPIAPI LogicalToDeviceUnitsX(int x);
        static int HDPIAPI LogicalToDeviceUnitsY(int y);
    
        // Converts between device and logical units.
        static int HDPIAPI DeviceToLogicalUnitsX(int x);
        static int HDPIAPI DeviceToLogicalUnitsY(int y);

        // Converts from logical units to device units.
        static void HDPIAPI LogicalToDeviceUnits(_Inout_ POINT * pPoint);
        static void HDPIAPI LogicalToDeviceUnits(_Inout_ RECT * pRect);

        // Converts from device units to logical units.
        static void HDPIAPI DeviceToLogicalUnits(_Inout_ POINT * pPoint);
        static void HDPIAPI DeviceToLogicalUnits(_Inout_ RECT * pRect);
        
        // Converts (if necessary) the image from logical to device pixels. By default we use interpolation that gives smoother results when scaling up.
        // The functions will return the original image if no scaling is required due to high DPI modes.
        // Should scaling be necessary, the original image will be destroyed, a new scaled imaged be returned, and the caller will now control the lifetime of the scaled image.
//         static void HDPIAPI LogicalToDeviceUnits(_Inout_ VsUI::GdiplusImage * pImage, ImageScalingMode scalingMode = ImageScalingMode::Default, Gdiplus::Color clrBackground = TransparentColor);
        static void HDPIAPI LogicalToDeviceUnits(_Inout_ HBITMAP * pImage, ImageScalingMode scalingMode = ImageScalingMode::Default, Gdiplus::Color clrBackground = TransparentColor);
        static void HDPIAPI LogicalToDeviceUnits(_Inout_ CBitmap& bitMap, ImageScalingMode scalingMode = ImageScalingMode::Default, Gdiplus::Color clrBackground = TransparentColor);
//         static void HDPIAPI LogicalToDeviceUnits(_Inout_ HIMAGELIST * pImageList, ImageScalingMode scalingMode = ImageScalingMode::Default);
//         static void HDPIAPI LogicalToDeviceUnits(_Inout_ CImageList * pImageList, ImageScalingMode scalingMode = ImageScalingMode::Default);
        // Note: Currently, icon scaling supports either loading a different frame from the icon resource or NearestNeighbor resizing
//         static void HDPIAPI LogicalToDeviceUnits(_Inout_ HICON * pIcon, _In_opt_ const SIZE * pLogicalSize = nullptr);

        // Creates and returns a new image suitable for display on device units. A clone image will be created when scaling is not necessary. The caller is reponsible of the lifetime of the returned image.
        static std::unique_ptr<VsUI::GdiplusImage> HDPIAPI CreateDeviceFromLogicalImage(_In_ VsUI::GdiplusImage* pImage, ImageScalingMode scalingMode = ImageScalingMode::Default, Gdiplus::Color clrBackground = TransparentColor);
        static HBITMAP HDPIAPI CreateDeviceFromLogicalImage(_In_ HBITMAP hImage, ImageScalingMode scalingMode = ImageScalingMode::Default, Gdiplus::Color clrBackground = TransparentColor);
        static HIMAGELIST HDPIAPI CreateDeviceFromLogicalImage(_In_ HIMAGELIST hImageList, ImageScalingMode scalingMode = ImageScalingMode::Default);
        static HICON HDPIAPI CreateDeviceFromLogicalImage(_In_ HICON hIcon, _In_opt_ const SIZE * pLogicalSize = nullptr);

        // Convert a point size (1/72 of an inch) to device units.
        static int HDPIAPI PointsToDeviceUnits(int pt);
		static int HDPIAPI PointsToDeviceUnits(double pt);

        // Determine the screen dimensions in logical units.
        static int HDPIAPI LogicalScreenWidth();
        static int HDPIAPI LogicalScreenHeight();
        
        // Return the monitor information in logical units
        static BOOL HDPIAPI GetLogicalMonitorInfo(_In_ HMONITOR hMonitor, _Out_ LPMONITORINFO lpmi);

        // Determine if screen resolution meets minimum requirements in logical units.
        static bool HDPIAPI IsResolutionAtLeast(int cxMin, int cyMin);

		// Sets default helper and returns RAII object to control the scope
		static std::unique_ptr<CDpiHelperScope> HDPIAPI SetDefaultDirect(CDpiHelper* helper);
		static std::unique_ptr<CDpiHelperScope> HDPIAPI SetDefaultForWindow(HWND hWnd = nullptr, bool findHandler = true);
		static std::unique_ptr<CDpiHelperScope> HDPIAPI SetDefaultForWindow(CWnd * pWnd = nullptr, bool findHandler = true);
		//static std::unique_ptr<CDpiHelperScope> HDPIAPI SetDefaultForWindowFromDC(HDC dc = nullptr);
		static std::unique_ptr<CDpiHelperScope> HDPIAPI SetDefaultForMonitor(HMONITOR hMon);
		static std::unique_ptr<CDpiHelperScope> HDPIAPI SetDefaultForMonitor(const POINT& pt, DWORD flags = MONITOR_DEFAULTTONEAREST);
		static std::unique_ptr<CDpiHelperScope> HDPIAPI SetDefaultForMonitor(const RECT& rect, DWORD flags = MONITOR_DEFAULTTONEAREST);
		static std::unique_ptr<CDpiHelperScope> HDPIAPI SetDefaultForDPI(UINT dpi);

		static int GetSystemMetrics(int nIndex);

		static std::vector<UINT> GetDPIList();
	    static bool IsMultiDpiEnvironment();

        static bool HDPIAPI IsWithinDpiScope();

	private:
		// Returns the helper for 100% zoom factor, aka 96dpi
        static CDpiHelper* GetDefaultHelper(bool current = true);
        static CComAutoCriticalSection s_critSection;
		static std::vector<std::unique_ptr<CDpiHelper>> s_helpers; // in general it will be less than 16, so vector is good

		// sets current helper and returns previous
		static CDpiHelper* HDPIAPI SetCurrentForWindow(HWND hWnd = nullptr, bool findHandler = true);
		static CDpiHelper* HDPIAPI SetCurrentForDPI(UINT dpi);
		static CDpiHelper* HDPIAPI SetCurrentForMonitor(HMONITOR hMon);
		static CDpiHelper* HDPIAPI SetCurrent(CDpiHelper* helper);
		static CDpiHelper* HDPIAPI GetCurrent();
		static bool HDPIAPI IsCurrent(const CDpiHelper* helper);

        static void Initialize();

        static bool m_fInitialized;
        static int m_DeviceDpiX;
        static int m_DeviceDpiY;
    };

	class CDpiHelperScope
	{
		CDpiHelper* m_prev;
		CDpiHelper* m_curr;
        CDpiHelperScope* m_prev_scope;

	public:
		CDpiHelperScope(HDC hDC);
		CDpiHelperScope(CDpiHelper* helper);
		CDpiHelperScope(HWND hWnd, bool findHandler = true);
		CDpiHelperScope(UINT dpi);
		CDpiHelperScope(HMONITOR hMon);
		~CDpiHelperScope();

        static CDpiHelperScope* GetActive();

		CDpiHelper* GetHelper() const
		{
			return m_curr;
		}

		CDpiHelper* GetPreviousHelper() const
		{
			return m_prev;
		}
	};
} // namespace
