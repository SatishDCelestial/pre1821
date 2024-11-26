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
#pragma warning(disable : 5246)
#include "VsUIGdiplusImage.h"

namespace VsUI
{
    // Namespace global color definitions
    const Gdiplus::Color TransparentColor = Gdiplus::Color(0,0,0,0);
    const Gdiplus::Color MagentaColor = Gdiplus::Color(255, 255, 0, 255);
    const Gdiplus::Color NearGreenColor = Gdiplus::Color(255, 0, 254, 0);
    const Gdiplus::Color HaloColor = Gdiplus::Color(0xFF, 0xF6, 0xF6, 0xF6);
    const Gdiplus::Color TransparentHaloColor = Gdiplus::Color(0, 0xF6, 0xF6, 0xF6);

    /*static*/ GdiplusImage::CInitGDIPlus GdiplusImage::s_initGDIPlus;

    GdiplusImage::ImageDC::ImageDC(GdiplusImage& img)
    {
        m_pGraphics.Attach( img.GetGraphics() );
        m_hDC = m_pGraphics->GetHDC();
    }

    GdiplusImage::ImageDC::~ImageDC()
    {
        m_pGraphics->ReleaseHDC(m_hDC);
    }

    GdiplusImage::GdiplusImage()
    {
        s_initGDIPlus.Init();
        s_initGDIPlus.IncreaseImageCount();
    }

    GdiplusImage::~GdiplusImage()
    {
        Release(); // must delete the bitmap before uninitializing GDI+
        s_initGDIPlus.DecreaseImageCount();
    }
        
    // Exchange the content of the 2 images
    GdiplusImage& GdiplusImage::operator=(GdiplusImage&& rhs)
    {
//        std::swap(m_pBitmap, rhs.m_pBitmap);
        m_pBitmap = rhs.m_pBitmap;      // operator= takes ownership
        return *this;
    }

//	GdiplusImage& GdiplusImage::operator=(GdiplusImage& rhs)
//	{
//		std::swap(m_pBitmap, rhs.m_pBitmap);
//		return *this;
//	}

    int GdiplusImage::GetWidth() const
    {
        ATLASSUME( IsLoaded() );
        if (!m_pBitmap)
        {
            return 0;
        }
        return (int)m_pBitmap->GetWidth();
    }

    int GdiplusImage::GetHeight() const
    {
        ATLASSUME( IsLoaded() );
        if (!m_pBitmap)
        {
            return 0;
        }
        return (int)m_pBitmap->GetHeight();
    }
    
    //---------------------------------------------------------------
    // Releases a loaded image
    //---------------------------------------------------------------
    void GdiplusImage::Release()
    {
        m_pBitmap.Free();
    }
    
    //---------------------------------------------------------------
    // Release the current bitmap and attaches to the new one
    //---------------------------------------------------------------
    void GdiplusImage::SetBitmap(Gdiplus::Bitmap* pBitmap)
    {
        Release();
        m_pBitmap.Attach(pBitmap);
    }

    //---------------------------------------------------------------
    // Create an image with the specified size and format
    //---------------------------------------------------------------
    void GdiplusImage::Create( int width, int height, const Gdiplus::PixelFormat format )
    {
#pragma push_macro("new")
#undef new
        Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(width, height, format);
#pragma pop_macro("new")
        if( pBitmap )
        {
            SetBitmap(pBitmap);
        }
    }
    
    //---------------------------------------------------------------
    // Attach to an existing HBITMAP. If the HBITMAP is a 32bpp DIB,
    // then we'll create an ARGB Gdiplus image.
    //---------------------------------------------------------------
    void GdiplusImage::Attach( HBITMAP hBmp )
    {
        Gdiplus::Bitmap* pBitmap = NULL;

        // If we have a 32bpp DIB created by calling CreateDIBSection, assume that it's in ARGB format. 
        // This is the preferred format for full per-pixel alpha support.
        DIBSECTION dib = {};
        if( ::GetObject(hBmp, sizeof(dib), &dib) == sizeof(DIBSECTION) && dib.dsBm.bmBitsPixel == 32 )
        {
            pBitmap = CreateARGBBitmapFromDIB(dib);
        }
        else
        {
            // Fall back to Gdiplus conversion
            pBitmap = Gdiplus::Bitmap::FromHBITMAP(hBmp, NULL);
        }

        if( pBitmap )
        {
            SetBitmap(pBitmap);
        }
    }

