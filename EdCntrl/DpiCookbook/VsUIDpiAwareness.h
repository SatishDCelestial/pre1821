#pragma once

#include "atlpath.h"
#include "atlstr.h"

// Requires Windows 10 v1803 or higher to compile
#include "ShellScalingApi.h"
#include "WinDef.h"
#include "WinUser.h"

#include <functional>

#ifdef VISUAL_ASSIST_X
#include "Settings.h"
#endif

#pragma region Registry path defines

// Defines for the .NET setup registry key used to determine if the current version has the required
// functionality and behaviors needed for Per-Monitor DPI awareness to be enabled.

#ifdef VISUAL_ASSIST_X
#define DOTNET_SETUP_KEYNAME "SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full"
#define DOTNET_SETUP_VERSION "Version"
#define DOTNET_REQUIRED_VERSION "4.8"
#else
#define DOTNET_SETUP_KEYNAME _T("SOFTWARE\\Microsoft\\NET Framework Setup\\NDP\\v4\\Full")
#define DOTNET_SETUP_VERSION _T("Version")
#define DOTNET_REQUIRED_VERSION _T("4.8")
#endif

#pragma endregion Registry path defines

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef WM_GETDPISCALEDSIZE
#define WM_DPICHANGED_BEFOREPARENT	0x02E2
#define WM_DPICHANGED_AFTERPARENT	0x02E3
#define WM_GETDPISCALEDSIZE			0x02E4
#endif

namespace VsUI
{
    // This class contains multi-/mixed-DPI mode helpers for setting the DPI context of the process
    // or the calling thread.
    class CDpiAwareness
    {
	    friend class DpiHelper;

		// user32.dll
		using GetDpiForWindowProc = UINT(__stdcall*)(HWND hwnd);
		using GetWindowDpiAwarenessContextProc = DPI_AWARENESS_CONTEXT(__stdcall*)(HWND hwnd);
		using SetProcessDpiAwarenessContextProc = BOOL(__stdcall*)(DPI_AWARENESS_CONTEXT value);
		using SetThreadDpiAwarenessContextProc = DPI_AWARENESS_CONTEXT(__stdcall*)(DPI_AWARENESS_CONTEXT dpiContext);
		using SetThreadDpiHostingBehaviorProc = DPI_HOSTING_BEHAVIOR(__stdcall*)(DPI_HOSTING_BEHAVIOR value);
		using GetThreadDpiAwarenessContextProc = DPI_AWARENESS_CONTEXT(__stdcall*)();
		using AreDpiAwarenessContextsEqualProc = BOOL(__stdcall*)(DPI_AWARENESS_CONTEXT dpiContextA, DPI_AWARENESS_CONTEXT dpiContextB);
		using GetSystemMetricsForDpiProc = int (__stdcall*)(int  nIndex, UINT dpi);
		using GetDpiFromDpiAwarenessContextProc = UINT(__stdcall*)(DPI_AWARENESS_CONTEXT dpiContext);
		using GetDpiForSystemProc = UINT(__stdcall*)();

		// shcore.dll
		using GetDpiForMonitorProc = HRESULT(__stdcall*)(HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY);
		using GetProcessDpiAwarenessProc = HRESULT(__stdcall*)(HANDLE hprocess, PROCESS_DPI_AWARENESS* value);
		using SetProcessDpiAwarenessProc = HRESULT(__stdcall*)(PROCESS_DPI_AWARENESS value);

	public:
        // Checks whether the appid can turn on Per-Monitor DPI awareness for the process.
        static bool IsPerMonitorDPIAwarenessAvailable()
        {
            if (!s_isAvailableChecked)
            {
                Initialize();

                s_isAvailable        = IsWindowsSupportedVersion() && IsDotNetSupportedVersion();
                s_isAvailableChecked = true;
            }

            return s_isAvailable;
        }

        // Checks whether Per-Monitor DPI awareness is enabled for the process.
        static bool IsPerMonitorDPIAwarenessEnabled()
        {
            if (!s_isPerMonitorAwarenessChecked)
            {
                Initialize();

                PROCESS_DPI_AWARENESS processAwareness;

                // If NULL is passed to GetProcessDpiAwareness, the current process is used
                if (s_pGetPDAC_Win81 && (s_pGetPDAC_Win81(NULL, &processAwareness) == S_OK))
                    s_isPerMonitorAwarenessEnabled = (processAwareness == PROCESS_DPI_AWARENESS::PROCESS_PER_MONITOR_DPI_AWARE);

                s_isPerMonitorAwarenessChecked = true;
            }

            return IsPerMonitorDPIAwarenessAvailable() && s_isPerMonitorAwarenessEnabled;
        }

		static bool IsDefaultDpi(UINT dpiX, UINT dpiY)
		{
			Initialize();
			return dpiX == (UINT)s_DeviceDpiX && dpiY == (UINT)s_DeviceDpiY;
		}

