#pragma once

class CSaveDC
{
  public:
	CSaveDC(CDC& dc) : dc(dc)
	{
		i = dc.SaveDC();
	}
	~CSaveDC()
	{
		dc.RestoreDC(i);
	}

	void Reset()
	{
		dc.RestoreDC(i);
		i = dc.SaveDC();
	}

	CSaveDC& operator=(const CSaveDC&) = delete;

  protected:
	CDC& dc;
	int i;
};
