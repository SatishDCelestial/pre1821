// This was a first try at making FindExact return a list of DType, as apposed
// to one DType with concatenated defs.
// So searching for Class::Method, would return a list of all defs no matter where they came from.
// The flaw in this is that FileDicBase::FindExact and FileDicBase::Find both concatenate the defs
// into the first DType.
#pragma once

typedef std::list<DType> TClassDataColl;

class ClassDataList : public TClassDataColl
{
  public:
	ClassDataList()
	{
	}
};

/*
class ClassDataList :  ClassDataVect
{
    int nextCD;
    BOOL sorted;
public:
    void AddClassData(const DType *data)
    {
        nextCD = 0;
        sorted = FALSE;
        push_back(DType(data));
    }
    INT Size()
    {
        return ClassDataVect::size();
    }

    DType* GetFirstDef()
    {
        nextCD = 0;
        return GetNextDef();
    }
    DType* GetNextDef()
    {
        if(nextCD< (int)Size())
            return GetClassData(nextCD++);
        return NULL;
    }
    WTString ConcatenateAllDefs()
    {
        // concatenate all defs with \f separation
        int sz = Size();
        if(!sz)
            return NULLSTR;
        WTString defs;
        for(int i = 0; i < sz; i++)
        {
            WTString def = GetClassData(i)->Def() + '\f';
            if(!defs.contains(def))
                defs += def;
        }
        return defs;
    }
protected:
    void SortList()
    {
        // TODO: Need to iterate list sorting by type:
        // Local/Project/System/Preferred/ForwardDeclares
        sorted = TRUE;
    }
    DType *GetClassData(int i)
    {
        ASSERT( i < (int)Size());
        if(!sorted)
            SortList();
        return &(*this)[i];
    }
};
*/