        #pragma region Get DPI methods
		static HRESULT GetDeviceDpi(_Out_ UINT* pDpiX, _Out_ UINT* pDpiY)
		{
			Initialize();

			if ((pDpiX == nullptr) ||
				(pDpiY == nullptr))
				return E_POINTER;

			*pDpiX = (UINT)s_DeviceDpiX;
			*pDpiY = (UINT)s_DeviceDpiY;

			return S_OK;
		}

        // Gets the effective DPI for the given monitor. The DPI for a monitor is returned as both
        // vertical and horizontal values as out parameters.
        static HRESULT GetDpiForMonitor(_In_ HMONITOR hMonitor, _Out_ UINT *pDpiX, _Out_ UINT *pDpiY)
        {
            if (hMonitor == nullptr)
                return E_INVALIDARG;

            if ((pDpiX == nullptr) ||
                (pDpiY == nullptr))
                return E_POINTER;

            if (!CanGetMonitorDpi())
            {
                *pDpiX = (UINT)s_DeviceDpiX;
                *pDpiY = (UINT)s_DeviceDpiY;
                return S_OK;
            }

            return s_pGetDFM(hMonitor, MONITOR_DPI_TYPE::MDT_EFFECTIVE_DPI, pDpiX, pDpiY);
        }

#ifdef _MFC_VER
		static HRESULT GetDPIFromThreadStateMessage(_In_ HWND hwnd, _Out_ UINT* pDpiX, _Out_ UINT* pDpiY)
		{
			if (hwnd == nullptr)
				return E_INVALIDARG;

			if ((pDpiX == nullptr) ||
				(pDpiY == nullptr))
				return E_POINTER;

			const auto* threadData = _afxThreadState.GetData();
			if (threadData)
			{
				for (int i = 0; i < 2; i++)
				{
					const MSG* msg = i ? &_afxThreadState.GetData()->m_msgCur : &_afxThreadState.GetData()->m_lastSentMsg;
					if (msg && msg->hwnd == hwnd)
					{
						// for those two messages, window still has old DPI
						// so if we call GetDpiForWindow, we would get previous,
						// not the one we are going to switch to

						if (msg->message == WM_DPICHANGED)
						{
							*pDpiX = (UINT)LOWORD(msg->wParam);
							*pDpiY = (UINT)HIWORD(msg->wParam);
							return S_OK;
						}
						else if (msg->message == WM_GETDPISCALEDSIZE)
						{
							*pDpiX = *pDpiY = (UINT)msg->wParam;
							return S_OK;
						}
					}
				}
			}

			return E_FAIL;
		}
#endif

        // Gets the DPI for the given HWND. The DPI for a window is returned as both vertical and
        // horizontal values as out parameters.
        static HRESULT GetDpiForWindow(_In_ HWND hwnd, _Out_ UINT *pDpiX, _Out_ UINT *pDpiY 
#ifdef _MFC_VER			
			, bool fromThreadMessage = true
#endif		
		)
        {
            if (hwnd == nullptr)
                return E_INVALIDARG;

            if ((pDpiX == nullptr) ||
                (pDpiY == nullptr))
                return E_POINTER;

            if (!CanGetWindowDpi())
            {
                *pDpiX = (UINT)s_DeviceDpiX;
                *pDpiY = (UINT)s_DeviceDpiY;
                return S_OK;
            }
#ifdef _MFC_VER	
			if (fromThreadMessage)
			{
				auto hr = GetDPIFromThreadStateMessage(hwnd, pDpiX, pDpiY);
				if (SUCCEEDED(hr))
					return hr;
			}
#endif	
            UINT dpi = s_pGetDFW(hwnd);
            if (dpi == 0)
                return HRESULT_FROM_WIN32(GetLastError());

            *pDpiX = dpi;
            *pDpiY = dpi;
            return S_OK;
        }

		// Gets the DPI for System. 
		static UINT GetDpiForSystem()
		{
			if (CanGetSystemDpi())
			{
				UINT dpi = s_pGetDFS();
				if (dpi)
					return dpi;
			}

			HDC hdc = ::GetDC(NULL);
			if (hdc)
			{
				UINT dpi = (UINT)::GetDeviceCaps(hdc, LOGPIXELSX);
				::ReleaseDC(NULL, hdc);
				return dpi;
			}

			return k_DefaultLogicalDpi;
		}

		static UINT GetDpiForWindow(_In_ HWND hwnd
#ifdef _MFC_VER				
			, bool fromThreadMessage = true
#endif			
		)
		{
			if (hwnd == nullptr)
				return k_DefaultLogicalDpi;

			if (!CanGetWindowDpi())
				return (UINT)s_DeviceDpiX;
#ifdef _MFC_VER		
			if (fromThreadMessage)
			{
				UINT dpiX, dpiY;
				if (SUCCEEDED(GetDPIFromThreadStateMessage(hwnd, &dpiX, &dpiY)))
					return dpiX;
			}
#endif	
			UINT dpi = s_pGetDFW(hwnd);
			if (dpi == 0)
				return k_DefaultLogicalDpi;

			return dpi;
		}