    //---------------------------------------------------------------
    // Convert the image to an HBITMAP and detach ownership.
    //---------------------------------------------------------------
    HBITMAP GdiplusImage::Detach(const Gdiplus::Color& backgroundColor)
    {
        if(!IsLoaded())
        {
            return NULL;
        }

        HBITMAP hBmp = NULL;
        if( Gdiplus::Ok != m_pBitmap->GetHBITMAP( backgroundColor, &hBmp ) )
        {
            return NULL;
        }

        Release();
        return hBmp;
    }

    //---------------------------------------------------------------
    // Attach to an existing HICON.
    //---------------------------------------------------------------
    void GdiplusImage::AttachIcon( HICON hIcon )
    {
        Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromHICON(hIcon);

        if( pBitmap )
        {
            SetBitmap(pBitmap);
        }
    }

    //---------------------------------------------------------------
    // Convert the image to an HICON and detach ownership.
    //---------------------------------------------------------------
    HICON GdiplusImage::DetachIcon()
    {
        if(!IsLoaded())
        {
            return NULL;
        }

        HICON hIcon = NULL;
        if( Gdiplus::Ok != m_pBitmap->GetHICON( &hIcon ) )
        {
            return NULL;
        }

        Release();
        return hIcon;
    }

    //-----------------------------------------------------------------
    // Get a Gdiplus graphics surface for drawing onto the loaded image
    //-----------------------------------------------------------------
    Gdiplus::Graphics* GdiplusImage::GetGraphics()
    {
        if(!IsLoaded())
        {
            return NULL;
        }
        return Gdiplus::Graphics::FromImage(m_pBitmap);
    }

    //-----------------------------------------------------------------
    // Load the image from a file
    // Supports all formats that Gdiplus supports (BMP, PNG, JPG etc)
    //-----------------------------------------------------------------
    HRESULT GdiplusImage::Load( _In_z_ LPCWSTR wszFilename )
    {
        if( !wszFilename )
        {
            return E_INVALIDARG;
        }

        // PERF: Don't call Gdiplus::Bitmap::FromFile on files that don't
        // exist because GDI+ blindly tries different ways to read the given file.
        if( !::PathFileExistsW( wszFilename ) )
        {
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        }

        Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromFile( wszFilename );
        if( !pBitmap )
        {
            return E_FAIL;
        }

        SetBitmap(pBitmap);
        return S_OK;
    }

    //-----------------------------------------------------------------
    // Load the image from resources
    // Supports all formats that Gdiplus supports (BMP, PNG, JPG etc)
    //-----------------------------------------------------------------
    HRESULT GdiplusImage::LoadFromResource( HINSTANCE hInstance, UINT nIDResource, _In_z_ LPCWSTR wszResourceType )
    {
        if( !hInstance || !nIDResource || !wszResourceType )
        {
            return E_INVALIDARG;
        }

        // For BMP, use built-in helper
        if( wszResourceType == (LPCWSTR)RT_BITMAP )
        {
            Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromResource( hInstance, MAKEINTRESOURCEW(nIDResource) );
            if( !pBitmap )
            {
                return E_FAIL;
            }

            SetBitmap(pBitmap);
            return S_OK;
        }

        HRSRC hrsrc = ::FindResourceW( hInstance, MAKEINTRESOURCEW(nIDResource), wszResourceType );
        if( !hrsrc )
        {
            return E_INVALIDARG;
        }

        CComPtr< IStream > spStream;
        HRESULT hr = CreateStreamOnResource( hInstance, hrsrc, &spStream );
        if( FAILED(hr) )
        {
            return hr;
        }

        Gdiplus::Bitmap* pBitmap = Gdiplus::Bitmap::FromStream(spStream);
        if( !pBitmap )
        {
            return E_FAIL;
        }

        SetBitmap(pBitmap);
        return S_OK;
    }

    //-----------------------------------------------------------------
    // Load the image from resources. Try PNG first and then BMP format
    //-----------------------------------------------------------------
    HRESULT GdiplusImage::LoadFromPngOrBmp( HINSTANCE hInstance, UINT nIDResource )
    {
        // Try PNG first
        HRESULT hr = LoadFromResource( hInstance, nIDResource, L"PNG" );
        if( SUCCEEDED(hr) )
        {
            return hr;
        }

        // Fall back to BMP
        return LoadFromResource( hInstance, nIDResource, (LPCWSTR)RT_BITMAP );
    }

