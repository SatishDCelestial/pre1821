#include <Windows.h>
#include <atltypes.h>

// Replaces slow GitPixels calls. [case: 112147]
// source: https://developercommunity.visualstudio.com/content/problem/136952/mfc-ribbon-slow-on-first-loadshow.html
//		   https://developercommunity.visualstudio.com/storage/attachments/16873-edits-to-afxtoolbarimages.txt
//		   https://stackoverflow.com/questions/3688409/getdibits-and-loop-through-pixels-using-x-y
class CFastBitmapReader
{
  public:
	CFastBitmapReader(HDC aHdc, HBITMAP hBitMap) : bGood(false), data(0)
	{
		constructFrom(aHdc, hBitMap);
	}
	~CFastBitmapReader()
	{
		delete[] data;
		data = 0;
	}

	COLORREF FastGetPixel(int x, int y)
	{
		if (bGood)
		{
			int offset = (rowSize * (pixelHeight() - y - 1) + x) * 4;
			if ((offset >= 0) && (offset + 2 < sizeInBytes))
			{
				unsigned char b = (unsigned char)data[offset];
				unsigned char g = (unsigned char)data[offset + 1];
				unsigned char r = (unsigned char)data[offset + 2];
				return RGB(r, g, b);
			}
		}
		// Fall back on standard GetPixel if the pixels could not be read
		return GetPixel(hdc, x, y);
	}

  protected:
	BITMAPINFO bitmapInfo;
	CSize size;
	bool constructFrom(HDC hdc, HBITMAP hBitMap);
	int pixelHeight()
	{
		return abs(bitmapInfo.bmiHeader.biHeight);
	}
	int pixelWidth()
	{
		return abs(bitmapInfo.bmiHeader.biWidth);
	}
	int sizeInBytes;
	int rowSize;
	HDC hdc;
	bool bGood;
	char* data;
};