		static UINT GetDpiForMonitor(_In_ HMONITOR hMonitor)
		{
			UINT dpiX, dpiY;
			if (SUCCEEDED(GetDpiForMonitor(hMonitor, &dpiX, &dpiY)))
				return dpiX;

			return k_DefaultLogicalDpi;
		}

		static UINT GetDpiForScreenPoint(const POINT & screenPoint, DWORD defaultMonitor = MONITOR_DEFAULTTONEAREST)
		{
			HMONITOR hMonitor = ::MonitorFromPoint(screenPoint, defaultMonitor);
			if (hMonitor)
			{
				UINT dpiX, dpiY;
				if (SUCCEEDED(GetDpiForMonitor(hMonitor, &dpiX, &dpiY)))
					return dpiX;
			}

			return k_DefaultLogicalDpi;
		}

		static UINT GetDpiForScreenRect(const RECT& screenRect, DWORD defaultMonitor = MONITOR_DEFAULTTONEAREST)
		{
			HMONITOR hMonitor = ::MonitorFromRect(&screenRect, defaultMonitor);
			if (hMonitor)
			{
				UINT dpiX, dpiY;
				if (SUCCEEDED(GetDpiForMonitor(hMonitor, &dpiX, &dpiY)))
					return dpiX;
			}

			return k_DefaultLogicalDpi;
		}

		static UINT GetDpiForThread()
		{
			auto ctx = GetThreadDpiAwarenessContext();

			if (ctx && s_pGetDFDAC)
				return s_pGetDFDAC(ctx);

			return k_DefaultLogicalDpi;
		}

		static int GetSystemMetricsForDPI(int nIndex, UINT dpi)
		{
			if (s_pGetSMFD)
			{
				if (dpi)
					return s_pGetSMFD(nIndex, dpi);
	
				return s_pGetSMFD(nIndex, GetDpiForThread());
			}

			return ::GetSystemMetrics(nIndex);
		}

		static int GetSystemMetricsForThread(int nIndex)
		{
			if (IsPerMonitorDPIAwarenessEnabled() && s_pGetSMFD)
				return s_pGetSMFD(nIndex, GetDpiForThread());
				
			return ::GetSystemMetrics(nIndex);
		}

        #pragma endregion Get DPI methods

        #pragma region Device to logical conversion methods

        // Converts a POINT from device units to logical units.
        static HRESULT DeviceToLogicalPoint(_In_ HWND hwnd, _Inout_ POINT *pPoint)
        {
            return ConvertPoint(hwnd, ConversionDirection::DeviceToLogical, pPoint);
        }

        // Converts a RECT from device units to logical units.
        static HRESULT DeviceToLogicalRect(_In_ HWND hwnd, _Inout_ RECT *pRect)
        {
            return ConvertRect(hwnd, ConversionDirection::DeviceToLogical, pRect);
        }

        // Converts a SIZE from device units to logical units.
        static HRESULT DeviceToLogicalSize(_In_ HWND hwnd, _Inout_ SIZE *pSize)
        {
            return ConvertSize(hwnd, ConversionDirection::DeviceToLogical, pSize);
        }

        // Converts an x-coordinate (or horizontal) value from device units to logical units.
        static HRESULT DeviceToLogicalUnitsX(_In_ HWND hwnd, _Inout_ int *pValue)
        {
            return ConvertUnits(hwnd, ConversionDirection::DeviceToLogical, Orientation::Horizontal, pValue);
        }

        // Converts a y-coordinate (or vertical) value from device units to logical units.
        static HRESULT DeviceToLogicalUnitsY(_In_ HWND hwnd, _Inout_ int *pValue)
        {
            return ConvertUnits(hwnd, ConversionDirection::DeviceToLogical, Orientation::Vertical, pValue);
        }

        #pragma endregion Device to logical conversion methods

        #pragma region Logical to device conversion methods

        // Converts a POINT from logical units to device units.
        static HRESULT LogicalToDevicePoint(_In_ HWND hwnd, _Inout_ POINT *pPoint)
        {
            return ConvertPoint(hwnd, ConversionDirection::LogicalToDevice, pPoint);
        }

        // Converts a RECT from logical units to device units.
        static HRESULT LogicalToDeviceRect(_In_ HWND hwnd, _Inout_ RECT *pRect)
        {
            return ConvertRect(hwnd, ConversionDirection::LogicalToDevice, pRect);
        }

        // Converts a SIZE from logical units to device units.
        static HRESULT LogicalToDeviceSize(_In_ HWND hwnd, _Inout_ SIZE *pSize)
        {
            return ConvertSize(hwnd, ConversionDirection::LogicalToDevice, pSize);
        }

        // Converts an x-coordinate (or horizontal) value from logical units to device units.
        static HRESULT LogicalToDeviceUnitsX(_In_ HWND hwnd, _Inout_ int *pValue)
        {
            return ConvertUnits(hwnd, ConversionDirection::LogicalToDevice, Orientation::Horizontal, pValue);
        }