    //-----------------------------------------------------------------
    // Save to the given stream in the specified format
    //-----------------------------------------------------------------
    HRESULT GdiplusImage::Save( _In_ IStream* pStream, const GUID& format )
    {
        if( !pStream )
        {
            return E_INVALIDARG;
        }
        
        return SaveBitmap(format, [&](const CLSID * clsidEncoder) {
            return m_pBitmap->Save( pStream, clsidEncoder, NULL );
        });
    }

    //-----------------------------------------------------------------
    // Save to the given file in the specified format
    //-----------------------------------------------------------------
    HRESULT GdiplusImage::Save( _In_z_ LPCWSTR wszFilename, const GUID& format )
    {
        if( !wszFilename )
        {
            return E_INVALIDARG;
        }
        
        return SaveBitmap(format, [&](const CLSID * clsidEncoder) {
            return m_pBitmap->Save( wszFilename, clsidEncoder, NULL );
        });
    }
    
    //-----------------------------------------------------------------
    // Save to the given file in the specified format
    //-----------------------------------------------------------------
    HRESULT GdiplusImage::SaveBitmap(const GUID& format, std::function< Gdiplus::Status (_In_ const CLSID * clsidEncoder) > saveFunction )
    {
        if( !IsLoaded() )
        {
            return E_FAIL;
        }

        UINT nEncoders;
        UINT nBytes;
        if( Gdiplus::Ok != Gdiplus::GetImageEncodersSize( &nEncoders, &nBytes ) )
        {
            return E_FAIL;
        }

        USES_ATL_SAFE_ALLOCA;
        Gdiplus::ImageCodecInfo* pCodecs = static_cast< Gdiplus::ImageCodecInfo* >( _ATL_SAFE_ALLOCA(nBytes, _ATL_SAFE_ALLOCA_DEF_THRESHOLD) );
        if( pCodecs == NULL )
        {
            return E_OUTOFMEMORY;
        }

        if( Gdiplus::Ok != Gdiplus::GetImageEncoders( nEncoders, nBytes, pCodecs ) )
        {
            return E_FAIL;
        }

        for( UINT n = 0; n != nEncoders; ++n )
        {
            if( pCodecs[n].FormatID == format )
            {
                if( Gdiplus::Ok != saveFunction( &(pCodecs[n].Clsid) ) )
                {
                    return E_FAIL;
                }

                return S_OK;
            }
        }

        return E_FAIL;
    }
    
    //-----------------------------------------------------------------
    // Converts the bitmap to 32bpp ARGB if necessary and converts all pixels of clrTransparency color to be fully transparent.
    //-----------------------------------------------------------------
    HRESULT GdiplusImage::MakeTransparent(const Gdiplus::Color& clrTransparency)
    {
        if( !IsLoaded() )
        {
            return E_FAIL;

        }
    
        Gdiplus::PixelFormat format = m_pBitmap->GetPixelFormat();
        
        // Modify the image. If the image is 24bpp or lower, convert to 32bpp so we can use alpha values
        if (format != PixelFormat32bppARGB)
        {
            m_pBitmap->ConvertFormat(PixelFormat32bppARGB, Gdiplus::DitherTypeNone, Gdiplus::PaletteTypeCustom, nullptr/*ColorPalette*/, 0 /*alphaThresholdPercent - all opaque*/);
        }
        
        // Now that we have 32bpp image, let's make the pixels transparent
        ProcessBitmapBits(m_pBitmap, [&](Gdiplus::ARGB * pPixelData) 
        {
            if (*pPixelData == clrTransparency.GetValue())
            {
                *pPixelData = TransparentColor.GetValue();
            }
        });
        
        return S_OK;
    }
    
    //-----------------------------------------------------------------
    // Apply a processor function to all bitmap pixels 
    //-----------------------------------------------------------------
    void GdiplusImage::ProcessBitmapBits(_In_ Gdiplus::Bitmap * pBitmap, std::function<void (_Inout_ Gdiplus::ARGB* pPixelData)> pixelProcessor)
    {
        if (!pBitmap)
            return;

        Gdiplus::BitmapData lockedBitmapData;
        UINT bitmapWidth = pBitmap->GetWidth();
        UINT bitmapHeight = pBitmap->GetHeight();

        // Figure out what is the transparency color. Make Magenta/NearGreen pixels transparent-Halo
        Gdiplus::Rect rectImage(0, 0, (int)bitmapWidth, (int)bitmapHeight);
        if (pBitmap->LockBits(&rectImage, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &lockedBitmapData) == Gdiplus::Ok)
        {
            BYTE * pData = reinterpret_cast<BYTE*>(lockedBitmapData.Scan0);
            for (UINT y = 0; y < bitmapHeight; y++, pData += lockedBitmapData.Stride)
            {
                BYTE * pPixelData = pData;
                for (UINT x = 0; x < bitmapWidth; x++, pPixelData += 4)
                {
                    pixelProcessor(reinterpret_cast<Gdiplus::ARGB*>(pPixelData));
                }
            }

            pBitmap->UnlockBits(&lockedBitmapData);
        }
    }

