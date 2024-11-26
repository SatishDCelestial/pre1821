#pragma once

#include "Win32Heap.h"

// [case: 92495]
// This class is used to create an allocator for stl containers that uses an
// independent / private heap.
// see usage in FileFinder::LocationValue and FileIdManager::FileIdToNameMap.
// It is cloned from std::allocator but uses a specified heap instead of global operator new / delete.

#ifndef _THROW0
#define _THROW0()
#endif

template <class _Ty, Win32Heap& _heap>
class PrivateHeapAllocator : public
#if (_MSC_VER < 1900)
                             std::_Allocator_base<_Ty>
#else
                             std::allocator<_Ty>
#endif
{ // generic allocator for objects of class _Ty
  public:
	typedef PrivateHeapAllocator<_Ty, _heap> other;

#if (_MSC_VER < 1900)
	typedef _Allocator_base<_Ty> _Mybase;
#else
	typedef std::allocator<_Ty> _Mybase;
#endif
	typedef typename _Mybase::value_type value_type;

	typedef value_type* pointer;
	typedef const value_type* const_pointer;
	typedef void* void_pointer;
	typedef const void* const_void_pointer;

	typedef value_type& reference;
	typedef const value_type& const_reference;

	typedef size_t size_type;
	typedef ptrdiff_t difference_type;

	typedef std::false_type propagate_on_container_copy_assignment;
	typedef std::false_type propagate_on_container_move_assignment;
	typedef std::false_type propagate_on_container_swap;

	PrivateHeapAllocator<_Ty, _heap> select_on_container_copy_construction() const
	{ // return this allocator
		return (*this);
	}

	template <class _Other> struct rebind
	{ // convert this type to PrivateHeapAllocator<_Other>
		typedef PrivateHeapAllocator<_Other, _heap> other;
	};

	pointer address(reference _Val) const noexcept
	{ // return address of mutable _Val
		return (_STD addressof(_Val));
	}

	const_pointer address(const_reference _Val) const noexcept
	{ // return address of nonmutable _Val
		return (_STD addressof(_Val));
	}

	PrivateHeapAllocator() _THROW0()
	{ // construct default allocator (do nothing)
	}

	PrivateHeapAllocator(const PrivateHeapAllocator<_Ty, _heap>&) _THROW0()
	{ // construct by copying (do nothing)
	}

	template <class _Other> PrivateHeapAllocator(const PrivateHeapAllocator<_Other, _heap>&) _THROW0()
	{ // construct from a related allocator (do nothing)
	}

	template <class _Other> PrivateHeapAllocator<_Ty, _heap>& operator=(const PrivateHeapAllocator<_Other, _heap>&)
	{ // assign from a related allocator (do nothing)
		return (*this);
	}

	void deallocate(pointer _Ptr, size_type)
	{ // deallocate object at _Ptr, ignore size
		_heap.Free(_Ptr);
	}

	pointer allocate(size_type _Count)
	{ // allocate array of _Count elements
		void* _Ptr = 0;

		if (_Count == 0)
			;
		else if (((size_t)(-1) / sizeof(_Ty) < _Count) || (_Ptr = _heap.Alloc(_Count * sizeof(_Ty))) == 0)
			std::_Xbad_alloc(); // report no memory

		return ((_Ty*)_Ptr);
	}

	pointer allocate(size_type _Count, const void*)
	{ // allocate array of _Count elements, ignore hint
		return (allocate(_Count));
	}

	void construct(_Ty* _Ptr)
	{ // default construct object at _Ptr
		::new ((void*)_Ptr) _Ty();
	}

	void construct(_Ty* _Ptr, const _Ty& _Val)
	{ // construct object at _Ptr with value _Val
		::new ((void*)_Ptr) _Ty(_Val);
	}

	template <class _Objty, class... _Types> void construct(_Objty* _Ptr, _Types&&... _Args)
	{ // construct _Objty(_Types...) at _Ptr
		::new ((void*)_Ptr) _Objty(_STD forward<_Types>(_Args)...);
	}

	template <class _Uty> void destroy(_Uty* _Ptr)
	{ // destroy object at _Ptr
		_Ptr->~_Uty();
	}

	size_t max_size() const _THROW0()
	{ // estimate maximum array size
		return ((size_t)(-1) / sizeof(_Ty));
	}
};