        // Converts a y-coordinate (or vertical) value from logical units to device units.
        static HRESULT LogicalToDeviceUnitsY(_In_ HWND hwnd, _Inout_ int *pValue)
        {
            return ConvertUnits(hwnd, ConversionDirection::LogicalToDevice, Orientation::Vertical, pValue);
        }

        #pragma endregion Logical to device conversion methods

        #pragma region DPI awareness methods

        // Gets the DPI awareness context from the given HWND. This method should not be called
        // directly and the CDpiScope class should be used to handle switching the thread DPI
        // context to match the given HWND and to make sure the thread's DPI context is returned to
        // its original state upon leaving the DPI scope.
        //
        // This method will return NULL if GetWindowDpiAwarenessContext is not supported in the
        // current environment, or it will return the DPI context associated with the HWND.
        static DPI_AWARENESS_CONTEXT GetWindowDpiAwarenessContext(_In_ HWND hwnd)
        {
            // If GetWindowDpiAwarenessContext is not supported, mimic the
            // SetThreadDpiAwarenessContext method's error handling and return NULL.
            if (!CanGetWindowDpiContext())
                return NULL;

            return s_pGetWDAC(hwnd);
        }

        // The process DPI context can only be set once and only before the first HWND is created.
        // The appid will do the calculation to determine when and how to call this. All other calls
        // to this method after the process DPI context has been set will fail.
        //
        // This method will return false if SetProcessDpiAwarenessContext is not supported in the
        // current environment, when trying to set Per-Monitor V2 awareness mode when it's not
        // supported, or if it has already been called before.
        static bool SetProcessDpiAwarenessContext(_In_ DPI_AWARENESS_CONTEXT dpiContext)
        {
            if (!CanSetProcessDpiContext())
                return false;

            if (s_pSetPDAC)
            {
                return s_pSetPDAC(dpiContext);
            }
            else
            {
                PROCESS_DPI_AWARENESS dpiContext_Win81 = PROCESS_DPI_AWARENESS::PROCESS_DPI_UNAWARE;

                // Don't convert the PMA values since they aren't supported on Win8.1
                if (dpiContext == DPI_AWARENESS_CONTEXT_SYSTEM_AWARE)
                    dpiContext_Win81 = PROCESS_DPI_AWARENESS::PROCESS_SYSTEM_DPI_AWARE;

                return (s_pSetPDAC_Win81(dpiContext_Win81) == S_OK);
            }
        }

        // The DPI host behavior determines if a piece of parent UI with one DPI context can host a
        // piece of child UI with a different DPI context.
        //
        // This method will return false if SetThreadDpiHostingBehavior is not supported in the
        // current environment, or if the requested hosting behavior is unsupported/invalid.
        static bool SetProcessDpiHostingMode(DPI_HOSTING_BEHAVIOR dpiHostingBehavior)
        {
            if (!CanSetThreadHosting())
                return false;

            return s_pSetTDHB(dpiHostingBehavior) != DPI_HOSTING_BEHAVIOR::DPI_HOSTING_BEHAVIOR_INVALID;
        }

        // The thread DPI context is normally changed by Windows prior to sending te process messages
        // via the message loop. If however some non-message loop code path needs to interact with
        // HWNDs or coordinates that are in different DPI contexts, then this method can be used to
        // change the thread context. This method should not be called directly, and the CDpiScope
        // class should be used to make sure the thread's DPI context is returned to its original
        // state upon leaving the DPI scope.
        //
        // This method will return NULL if SetThreadDpiAwarenessContext is not supported in the
        // current environment, or it will return the previous DPI context.
        static DPI_AWARENESS_CONTEXT SetThreadDpiAwarenessContext(_In_ DPI_AWARENESS_CONTEXT dpiContext)
        {
            // If an invalid context is given to SetThreadDpiAwarenessContext the method returns
            // NULL, so mimic that behavior when that call is unsupported.
            if (!CanSetThreadDpiContext())
                return NULL;

            return s_pSetTDAC(dpiContext);
        }

		static DPI_AWARENESS_CONTEXT GetThreadDpiAwarenessContext()
		{
			if (CanGetThreadDpiContext())
				return s_pGetTDAC();

			return nullptr;
		}

		static bool CanGetThreadDpiContext()
		{
			Initialize();
			return s_pGetTDAC != nullptr; 
		}

		static bool CanGetThreadDpiContextsEquality()
		{
			Initialize();
			return s_pAreDACE != nullptr;
		}

		static bool AreDpiContextsEqual(_In_ DPI_AWARENESS_CONTEXT dpiContextA, _In_ DPI_AWARENESS_CONTEXT dpiContextB)
		{
			if (CanGetThreadDpiContextsEquality())
				return s_pAreDACE(dpiContextA, dpiContextB);

			return false;
		}

