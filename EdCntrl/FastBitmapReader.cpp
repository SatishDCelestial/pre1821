#include "stdafxed.h"
#include "FastBitmapReader.h"

// Read all of the pixels at once into an array of bytes
bool CFastBitmapReader::constructFrom(HDC aHdc, HBITMAP hBitMap)
{
	hdc = aHdc;
	memset(&bitmapInfo, 0, sizeof bitmapInfo);
	bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
	if (!GetDIBits(hdc, hBitMap, 0, 0, NULL, &bitmapInfo, 0 /* = DIG_RGB_COLORS */))
		return false;
	rowSize = pixelWidth();
	sizeInBytes = pixelHeight() * pixelWidth() * 4;
	data = new char[(size_t)sizeInBytes];

	// standardize to positive height, no compression, 32-bit
	bitmapInfo.bmiHeader.biBitCount = 32;
	bitmapInfo.bmiHeader.biCompression = BI_RGB;
	bitmapInfo.bmiHeader.biHeight = pixelHeight();
	if (!GetDIBits(hdc, hBitMap, 0, (uint)pixelHeight(), data, &bitmapInfo, 0 /* = DIG_RGB_COLORS */))
		return false;
	bGood = true;
	return true;
}