    void GdiplusImage::ProcessBitmapBitsXY( _In_ Gdiplus::Bitmap* pBitmap,std::function<void(_Inout_ Gdiplus::ARGB* pPixelData, UINT x, UINT y)> pixelProcessor)
    {
	    if (!pBitmap)
		    return;

	    Gdiplus::BitmapData lockedBitmapData;
	    UINT bitmapWidth = pBitmap->GetWidth();
	    UINT bitmapHeight = pBitmap->GetHeight();

	    // Figure out what is the transparency color. Make Magenta/NearGreen pixels transparent-Halo
	    Gdiplus::Rect rectImage(0, 0, (int)bitmapWidth, (int)bitmapHeight);
	    if (pBitmap->LockBits(&rectImage, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
	                          PixelFormat32bppARGB, &lockedBitmapData) == Gdiplus::Ok)
	    {
		    BYTE* pData = reinterpret_cast<BYTE*>(lockedBitmapData.Scan0);
		    for (UINT y = 0; y < bitmapHeight; y++, pData += lockedBitmapData.Stride)
		    {
			    BYTE* pPixelData = pData;
			    for (UINT x = 0; x < bitmapWidth; x++, pPixelData += 4)
			    {
				    pixelProcessor(reinterpret_cast<Gdiplus::ARGB*>(pPixelData), x, y);
			    }
		    }

		    pBitmap->UnlockBits(&lockedBitmapData);
	    }
    }