		static void AssertUnexpectedDpiContext(_In_ DPI_AWARENESS_CONTEXT expected)
		{
            (void) expected;
#ifdef _DEBUG
// 			if (!IsPerMonitorDPIAwarenessEnabled())
// 				return;
// 
// 			if (expected &&
// 				CanGetThreadDpiContext() &&
// 				CanGetThreadDpiContextsEquality())
// 			{
// 				auto ctx = GetThreadDpiAwarenessContext();
// 				auto areEqual = AreDpiContextsEqual(ctx, expected);
// 				_ASSERTE("Not in expected DPI awareness context!" && areEqual);
// 			}
#endif // _DEBUG
		}

        #pragma endregion DPI awareness methods

        #pragma region DPI API availability checker methods

        // Used for checking if an HMONITOR's DPI can be queried.
        //
        // This is expected to return true for Windows versions >= 8.1 and false for all others.
        static bool CanGetMonitorDpi()
        {
            Initialize();
            return (s_pGetDFM != nullptr);
        }

        // Used for checking if an HWND's DPI can be queried.
        //
        // This is expected to return true for Windows versions >= 10.1607 and false for all others.
        static bool CanGetWindowDpi()
        {
            Initialize();
            return (s_pGetDFW != nullptr);
        }

		// Used for checking if an system's DPI can be queried.
		//
		// This is expected to return true for Windows versions >= 10.1607 and false for all others.
		static bool CanGetSystemDpi()
		{
			Initialize();
			return (s_pGetDFS != nullptr);
		}

        // Used for checking if the window DPI context can be queried.
        //
        // This is expected to return true for Windows versions >= 10.1607 and false for all others.
        static bool CanGetWindowDpiContext()
        {
            Initialize();
            return (s_pGetWDAC != nullptr);
        }

        // Used for checking if the process DPI context can be changed.
        //
        // This is expected to return true for Windows versions >= 8.1 and false for all others.
        static bool CanSetProcessDpiContext()
        {
            Initialize();
            return (s_pSetPDAC != nullptr) ||
                   (s_pSetPDAC_Win81 != nullptr);
        }

        // Used for checking if the thread DPI context can be changed.
        //
        // This is expected to return true for Windows versions >= 10.1607 and false for all others.
        static bool CanSetThreadDpiContext()
        {
            Initialize();
            return (s_pSetTDAC != nullptr);
        }

        // Used for checking if the thread DPI hosting behavior can be changed.
        //
        // This is expected to return true for Windows versions >= 10.1803 and false for all others.
        static bool CanSetThreadHosting()
        {
            Initialize();
            return (s_pSetTDHB != nullptr);
        }

        #pragma endregion DPI API availability checker methods

    private:
        // Initializes the backing state.
        static void Initialize()
        {
            if (s_isInitialized)
                return;

			// Get the required method addresses from User32.dll.
		    HMODULE hModUser32 = GetModuleHandleW(GetSystemPath(L"user32.dll"));
			if (hModUser32)
			{
			    s_pGetDFW = (GetDpiForWindowProc)(intptr_t)GetProcAddress(hModUser32, "GetDpiForWindow");
			    s_pGetWDAC = (GetWindowDpiAwarenessContextProc)(intptr_t)GetProcAddress(hModUser32, "GetWindowDpiAwarenessContext");
			    s_pSetPDAC = (SetProcessDpiAwarenessContextProc)(intptr_t)GetProcAddress(hModUser32, "SetProcessDpiAwarenessContext");
			    s_pSetTDAC = (SetThreadDpiAwarenessContextProc)(intptr_t)GetProcAddress(hModUser32, "SetThreadDpiAwarenessContext");
			    s_pSetTDHB = (SetThreadDpiHostingBehaviorProc)(intptr_t)GetProcAddress(hModUser32, "SetThreadDpiHostingBehavior");
			    s_pGetTDAC = (GetThreadDpiAwarenessContextProc)(intptr_t)GetProcAddress(hModUser32, "GetThreadDpiAwarenessContext");
			    s_pAreDACE = (AreDpiAwarenessContextsEqualProc)(intptr_t)GetProcAddress(hModUser32, "AreDpiAwarenessContextsEqual");
			    s_pGetSMFD = (GetSystemMetricsForDpiProc)(intptr_t)GetProcAddress(hModUser32, "GetSystemMetricsForDpi");
			    s_pGetDFDAC = (GetDpiFromDpiAwarenessContextProc)(intptr_t)GetProcAddress(hModUser32, "GetDpiFromDpiAwarenessContext");
			    s_pGetDFS = (GetDpiForSystemProc)(intptr_t)GetProcAddress(hModUser32, "GetDpiForSystem");
			}

			// Get the required methods addresses from ShCore.dll.
		    HMODULE hModShCore = GetModuleHandleW(GetSystemPath(L"shcore.dll"));
			if (hModShCore)
			{
			    s_pGetDFM = (GetDpiForMonitorProc)(intptr_t)GetProcAddress(hModShCore, "GetDpiForMonitor");
			    s_pGetPDAC_Win81 = (GetProcessDpiAwarenessProc)(intptr_t)GetProcAddress(hModShCore, "GetProcessDpiAwareness");
			    s_pSetPDAC_Win81 = (SetProcessDpiAwarenessProc)(intptr_t)GetProcAddress(hModShCore, "SetProcessDpiAwareness");
			}

			class FMW
			{
				struct handle_data {
					unsigned long process_id;
					HWND best_handle;
				};

				static BOOL CALLBACK enum_windows_callback(HWND handle, LPARAM lParam)
				{
					handle_data& data = *(handle_data*)lParam;
					unsigned long process_id = 0;
					GetWindowThreadProcessId(handle, &process_id);
					if (data.process_id != process_id || !is_main_window(handle)) {
						return TRUE;
					}
					data.best_handle = handle;
					return FALSE;
				}

				static BOOL is_main_window(HWND handle)
				{
					return GetWindow(handle, GW_OWNER) == (HWND)0 && IsWindowVisible(handle);
				}

			public:
				HWND Handle = nullptr;

				FMW(unsigned long process_id = GetCurrentProcessId())
				{
					handle_data data;
					data.process_id = process_id;
					data.best_handle = nullptr;
					EnumWindows(enum_windows_callback, (LPARAM)& data);
					Handle = data.best_handle;
				}
			};

			// Set up the screen DPI values from main window, DC holds the DPI values of the screen.
			// We could actually use GetDpiForSystem because we get initialized in VaApp::Start 
			// but with following we are safe to get the DPI used by DCs across the process anytime. 
			// According to MSDN, GetDpiForSystem value should not be cached. 
			// It is reasonable as it may get changed by user, see case: 142324. 
			HWND hWnd = s_pGetPDAC_Win81 ? FMW().Handle : nullptr;
			HDC hdc = ::GetDC(hWnd);
			if (hdc)
			{
				s_DeviceDpiX = ::GetDeviceCaps(hdc, LOGPIXELSX);
				s_DeviceDpiY = ::GetDeviceCaps(hdc, LOGPIXELSY);
				::ReleaseDC(hWnd, hdc);
			}

            s_isInitialized = true;
        }

