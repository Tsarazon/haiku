/*
 * Copyright 2004, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2008-2009, Stephan Aßmus <superstippi@gmx.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include <vector>

#include <Locker.h>

#include "AutoLocker.h"

using std::vector;


// SemaphoreLocking policy for AutoLocker

class SemaphoreLocking {
public:
	inline bool Lock(sem_id* lockable)
	{
		return acquire_sem(*lockable) == B_OK;
	}

	inline void Unlock(sem_id* lockable)
	{
		release_sem(*lockable);
	}
};


class SemaphoreLocker : public AutoLocker<sem_id, SemaphoreLocking> {
public:
	inline SemaphoreLocker(sem_id semaphore, bool alreadyLocked = false,
			bool lockIfNotLocked = true)
		:
		AutoLocker<sem_id, SemaphoreLocking>(),
		fSem(semaphore)
	{
		SetTo(&fSem, alreadyLocked, lockIfNotLocked);
	}

private:
	sem_id	fSem;
};


// BlockingQueue - thread-safe queue with blocking Pop

typedef BLocker Locker;

template<typename Element>
class BlockingQueue : public Locker {
public:
								BlockingQueue(const char* name = NULL);
								~BlockingQueue();

			status_t			InitCheck() const;

			status_t			Close(bool deleteElements,
									const vector<Element*>** elements = NULL);

			status_t			Push(Element* element);
			status_t			Pop(Element** element,
									bigtime_t timeout = B_INFINITE_TIMEOUT);
			status_t			Peek(Element** element);
			status_t			Remove(Element* element);

			int32				Size();

private:
			vector<Element*>	fElements;
			sem_id				fElementSemaphore;
};


template<typename Element>
BlockingQueue<Element>::BlockingQueue(const char* name)
	: fElements(),
	  fElementSemaphore(-1)
{
	fElementSemaphore = create_sem(0, (name ? name : "blocking queue"));
}


template<typename Element>
BlockingQueue<Element>::~BlockingQueue()
{
	if (fElementSemaphore >= 0)
		delete_sem(fElementSemaphore);
}


template<typename Element>
status_t
BlockingQueue<Element>::InitCheck() const
{
	return (fElementSemaphore < 0 ? fElementSemaphore : B_OK);
}


template<typename Element>
status_t
BlockingQueue<Element>::Close(bool deleteElements,
	const vector<Element*>** elements)
{
	AutoLocker<Locker> _(this);
	status_t error = delete_sem(fElementSemaphore);
	if (error != B_OK)
		return error;
	fElementSemaphore = -1;
	if (elements)
		*elements = &fElements;
	if (deleteElements) {
		int32 count = fElements.size();
		for (int32 i = 0; i < count; i++)
			delete fElements[i];
		fElements.clear();
	}
	return error;
}


template<typename Element>
status_t
BlockingQueue<Element>::Push(Element* element)
{
	AutoLocker<Locker> _(this);
	if (fElementSemaphore < 0)
		return B_NO_INIT;
	try {
		fElements.push_back(element);
	} catch (std::bad_alloc&) {
		return B_NO_MEMORY;
	}
	status_t error = release_sem(fElementSemaphore);
	if (error != B_OK)
		fElements.erase(fElements.begin() + fElements.size() - 1);
	return error;
}


template<typename Element>
status_t
BlockingQueue<Element>::Pop(Element** element, bigtime_t timeout)
{
	status_t error = acquire_sem_etc(fElementSemaphore, 1, B_RELATIVE_TIMEOUT,
		timeout);
	if (error != B_OK)
		return error;
	AutoLocker<Locker> _(this);
	if (fElementSemaphore < 0)
		return B_NO_INIT;
	int32 count = fElements.size();
	if (count == 0)
		return B_ERROR;
	*element = fElements[0];
	fElements.erase(fElements.begin());
	return B_OK;
}


template<typename Element>
status_t
BlockingQueue<Element>::Peek(Element** element)
{
	AutoLocker<Locker> _(this);
	if (fElementSemaphore < 0)
		return B_NO_INIT;
	int32 count = fElements.size();
	if (count == 0)
		return B_ENTRY_NOT_FOUND;
	*element = fElements[0];
	return B_OK;
}


template<typename Element>
status_t
BlockingQueue<Element>::Remove(Element* element)
{
	status_t error = acquire_sem_etc(fElementSemaphore, 1,
		B_RELATIVE_TIMEOUT, 0);
	if (error != B_OK)
		return error;
	AutoLocker<Locker> _(this);
	if (fElementSemaphore < 0)
		return B_NO_INIT;

	int32 count = 0;
	for (int32 i = fElements.size() - 1; i >= 0; i--) {
		if (fElements[i] == element) {
			fElements.erase(fElements.begin() + i);
			count++;
		}
	}
	if (count == 0) {
		release_sem(fElementSemaphore);
		return B_ENTRY_NOT_FOUND;
	}
	if (count > 1) {
		acquire_sem_etc(fElementSemaphore, count - 1,
			B_RELATIVE_TIMEOUT, 0);
	}
	return error;
}


template<typename Element>
int32
BlockingQueue<Element>::Size()
{
	AutoLocker<Locker> _(this);
	return (fElements.size());
}

#endif	// CONCURRENCY_H
