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

//-----------------------------------------------------------------------------
// Wrapper around Gdiplus::Bitmap
// Uses GDI+ so we can work entirely in 32bpp ARGB mode
// Note: Some of this is copied from ATL::CImage (particularly the CInitGDIPlus
// helper). The key difference is that we store the image as Gdiplus::Bitmap
// internally.
//-----------------------------------------------------------------------------
#pragma once

#pragma warning(push, 1)
#include <shlwapi.h> // For PathFileExists
#pragma comment(lib, "shlwapi.lib")

// we want the lastest API supported
#ifndef GDIPVER 
#define GDIPVER 0x0110
#endif

#pragma push_macro("new")
#undef new
#pragma push_macro("delete")
#undef delete
#pragma warning(push)
#pragma warning(disable: 4365 4458)
#include <gdiplus.h>
#pragma warning(pop)
#pragma pop_macro("delete")
#pragma pop_macro("new")

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

#include <intrin.h>
#include <atlbase.h>
#include <algorithm>
#include <functional>
#pragma warning(pop)

namespace VsUI
{
    extern const Gdiplus::Color TransparentColor;
    extern const Gdiplus::Color MagentaColor;
    extern const Gdiplus::Color NearGreenColor;
    extern const Gdiplus::Color HaloColor;
    extern const Gdiplus::Color TransparentHaloColor;

    class GdiplusImage
    {
    private:
        class CInitGDIPlus
        {
        public:
            CInitGDIPlus() : m_GdiplusToken(0), m_GdiplusImageObjects(0)
            {
            }

            ~CInitGDIPlus()
            {
                ReleaseGDIPlus();
            }

            bool Init();
            void ReleaseGDIPlus();
            void IncreaseImageCount();
            void DecreaseImageCount();

        private:
            ULONG_PTR InterlockedExchangeToken( ULONG_PTR token );
            std::atomic<uintptr_t> m_GdiplusToken;
            LONG m_GdiplusImageObjects;
        };

    public:
        class ImageDC
        {
            ATL::CAutoPtr<Gdiplus::Graphics> m_pGraphics;
            HDC m_hDC;

        public:
            ImageDC(GdiplusImage& img);
            ~ImageDC();

            operator HDC() const
            {
                return m_hDC;
            }
        };

        GdiplusImage();
        ~GdiplusImage();
        
        // Exchange the content of the 2 images
        GdiplusImage& operator=(GdiplusImage&& rhs);
		GdiplusImage& operator=(GdiplusImage& rhs);

        bool IsLoaded() const
        {
            return m_pBitmap.m_p != NULL;
        }

        operator Gdiplus::Bitmap* () const
        {
            return GetBitmap();
        }
        
        Gdiplus::Bitmap* GetBitmap() const
        {
            return m_pBitmap;
        }

        // Releases a loaded image
        void Release();
        
        // Return loaded image dimensions
        int GetWidth() const;
        int GetHeight() const;

        // Create an image with the specified size and format
        void Create( int width, int height, const Gdiplus::PixelFormat format = PixelFormat32bppARGB );
        
        // Attach to an existing HBITMAP (does NOT take ownership of the HBITMAP passed as argument). 
		// Method uses Bitmap.FromHBITMAP or if the HBITMAP is a 32bpp DIB, then it creates an ARGB Gdiplus 
		// image based on bits from HBITMAP. In both cases class does not take ownership of the HBITMAP
		// passed as argument.
		// WARNING: 
		// Passed HBITMAP must be valid for whole lifetime of the GdiplusImage attached to it!
        void Attach( HBITMAP hBmp );

        // Convert the image to a new HBITMAP and detach ownership of wrapped GDI+ bitmap.
		// The caller is responsive to call DeleteObject on the returned HBITMAP when no longer needed.
        HBITMAP Detach( const Gdiplus::Color& backgroundColor = TransparentColor );

        // Attach to an existing HICON
        void AttachIcon( HICON hIcon );

        // Convert the image to an HICON and detach ownership.
        HICON DetachIcon();

        // Get a Gdiplus graphics surface for drawing onto the loaded image
        Gdiplus::Graphics* GetGraphics();

        // Load the image from a file with formats that Gdiplus supports (BMP, PNG, JPG etc)
        HRESULT Load( _In_z_ LPCWSTR wszFilename );

        // Load the image from resources with formats that Gdiplus supports (BMP, PNG, JPG etc)
        HRESULT LoadFromResource( HINSTANCE hInstance, UINT nIDResource, _In_z_ LPCWSTR wszResourceType );

        // Load the image from resources. Try PNG first and then BMP format
        HRESULT LoadFromPngOrBmp( HINSTANCE hInstance, UINT nIDResource );

        // Save to the given stream in the specified format
        HRESULT Save( _In_ IStream* pStream, const GUID& format = Gdiplus::ImageFormatPNG );

        // Save to the given file in the specified format
        HRESULT Save( _In_z_ LPCWSTR wszFilename, const GUID& format = Gdiplus::ImageFormatPNG );

        // Converts the bitmap to 32bpp ARGB if necessary and converts all pixels of clrTransparency color to be fully transparent.
        HRESULT MakeTransparent(const Gdiplus::Color& clrTransparency = MagentaColor);
        
        // Apply a processor function to all bitmap pixels 
        static void ProcessBitmapBits(_In_ Gdiplus::Bitmap * pBitmap, std::function<void (_Inout_ Gdiplus::ARGB* pPixelData)> pixelProcessor);
	    static void ProcessBitmapBitsXY(_In_ Gdiplus::Bitmap* pBitmap, std::function<void(_Inout_ Gdiplus::ARGB* pPixelData, UINT x, UINT y)> pixelProcessor);

        // Apply sharpening optionally in multiple passes
        // passes: list of radius + amount pairs applied in multiple passes
        // preserveAlpha: makes copy of existing alpha channel, note: sharpening breaks alpha
        bool Sharpen(std::initializer_list<std::tuple<float, float>> passes, bool preserveAlpha);
    private:

        // Create an in-memory stream over a resource. The resource must have been found via FindResource
        static HRESULT CreateStreamOnResource( HINSTANCE hInst, HRSRC hResource, _Out_ IStream** ppStream );

        // Create a 32bpp ARGB Gdiplus::Bitmap from a DIBSECTION
        static Gdiplus::Bitmap* CreateARGBBitmapFromDIB( const DIBSECTION& dib );
        
        // Release the current bitmap and attaches to the new one
        void SetBitmap(Gdiplus::Bitmap* pBitmap);

        // Locates the codec for the specified format and calls the save function to save the bitmap
        HRESULT SaveBitmap(const GUID& format, std::function< Gdiplus::Status (_In_ const CLSID * clsidEncoder) > saveFunction );

    private:
        static CInitGDIPlus s_initGDIPlus;
        ATL::CAutoPtr<Gdiplus::Bitmap> m_pBitmap;
    };

};  // namespace VsUI