        #pragma region Private enums

        enum class ConversionDirection
        {
            DeviceToLogical,
            LogicalToDevice,
        };

        enum class Orientation
        {
            Horizontal,
            Vertical,
        };

        #pragma endregion Private enums

        #pragma region Private helper methods

        typedef CStrBufT<wchar_t, false> CAtlStrBufW;
        typedef CPathT<CAtlStringW> CAtlPathW;

        // Prepends the System32 Windows path to the given DLL name.
        static CStringW GetSystemPath(_In_ LPCWSTR dllName)
        {
            CAtlPathW fullPath;

            GetSystemDirectoryW(CAtlStrBufW(fullPath, MAX_PATH), MAX_PATH);

            fullPath.Append(dllName);
            return fullPath.m_strPath;
        }

        #pragma region Unit conversion helpers

        // Converts the given POINT between device and logical units based on the DPI of the given
        // HWND.
        static HRESULT ConvertPoint(_In_ HWND hwnd, ConversionDirection conversion, _Inout_ POINT *pPoint)
        {
            if (pPoint == nullptr)
                return E_POINTER;

            HRESULT hr = S_OK;

            UINT dpiX, dpiY;
            if (SUCCEEDED(hr = GetDpiForWindow(hwnd, &dpiX, &dpiY)))
            {
                pPoint->x = ScaleValue(pPoint->x, dpiX, conversion);
                pPoint->y = ScaleValue(pPoint->y, dpiY, conversion);
            }

            return hr;
        }

        // Converts the given RECT between device and logical units based on the DPI of the given
        // HWND.
        static HRESULT ConvertRect(_In_ HWND hwnd, ConversionDirection conversion, _Inout_ RECT *pRect)
        {
            if (pRect == nullptr)
                return E_POINTER;

            HRESULT hr = S_OK;

            UINT dpiX, dpiY;
            if (SUCCEEDED(hr = GetDpiForWindow(hwnd, &dpiX, &dpiY)))
            {
                pRect->left   = ScaleValue(pRect->left,   dpiX, conversion);
                pRect->top    = ScaleValue(pRect->top,    dpiY, conversion);
                pRect->right  = ScaleValue(pRect->right,  dpiX, conversion);
                pRect->bottom = ScaleValue(pRect->bottom, dpiY, conversion);
            }

            return hr;
        }

        // Converts the given SIZE between device and logical units based on the DPI of the given
        // HWND.
        static HRESULT ConvertSize(_In_ HWND hwnd, ConversionDirection conversion, _Inout_ SIZE *pSize)
        {
            if (pSize == nullptr)
                return E_POINTER;

            HRESULT hr = S_OK;

            UINT dpiX, dpiY;
            if (SUCCEEDED(hr = GetDpiForWindow(hwnd, &dpiX, &dpiY)))
            {
                pSize->cx = ScaleValue(pSize->cx, dpiX, conversion);
                pSize->cy = ScaleValue(pSize->cy, dpiY, conversion);
            }

            return hr;
        }