    bool GdiplusImage::Sharpen(std::initializer_list<std::tuple<float, float>> passes, bool preserveAlpha)
    {
        _ASSERTE(m_pBitmap && m_pBitmap->GetPixelFormat() == PixelFormat32bppARGB);

        // sharpening breaks alpha channel, so if we want to preserve existing transparency, 
        // we need to copy alpha channel from the initial bitmap thus we clone bitmap here
        // to have the source of original image as a source of alpha channel
        auto alphaClone = std::unique_ptr<Gdiplus::Bitmap>(
            preserveAlpha ?
            m_pBitmap->Clone(0, 0, (int)m_pBitmap->GetWidth(), (int)m_pBitmap->GetHeight(), PixelFormat32bppARGB) : nullptr);

        // apply sharpening passes one by one
        for (auto pass : passes)
        {
            Gdiplus::SharpenParams params;
            params.radius = std::get<0>(pass);
            params.amount = std::get<1>(pass);

            Gdiplus::Sharpen shrp;
            shrp.SetParameters(&params);

            // apply the sharpening pass
            if (Gdiplus::Ok != m_pBitmap->ApplyEffect(&shrp, nullptr))
                return false;
        }

		if (preserveAlpha)
		{
            // let's copy alpha channel from the clone

			Gdiplus::Rect rect(0, 0, (INT)m_pBitmap->GetWidth(), (INT)m_pBitmap->GetHeight());

			Gdiplus::BitmapData srcData, dstData;

			if (m_pBitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &dstData) == Gdiplus::Ok &&
			    alphaClone->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &srcData) == Gdiplus::Ok)
			{
				BYTE* pDstData = reinterpret_cast<BYTE*>(dstData.Scan0);
				BYTE* pSrcData = reinterpret_cast<BYTE*>(srcData.Scan0);

				for (INT y = 0; y < rect.Height; y++, pDstData += dstData.Stride, pSrcData += srcData.Stride)
				{
					BYTE* pDstPixData = pDstData;
					BYTE* pSrcPixData = pSrcData;
					for (INT x = 0; x < rect.Width; x++, pDstPixData += 4, pSrcPixData += 4)
					{
						pDstPixData[3] = pSrcPixData[3]; // copy alpha
					}
				}

				m_pBitmap->UnlockBits(&dstData);
				alphaClone->UnlockBits(&srcData);
			}
		}

        return true;
    }

    //---------------------------------------------------------------
    // Create an in-memory stream over a resource.
    // The resource must have been found via FindResource
    //---------------------------------------------------------------
    HRESULT GdiplusImage::CreateStreamOnResource( HINSTANCE hInst, HRSRC hResource, IStream** ppStream )
    {
        if( !ppStream )
        {
            return E_POINTER;
        }

        if( !hResource )
        {
            return E_INVALIDARG;
        }

        HGLOBAL hGlob = ::LoadResource(hInst, hResource);
        if( !hGlob )
        {
            return E_INVALIDARG;
        }

        // It would be nice if we could just call CreateStreamOnHGlobal on hGlob.
        // Unfortunately that doesn't work because the HGLOBAL we have here came
        // from LoadResource and not GlobalAlloc.
        // Instead, create a new stream (pass NULL for the HGLOBAL) and write
        // the resource into that stream.
        CComPtr< IStream > spStream;
        HRESULT hr = ::CreateStreamOnHGlobal(NULL /*create new*/, TRUE /*free on close*/, &spStream );
        if( FAILED(hr) )
        {
            return hr;
        }

        ULONG cbWritten = 0;
        hr = spStream->Write( ::LockResource(hGlob), ::SizeofResource(hInst, hResource), &cbWritten );
        if( FAILED(hr) )
        {
            return hr;
        }

        // Rewind the stream to the beginning.
        LARGE_INTEGER liZero = { 0, 0 }; // seek to zero
        hr = spStream->Seek( liZero, STREAM_SEEK_SET, NULL /*optional new position*/ );
        if( FAILED(hr) )
        {
            return hr;
        }

        return spStream.CopyTo(ppStream);
    }

    //---------------------------------------------------------------
    // Create a 32bpp ARGB Gdiplus::Bitmap from a DIBSECTION
    //---------------------------------------------------------------
    Gdiplus::Bitmap* GdiplusImage::CreateARGBBitmapFromDIB( const DIBSECTION& dib )
    {
        int width = dib.dsBmih.biWidth;
        int pitch = dib.dsBm.bmWidthBytes;
        int height = dib.dsBmih.biHeight;
        BYTE* pBits = static_cast< BYTE* >(dib.dsBm.bmBits);

        if( height < 0 )
        {
            // Top-down DIB
            height = -height;
        }
        else
        {
            // Bottom-up. Adjust the Scan0 to the start of the last row
            pBits += ( height - 1 ) * pitch;
            // and set the pitch to a -ve value
            pitch = -pitch;
        }

#pragma push_macro("new")
#undef new
        return new Gdiplus::Bitmap(width, height, pitch, PixelFormat32bppARGB, pBits);
#pragma pop_macro("new")
    }

    //---------------------------------------------------------------
    // Initializes GDI+ if not already initialized
    //---------------------------------------------------------------
    bool GdiplusImage::CInitGDIPlus::Init()
    {
        if( m_GdiplusToken != 0 )
        {
            // Already initialized
            return true;
        }

        ULONG_PTR token = 0;
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::Status status = Gdiplus::GdiplusStartup( &token, &input, NULL );
        if( status != Gdiplus::Ok )
        {
            return false;
        }

        if( InterlockedExchangeToken(token) != 0 )
        {
            // Initialized by another thread
            Gdiplus::GdiplusShutdown(token);
        }

        return true;
    }

    //---------------------------------------------------------------
    // Releases GDI+ if this is the last user
    //---------------------------------------------------------------
    void GdiplusImage::CInitGDIPlus::ReleaseGDIPlus()
    {
        ULONG_PTR token = InterlockedExchangeToken(0);
        if( token != 0 )
        {
            Gdiplus::GdiplusShutdown( token );
        }
    }

    //---------------------------------------------------------------
    // Increases image use count
    //---------------------------------------------------------------
    void GdiplusImage::CInitGDIPlus::IncreaseImageCount()
    {
        _InterlockedIncrement(&m_GdiplusImageObjects);
    }

    //---------------------------------------------------------------
    // Decreases image use count, releases GDI+ if this is the last image
    //---------------------------------------------------------------
    void GdiplusImage::CInitGDIPlus::DecreaseImageCount()
    {
        if( _InterlockedDecrement(&m_GdiplusImageObjects) == 0 )
        {
            ReleaseGDIPlus();
        }
    }

    ULONG_PTR GdiplusImage::CInitGDIPlus::InterlockedExchangeToken( ULONG_PTR token )
    {
        return m_GdiplusToken.exchange(token);
    }

};  // namespace VsUI
