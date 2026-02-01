/*
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_UTIL_ATOMIC_H
#define _KERNEL_UTIL_ATOMIC_H


#include <SupportDefs.h>

#include <debug.h>


#ifdef __cplusplus

template<typename PointerType> PointerType*
atomic_pointer_test_and_set(PointerType** _pointer, const PointerType* set,
	const PointerType* test)
{
	return (PointerType*)atomic_test_and_set64((int64*)_pointer, (int64)set,
		(int64)test);
}


template<typename PointerType> PointerType*
atomic_pointer_get_and_set(PointerType** _pointer, const PointerType* set)
{
	return (PointerType*)atomic_get_and_set64((int64*)_pointer, (int64)set);
}


template<typename PointerType> void
atomic_pointer_set(PointerType** _pointer, const PointerType* set)
{
	ASSERT((addr_t(_pointer) & (sizeof(PointerType*) - 1)) == 0);
	atomic_set64((int64*)_pointer, (int64)set);
}


template<typename PointerType> PointerType*
atomic_pointer_get(PointerType* const* _pointer)
{
	ASSERT((addr_t(_pointer) & (sizeof(PointerType*) - 1)) == 0);
	return (PointerType*)atomic_get64((int64*)_pointer);
}

#endif	// __cplusplus

#endif	/* _KERNEL_UTIL_ATOMIC_H */