        // Converts the given value between device and logical units based on the DPI of the given
        // HWND.
        static HRESULT ConvertUnits(_In_ HWND hwnd, ConversionDirection conversion, Orientation orientation, _Inout_ int *pValue)
        {
            if (pValue == nullptr)
                return E_POINTER;

            HRESULT hr = S_OK;

            UINT dpiX, dpiY;
            if (SUCCEEDED(hr = GetDpiForWindow(hwnd, &dpiX, &dpiY)))
            {
                UINT dpi;
                if (orientation == Orientation::Horizontal)
                    dpi = dpiX;
                else
                    dpi = dpiY;

                *pValue = ScaleValue(*pValue, dpi, conversion);
            }

            return hr;
        }

        // This method implicitly casts both input values to doubles. (Normally they'd be an int
        // and UINT respectively.) This is done to prevent truncating the intermediate value that
        // results from the multiplication operation if it would normally exceed the max value of
        // an int.
        static int ScaleValue(double originalValue, double dpi, ConversionDirection conversion)
        {
            if (conversion == ConversionDirection::DeviceToLogical)
                return Round((originalValue * k_DefaultLogicalDpi) / dpi);
            else
                return Round((originalValue * dpi) / k_DefaultLogicalDpi);
        }

        static int Round(double value)
        {
            if (value >= 0)
                return (int)(value + 0.5);
            else
                return (int)(value - 0.5);
        }

        #pragma endregion Unit conversion helpers

        #pragma endregion Private helper methods

        #pragma region Per-Monitor DPI availability checker methods

        // The version of .NET that offers the required multi-/mixed-DPI functionality and behaviors
        // sets a regkey when present. So check for that instead of checking for dll version numbers
        // or the presence of certain APIs.
        static bool IsDotNetSupportedVersion()
        {
            CRegKey dotNetSetupKey;

            if (dotNetSetupKey.Open(HKEY_LOCAL_MACHINE, DOTNET_SETUP_KEYNAME, KEY_READ) == ERROR_SUCCESS)
            {
                CString versionValue;
                ULONG charsRead = MAX_PATH;

                if (dotNetSetupKey.QueryStringValue(DOTNET_SETUP_VERSION, CStrBuf(versionValue, MAX_PATH), &charsRead) == ERROR_SUCCESS)
				{
					return versionValue.Find(DOTNET_REQUIRED_VERSION) == 0;
				}

                dotNetSetupKey.Close();
            }

            return false;
        }

        // Windows has had incremental rollout of all of the required multi-/mixed-DPI APIs needed
        // to fully support Per-Monitor DPI awareness and enable mixed DPI hosting behavior. This
        // will check for the oldest version of Windows that has all the required APIs and behaviors.
        static bool IsWindowsSupportedVersion()
        {
            // Currently being able to set the DPI hosting behavior is the
            // low bar for enabling Per-Monitor DPI awareness mode.
            return CanSetThreadHosting();
        }

        #pragma endregion Per-Monitor DPI availability checker methods

        #pragma region Statics declarations

        static const UINT k_DefaultLogicalDpi = 96;

        static int  s_DeviceDpiX;
        static int  s_DeviceDpiY;
        static bool s_isInitialized;
        static bool s_isAvailable;
        static bool s_isAvailableChecked;
        static bool s_isPerMonitorAwarenessChecked;
        static bool s_isPerMonitorAwarenessEnabled;
        static GetDpiForMonitorProc              s_pGetDFM;
        static GetDpiForWindowProc               s_pGetDFW;
		static GetDpiForSystemProc				 s_pGetDFS;
		static GetProcessDpiAwarenessProc        s_pGetPDAC_Win81;
        static GetWindowDpiAwarenessContextProc  s_pGetWDAC;
        static SetProcessDpiAwarenessContextProc s_pSetPDAC;
        static SetProcessDpiAwarenessProc        s_pSetPDAC_Win81;
        static SetThreadDpiAwarenessContextProc  s_pSetTDAC;
        static SetThreadDpiHostingBehaviorProc   s_pSetTDHB;
		static GetThreadDpiAwarenessContextProc  s_pGetTDAC;
		static AreDpiAwarenessContextsEqualProc  s_pAreDACE;
		static GetSystemMetricsForDpiProc		 s_pGetSMFD;
		static GetDpiFromDpiAwarenessContextProc s_pGetDFDAC;

        #pragma endregion Statics declarations
    };

    #pragma region CDpiScope

