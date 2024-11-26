namespace CodeGraphNS
{
	CREATE_MLC(VAParseCodeGraph, ParseDGMLCls_MLC);
	void VAParseCodeGraph::StaticGraphCodeFile(LPCWSTR file, LPCSTR symscope, IReferenceNodesContainer *dgml)
	{
//		try
//		{
			if (!Is_Some_Other_File(GetFileType(file)))
			{
//				static int count = 0;
//				count = g_pGlobDic->GetHashTable()->GetItemsInContainer();

				ParseDGMLCls_MLC vp(GetFileType(file));
				vp->GraphCodeFile(file, symscope, dgml);
			}
//		}
//		catch(...)
//		{
//
//		}
	}
}
