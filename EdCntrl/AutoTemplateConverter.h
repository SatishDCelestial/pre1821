#pragma once

class CAutoTemplateConverter
{
  public:
	CAutoTemplateConverter()
	{
	}

	void ConvertIfRequired(const CStringW& filePath);

  private:
	CStringW m_sTplFilePath;
	CString m_sOutputBuffer;

	bool IsConversionRequired();
	bool ConvertTemplateFile();
	void Save();
};