    // This class is used for changing the calling thread's DPI context for the duration of the
    // lifetime of this scope object. A DPI scope should not be created and stored indefinitely.
    // Proper use of a DPI scope limits its lifetime to just the code that needs the different
    // DPI context.
    class CDpiScope
    {
    public:
        // use this ctor to set user defined context awareness or system awareness
        CDpiScope(
#ifdef VISUAL_ASSIST_X
			bool fromSettings = false
#endif		
		)
		{
			oldDpi = NULL;

			// If the process is not in Per-Monitor awareness mode, then don't allow switching the
			// thread's DPI context.
			if (CDpiAwareness::IsPerMonitorDPIAwarenessEnabled())
			{
				DPI_AWARENESS_CONTEXT ctx = DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;

#ifdef VISUAL_ASSIST_X
				if (fromSettings && Psettings)
				{
					switch (Psettings->mDPIAwareness)
					{
					case DPI_AWARENESS_SYSTEM_AWARE:
						ctx = DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
						break;

					case DPI_AWARENESS_PER_MONITOR_AWARE:
						ctx = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2;
						break;

					default:
						ctx = nullptr;
					}		
				}
#endif

				if (ctx)
					oldDpi = CDpiAwareness::SetThreadDpiAwarenessContext(ctx);
			}
        }

        CDpiScope(DPI_AWARENESS_CONTEXT dpiContext)
        {
            // If the process is not in Per-Monitor awareness mode, then don't allow switching the
            // thread's DPI context.
            if (CDpiAwareness::IsPerMonitorDPIAwarenessEnabled() && dpiContext)
                oldDpi = CDpiAwareness::SetThreadDpiAwarenessContext(dpiContext);
            else
                oldDpi = NULL;
        }

        CDpiScope(HWND hwnd)
        {
            // If the process is not in Per-Monitor awareness mode, then don't allow switching the
            // thread's DPI context.
            if (CDpiAwareness::IsPerMonitorDPIAwarenessEnabled() && IsWindow(hwnd))
            {
                // Get the message HWND's DPI context and temporarily set the thread context to that
                // DPI awareness so that coordinate lookup and translation works correctly.
                DPI_AWARENESS_CONTEXT sourceDpi = CDpiAwareness::GetWindowDpiAwarenessContext(hwnd);
                oldDpi = CDpiAwareness::SetThreadDpiAwarenessContext(sourceDpi);
            }
            else
            {
                oldDpi = NULL;
            }
        }

        ~CDpiScope()
        {
            // Reset the thread DPI awareness to its original value.
            if (oldDpi != NULL)
                CDpiAwareness::SetThreadDpiAwarenessContext(oldDpi);
        }

    private:
        DPI_AWARENESS_CONTEXT oldDpi;
    };

    #pragma endregion CDpiScope

    #pragma region CDpiAwareness declspec statics

	__declspec(selectany) int  CDpiAwareness::s_DeviceDpiX = CDpiAwareness::k_DefaultLogicalDpi;
	__declspec(selectany) int  CDpiAwareness::s_DeviceDpiY = CDpiAwareness::k_DefaultLogicalDpi;
	__declspec(selectany) bool CDpiAwareness::s_isInitialized = false;
	__declspec(selectany) bool CDpiAwareness::s_isAvailable = false;
	__declspec(selectany) bool CDpiAwareness::s_isAvailableChecked = false;
	__declspec(selectany) bool CDpiAwareness::s_isPerMonitorAwarenessChecked = false;
	__declspec(selectany) bool CDpiAwareness::s_isPerMonitorAwarenessEnabled = false;

	__declspec(selectany) CDpiAwareness::GetDpiForMonitorProc              CDpiAwareness::s_pGetDFM = nullptr;
	__declspec(selectany) CDpiAwareness::GetDpiForWindowProc               CDpiAwareness::s_pGetDFW = nullptr;
	__declspec(selectany) CDpiAwareness::GetProcessDpiAwarenessProc        CDpiAwareness::s_pGetPDAC_Win81 = nullptr;
	__declspec(selectany) CDpiAwareness::GetWindowDpiAwarenessContextProc  CDpiAwareness::s_pGetWDAC = nullptr;
	__declspec(selectany) CDpiAwareness::SetProcessDpiAwarenessContextProc CDpiAwareness::s_pSetPDAC = nullptr;
	__declspec(selectany) CDpiAwareness::SetProcessDpiAwarenessProc        CDpiAwareness::s_pSetPDAC_Win81 = nullptr;
	__declspec(selectany) CDpiAwareness::SetThreadDpiAwarenessContextProc  CDpiAwareness::s_pSetTDAC = nullptr;
	__declspec(selectany) CDpiAwareness::SetThreadDpiHostingBehaviorProc   CDpiAwareness::s_pSetTDHB = nullptr;
	__declspec(selectany) CDpiAwareness::GetThreadDpiAwarenessContextProc  CDpiAwareness::s_pGetTDAC = nullptr;
	__declspec(selectany) CDpiAwareness::AreDpiAwarenessContextsEqualProc  CDpiAwareness::s_pAreDACE = nullptr;
	__declspec(selectany) CDpiAwareness::GetSystemMetricsForDpiProc		   CDpiAwareness::s_pGetSMFD = nullptr;
	__declspec(selectany) CDpiAwareness::GetDpiFromDpiAwarenessContextProc CDpiAwareness::s_pGetDFDAC = nullptr;
	__declspec(selectany) CDpiAwareness::GetDpiForSystemProc			   CDpiAwareness::s_pGetDFS = nullptr;
#pragma endregion CDpiAwareness declspec statics
}
