/*
 * Copyright 2003, Ingo Weinhold, ingo_weinhold@gmx.de.
 * All rights reserved. Distributed under the terms of the MIT license.
 */
#ifndef _VECTOR_H
#define _VECTOR_H

#include <stdlib.h>
#include <string.h>

#include <SupportDefs.h>
#include <util/kernel_cpp.h>

template<typename Value> class VectorIterator;

// for convenience
#define _VECTOR_TEMPLATE_LIST template<typename Value>
#define _VECTOR_CLASS_NAME Vector<Value>
#define _VECTOR_CLASS_TYPE Vector<Value>

/*!
	\class Vector
	\brief A generic vector implementation.
*/
template<typename Value>
class Vector {
public:
	typedef VectorIterator<Value>		Iterator;
	typedef VectorIterator<const Value>	ConstIterator;

private:
	static const size_t				kDefaultChunkSize = 10;
	static const size_t				kMaximalChunkSize = 1024 * 1024;

public:
	Vector(size_t chunkSize = kDefaultChunkSize);
	~Vector();

	// Copy constructor (existing behavior)
	Vector(const Vector& other);
	Vector& operator=(const Vector& other);

	// C++14 move semantics (without STL dependencies)
	Vector(Vector&& other);
	Vector& operator=(Vector&& other);

	status_t PushFront(const Value &value);
	status_t PushBack(const Value &value);

	void PopFront();
	void PopBack();

	status_t Add(const Value &value) { return PushBack(value); }
	status_t Add(const Value &value, int32 index) { return Insert(value, index); }

	// C++14 EmplaceBack with perfect forwarding (without std::forward)
	template<typename... Args>
	status_t EmplaceBack(Args&&... args);

	// Reserve capacity to avoid multiple reallocations
	status_t Reserve(size_t newCapacity);

	// Free unused capacity
	status_t ShrinkToFit();

	status_t Insert(const Value &value, int32 index);
	status_t Insert(const Value &value, const Iterator &iterator);

	int32 Remove(const Value &value);
	Iterator Erase(int32 index);
	Iterator Erase(const Iterator &iterator);

	inline int32 Count() const;
	inline bool IsEmpty() const;
	void MakeEmpty();

	inline Iterator Begin();
	inline ConstIterator Begin() const;
	inline Iterator End();
	inline ConstIterator End() const;
	inline Iterator Null();
	inline ConstIterator Null() const;
	inline Iterator IteratorForIndex(int32 index);
	inline ConstIterator IteratorForIndex(int32 index) const;

	inline const Value &ElementAt(int32 index) const;
	inline Value &ElementAt(int32 index);

	int32 IndexOf(const Value &value, int32 start = 0) const;
	Iterator Find(const Value &value);
	Iterator Find(const Value &value, const Iterator &start);
	ConstIterator Find(const Value &value) const;
	ConstIterator Find(const Value &value, const ConstIterator &start) const;

	inline Value &operator[](int32 index);
	inline const Value &operator[](int32 index) const;

	// debugging
	int32 GetCapacity() const	{ return fCapacity; }

private:
	inline static void _MoveItems(Value *values, int32 offset, int32 count);
	bool _Resize(size_t count);
	inline int32 _IteratorIndex(const Iterator &iterator) const;
	inline int32 _IteratorIndex(const ConstIterator &iterator) const;

private:
	size_t	fCapacity;
	size_t	fChunkSize;
	int32	fItemCount;
	Value	*fItems;
};


// VectorIterator
template<typename Value>
class VectorIterator {
private:
	typedef VectorIterator<Value>	Iterator;

public:
	inline VectorIterator()
		: fElement(NULL)
	{
	}

	inline VectorIterator(const Iterator &other)
		: fElement(other.fElement)
	{
	}

	inline Iterator &operator++()
	{
		if (fElement)
			++fElement;
		return *this;
	}

	inline Iterator operator++(int)
	{
		Iterator it(*this);
		++*this;
		return it;
	}

	inline Iterator &operator--()
	{
		if (fElement)
			--fElement;
		return *this;
	}

	inline Iterator operator--(int)
	{
		Iterator it(*this);
		--*this;
		return it;
	}

	inline Iterator &operator=(const Iterator &other)
	{
		fElement = other.fElement;
		return *this;
	}


	inline bool operator==(const Iterator &other) const
	{
		return (fElement == other.fElement);
	}

	inline bool operator!=(const Iterator &other) const
	{
		return !(*this == other);
	}

	inline Value &operator*() const
	{
		return *fElement;
	}

	inline Value *operator->() const
	{
		return fElement;
	}

	inline operator bool() const
	{
		return fElement;
	}

// private
public:
	inline VectorIterator(Value *element)
		: fElement(element)
	{
	}

	inline Value *Element() const
	{
		return fElement;
	}

protected:
	Value	*fElement;
};


// Vector

// constructor
/*!	\brief Creates an empty vector.
	\param chunkSize The granularity for the vector's capacity, i.e. the
		   minimal number of elements the capacity grows or shrinks when
		   necessary.
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_NAME::Vector(size_t chunkSize)
	: fCapacity(0),
	  fChunkSize(chunkSize),
	  fItemCount(0),
	  fItems(NULL)
{
	if (fChunkSize == 0 || fChunkSize > kMaximalChunkSize)
		fChunkSize = kDefaultChunkSize;
	_Resize(0);
}

// copy constructor
/*!	\brief Creates a copy of another vector.
	\param other The vector to copy from.
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_NAME::Vector(const Vector& other)
	: fCapacity(0),
	  fChunkSize(other.fChunkSize),
	  fItemCount(0),
	  fItems(NULL)
{
	if (other.fItemCount > 0) {
		if (_Resize(other.fItemCount)) {
			for (int32 i = 0; i < other.fItemCount; i++)
				new(fItems + i) Value(other.fItems[i]);
		}
	}
}

// copy assignment operator
/*!	\brief Assigns the contents of another vector to this one.
	\param other The vector to copy from.
	\return A reference to this vector.
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_TYPE&
_VECTOR_CLASS_NAME::operator=(const Vector& other)
{
	if (this != &other) {
		MakeEmpty();
		if (other.fItemCount > 0) {
			if (_Resize(other.fItemCount)) {
				for (int32 i = 0; i < other.fItemCount; i++)
					new(fItems + i) Value(other.fItems[i]);
			}
		}
	}
	return *this;
}

// move constructor
/*!	\brief Moves resources from another vector (C++14 move semantics).
	\param other The vector to move from. Left in a valid but empty state.
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_NAME::Vector(Vector&& other)
	: fCapacity(other.fCapacity),
	  fChunkSize(other.fChunkSize),
	  fItemCount(other.fItemCount),
	  fItems(other.fItems)
{
	other.fCapacity = 0;
	other.fItemCount = 0;
	other.fItems = NULL;
}

// move assignment operator
/*!	\brief Move assigns from another vector (C++14 move semantics).
	\param other The vector to move from. Left in a valid but empty state.
	\return A reference to this vector.
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_TYPE&
_VECTOR_CLASS_NAME::operator=(Vector&& other)
{
	if (this != &other) {
		// Explicitly call destructors for all existing elements
		for (int32 i = 0; i < fItemCount; i++)
			fItems[i].~Value();
		free(fItems);

		// Move resources from other
		fCapacity = other.fCapacity;
		fChunkSize = other.fChunkSize;
		fItemCount = other.fItemCount;
		fItems = other.fItems;

		// Leave other in valid but empty state
		other.fCapacity = 0;
		other.fItemCount = 0;
		other.fItems = NULL;
	}
	return *this;
}

// destructor
/*!	\brief Frees all resources associated with the object.

	The contained elements are destroyed. Note, that, if the element
	type is a pointer type, only the pointer is destroyed, not the object
	it points to.
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_NAME::~Vector()
{
	MakeEmpty();
	free(fItems);
}

// PushFront
/*!	\brief Inserts a copy of the supplied value at the beginning of the
		   vector.
	\param value The element to be inserted.
	\return
	- \c B_OK: Everything went fine.
	- \c B_NO_MEMORY: Insufficient memory for this operation.
*/
_VECTOR_TEMPLATE_LIST
status_t
_VECTOR_CLASS_NAME::PushFront(const Value &value)
{
	return Insert(value, 0);
}

// PushBack
/*!	\brief Inserts a copy of the supplied value at the end of the vector.
	\param value The element to be inserted.
	\return
	- \c B_OK: Everything went fine.
	- \c B_NO_MEMORY: Insufficient memory for this operation.
*/
_VECTOR_TEMPLATE_LIST
status_t
_VECTOR_CLASS_NAME::PushBack(const Value &value)
{
	return Insert(value, fItemCount);
}

// PopFront
/*!	\brief Removes the first element of the vector.

	Invocation on an empty vector is harmless.
*/
_VECTOR_TEMPLATE_LIST
void
_VECTOR_CLASS_NAME::PopFront()
{
	if (fItemCount > 0)
		Erase(0);
}

// PopBack
/*!	\brief Removes the last element of the vector.

	Invocation on an empty vector is harmless.
*/
_VECTOR_TEMPLATE_LIST
void
_VECTOR_CLASS_NAME::PopBack()
{
	if (fItemCount > 0)
		Erase(fItemCount - 1);
}

// _MoveItems
/*!	\brief Moves elements within an array.
	\param items The elements to be moved.
	\param offset The index to which the elements shall be moved. May be
		   negative.
	\param count The number of elements to be moved.
*/
_VECTOR_TEMPLATE_LIST
inline
void
_VECTOR_CLASS_NAME::_MoveItems(Value* items, int32 offset, int32 count)
{
	if (count > 0 && offset != 0)
		memmove(items + offset, items, count * sizeof(Value));
}

// Insert
/*!	\brief Inserts a copy of the the supplied value at the given index.
	\param value The value to be inserted.
	\param index The index at which to insert the new element. It must
		   hold: 0 <= \a index <= Count().
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \a index is out of range.
	- \c B_NO_MEMORY: Insufficient memory for this operation.
*/
_VECTOR_TEMPLATE_LIST
status_t
_VECTOR_CLASS_NAME::Insert(const Value &value, int32 index)
{
	if (index < 0 || index > fItemCount)
		return B_BAD_VALUE;
	if (!_Resize(fItemCount + 1))
		return B_NO_MEMORY;
	_MoveItems(fItems + index, 1, fItemCount - index - 1);
	new(fItems + index) Value(value);
	return B_OK;
}

// Insert
/*!	\brief Inserts a copy of the the supplied value at the given position.
	\param value The value to be inserted.
	\param iterator An iterator specifying the position at which to insert
		   the new element.
	\return
	- \c B_OK: Everything went fine.
	- \c B_BAD_VALUE: \a iterator is is invalid.
	- \c B_NO_MEMORY: Insufficient memory for this operation.
*/
_VECTOR_TEMPLATE_LIST
status_t
_VECTOR_CLASS_NAME::Insert(const Value &value, const Iterator &iterator)
{
	int32 index = _IteratorIndex(iterator);
	if (index >= 0)
		return Insert(value, index);
	return B_BAD_VALUE;
}

// Remove
/*!	\brief Removes all elements of the supplied value.
	\param value The value of the elements to be removed.
	\return The number of removed occurrences.
*/
_VECTOR_TEMPLATE_LIST
int32
_VECTOR_CLASS_NAME::Remove(const Value &value)
{
	int32 count = 0;
	for (int32 i = fItemCount - 1; i >= 0; i--) {
		if (ElementAt(i) == value) {
			Erase(i);
			count++;
		}
	}
	return count;
}

// Erase
/*!	\brief Removes the element at the given index.
	\param index The position of the element to be removed.
	\return An iterator referring to the element now being located at index
			\a index (End(), if it was the last element that has been
			removed), or Null(), if \a index was out of range.
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_TYPE::Iterator
_VECTOR_CLASS_NAME::Erase(int32 index)
{
	if (index >= 0 && index < fItemCount) {
		fItems[index].~Value();
		_MoveItems(fItems + index + 1, -1, fItemCount - index - 1);
		_Resize(fItemCount - 1);
		return Iterator(fItems + index);
	}
	return Null();
}

// Erase
/*!	\brief Removes the element at the given position.
	\param iterator An iterator referring to the element to be removed.
	\return An iterator referring to the element succeeding the removed
			one (End(), if it was the last element that has been
			removed), or Null(), if \a iterator was an invalid iterator
			(in this case including End()).
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_TYPE::Iterator
_VECTOR_CLASS_NAME::Erase(const Iterator &iterator)
{
	int32 index = _IteratorIndex(iterator);
	if (index >= 0 && index < fItemCount)
		return Erase(index);
	return Null();
}

// Count
/*!	\brief Returns the number of elements the vector contains.
	\return The number of elements the vector contains.
*/
_VECTOR_TEMPLATE_LIST
inline
int32
_VECTOR_CLASS_NAME::Count() const
{
	return fItemCount;
}

// IsEmpty
/*!	\brief Returns whether the vector is empty.
	\return \c true, if the vector is empty, \c false otherwise.
*/
_VECTOR_TEMPLATE_LIST
inline
bool
_VECTOR_CLASS_NAME::IsEmpty() const
{
	return (fItemCount == 0);
}

// MakeEmpty
/*!	\brief Removes all elements from the vector.
*/
_VECTOR_TEMPLATE_LIST
void
_VECTOR_CLASS_NAME::MakeEmpty()
{
	for (int32 i = 0; i < fItemCount; i++)
		fItems[i].~Value();
	_Resize(0);
}

// Begin
/*!	\brief Returns an iterator referring to the beginning of the vector.

	If the vector is not empty, Begin() refers to its first element,
	otherwise it is equal to End() and must not be dereferenced!

	\return An iterator referring to the beginning of the vector.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::Iterator
_VECTOR_CLASS_NAME::Begin()
{
	return Iterator(fItems);
}

// Begin
/*!	\brief Returns an iterator referring to the beginning of the vector.

	If the vector is not empty, Begin() refers to its first element,
	otherwise it is equal to End() and must not be dereferenced!

	\return An iterator referring to the beginning of the vector.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::ConstIterator
_VECTOR_CLASS_NAME::Begin() const
{
	return ConstIterator(fItems);
}

// End
/*!	\brief Returns an iterator referring to the end of the vector.

	The position identified by End() is the one succeeding the last
	element, i.e. it must not be dereferenced!

	\return An iterator referring to the end of the vector.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::Iterator
_VECTOR_CLASS_NAME::End()
{
	return Iterator(fItems + fItemCount);
}

// End
/*!	\brief Returns an iterator referring to the end of the vector.

	The position identified by End() is the one succeeding the last
	element, i.e. it must not be dereferenced!

	\return An iterator referring to the end of the vector.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::ConstIterator
_VECTOR_CLASS_NAME::End() const
{
	return ConstIterator(fItems + fItemCount);
}

// Null
/*!	\brief Returns an invalid iterator.

	Null() is used as a return value, if something went wrong. It must
	neither be incremented or decremented nor dereferenced!

	\return An invalid iterator.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::Iterator
_VECTOR_CLASS_NAME::Null()
{
	return Iterator(NULL);
}

// Null
/*!	\brief Returns an invalid iterator.

	Null() is used as a return value, if something went wrong. It must
	neither be incremented or decremented nor dereferenced!

	\return An invalid iterator.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::ConstIterator
_VECTOR_CLASS_NAME::Null() const
{
	return ConstIterator(NULL);
}

// IteratorForIndex
/*!	\brief Returns an iterator for a given index.
	\return An iterator referring to the same element as \a index, or
			End(), if \a index is out of range.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::Iterator
_VECTOR_CLASS_NAME::IteratorForIndex(int32 index)
{
	if (index >= 0 && index <= fItemCount)
		return Iterator(fItems + index);
	return End();
}

// IteratorForIndex
/*!	\brief Returns an iterator for a given index.
	\return An iterator referring to the same element as \a index, or
			End(), if \a index is out of range.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::ConstIterator
_VECTOR_CLASS_NAME::IteratorForIndex(int32 index) const
{
	if (index >= 0 && index <= fItemCount)
		return ConstIterator(fItems + index);
	return End();
}

// ElementAt
/*!	\brief Returns the element at a given index.
	\param index The index identifying the element to be returned.
	\return The element identified by the given index.
*/
_VECTOR_TEMPLATE_LIST
inline
const Value &
_VECTOR_CLASS_NAME::ElementAt(int32 index) const
{
	if (index >= 0 && index < fItemCount)
		return fItems[index];
	// Return the 0th element by default. Unless the allocation failed, there
	// is always a 0th element -- uninitialized perhaps.
	return fItems[0];
}

// ElementAt
/*!	\brief Returns the element at a given index.
	\param index The index identifying the element to be returned.
	\return The element identified by the given index.
*/
_VECTOR_TEMPLATE_LIST
inline
Value &
_VECTOR_CLASS_NAME::ElementAt(int32 index)
{
	if (index >= 0 && index < fItemCount)
		return fItems[index];
	// Return the 0th element by default. Unless the allocation failed, there
	// is always a 0th element -- uninitialized perhaps.
	return fItems[0];
}

// IndexOf
/*!	\brief Returns the index of the next element with the specified value.
	\param value The value of the element to be found.
	\param start The index at which to be started to search for the element.
	\return The index of the found element, or \c -1, if no further element
			with the given value could be found or \a index is out of range.
*/
_VECTOR_TEMPLATE_LIST
int32
_VECTOR_CLASS_NAME::IndexOf(const Value &value, int32 start) const
{
	if (start >= 0) {
		for (int32 i = start; i < fItemCount; i++) {
			if (fItems[i] == value)
				return i;
		}
	}
	return -1;
}

// Find
/*!	\brief Returns an iterator referring to the next element with the
		   specified value.
	\param value The value of the element to be found.
	\return An iterator referring to the found element, or End(), if no
			further with the given value could be found.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::Iterator
_VECTOR_CLASS_NAME::Find(const Value &value)
{
	return Find(value, Begin());
}

// Find
/*!	\brief Returns an iterator referring to the next element with the
		   specified value.
	\param value The value of the element to be found.
	\param start And iterator specifying where to start searching for the
		   element.
	\return An iterator referring to the found element, or End(), if no
			further with the given value could be found or \a start was
			invalid.
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_TYPE::Iterator
_VECTOR_CLASS_NAME::Find(const Value &value, const Iterator &start)
{
	int32 index = IndexOf(value, _IteratorIndex(start));
	if (index >= 0)
		return Iterator(fItems + index);
	return End();
}

// Find
/*!	\brief Returns an iterator referring to the of the next element with the
		   specified value.
	\param value The value of the element to be found.
	\return An iterator referring to the found element, or End(), if no
			further with the given value could be found.
*/
_VECTOR_TEMPLATE_LIST
inline
_VECTOR_CLASS_TYPE::ConstIterator
_VECTOR_CLASS_NAME::Find(const Value &value) const
{
	return Find(value, Begin());
}

// Find
/*!	\brief Returns an iterator referring to the of the next element with the
		   specified value.
	\param value The value of the element to be found.
	\param start And iterator specifying where to start searching for the
		   element.
	\return An iterator referring to the found element, or End(), if no
			further with the given value could be found or \a start was
			invalid.
*/
_VECTOR_TEMPLATE_LIST
_VECTOR_CLASS_TYPE::ConstIterator
_VECTOR_CLASS_NAME::Find(const Value &value, const ConstIterator &start) const
{
	int32 index = IndexOf(value, _IteratorIndex(start));
	if (index >= 0)
		return ConstIterator(fItems + index);
	return End();
}

// []
/*!	\brief Semantically equivalent to ElementAt().
*/
_VECTOR_TEMPLATE_LIST
inline
Value &
_VECTOR_CLASS_NAME::operator[](int32 index)
{
	return ElementAt(index);
}

// []
/*!	\brief Semantically equivalent to ElementAt().
*/
_VECTOR_TEMPLATE_LIST
inline
const Value &
_VECTOR_CLASS_NAME::operator[](int32 index) const
{
	return ElementAt(index);
}

// _Resize
/*!	\brief Resizes the vector.

	The internal element array will be grown or shrunk to the next multiple
	of \a fChunkSize >= \a count, but no less than \a fChunkSize.

	Also adjusts \a fItemCount according to the supplied \a count, but does
	not invoke a destructor or constructor on any element.

	\param count The number of element.
	\return \c true, if everything went fine, \c false, if the memory
			allocation failed.
*/
_VECTOR_TEMPLATE_LIST
bool
_VECTOR_CLASS_NAME::_Resize(size_t count)
{
	bool result = true;
	// calculate the new capacity
	int32 newSize = count;
	if (newSize <= 0)
		newSize = 1;
	newSize = ((newSize - 1) / fChunkSize + 1) * fChunkSize;
	// resize if necessary
	if ((size_t)newSize != fCapacity) {
		Value* newItems = (Value*)realloc(fItems, newSize * sizeof(Value));
		if (newItems) {
			fItems = newItems;
			fCapacity = newSize;
		} else
			result = false;
	}
	if (result)
		fItemCount = count;
	return result;
}

// _IteratorIndex
/*!	\brief Returns index of the element the supplied iterator refers to.
	\return The index of the element the supplied iterator refers to, or
			\c -1, if the iterator is invalid (End() is considered valid
			here, and Count() is returned).
*/
_VECTOR_TEMPLATE_LIST
inline
int32
_VECTOR_CLASS_NAME::_IteratorIndex(const Iterator &iterator) const
{
	if (iterator.Element()) {
		int32 index = iterator.Element() - fItems;
		if (index >= 0 && index <= fItemCount)
			return index;
	}
	return -1;
}

// _IteratorIndex
/*!	\brief Returns index of the element the supplied iterator refers to.
	\return The index of the element the supplied iterator refers to, or
			\c -1, if the iterator is invalid (End() is considered valid
			here, and Count() is returned).
*/
_VECTOR_TEMPLATE_LIST
inline
int32
_VECTOR_CLASS_NAME::_IteratorIndex(const ConstIterator &iterator) const
{
	if (iterator.Element()) {
		int32 index = iterator.Element() - fItems;
		if (index >= 0 && index <= fItemCount)
			return index;
	}
	return -1;
}

// EmplaceBack
/*!	\brief Constructs an element in-place at the end of the vector.

	Uses C++14 perfect forwarding without std::forward to construct the
	element directly in the vector, avoiding temporary object creation.

	\param args Arguments to be forwarded to the Value constructor.
	\return
	- \c B_OK: Everything went fine.
	- \c B_NO_MEMORY: Insufficient memory for this operation.
*/
_VECTOR_TEMPLATE_LIST
template<typename... Args>
status_t
_VECTOR_CLASS_NAME::EmplaceBack(Args&&... args)
{
	if (fItemCount >= (int32)fCapacity) {
		if (!_Resize(fItemCount + 1))
			return B_NO_MEMORY;
	} else {
		fItemCount++;
	}

	// Placement new with perfect forwarding (no std::forward needed)
	// The && in the template parameter already provides forwarding semantics
	new(fItems + fItemCount - 1) Value(static_cast<Args&&>(args)...);

	return B_OK;
}

// Reserve
/*!	\brief Reserves capacity for at least the specified number of elements.

	This can be used to avoid multiple reallocations when the final size
	is known in advance. Does nothing if the requested capacity is less
	than or equal to the current capacity.

	\param newCapacity The minimum capacity to reserve.
	\return
	- \c B_OK: Everything went fine.
	- \c B_NO_MEMORY: Insufficient memory for this operation.
*/
_VECTOR_TEMPLATE_LIST
status_t
_VECTOR_CLASS_NAME::Reserve(size_t newCapacity)
{
	if (newCapacity <= fCapacity)
		return B_OK;

	// Calculate properly aligned capacity
	size_t alignedCapacity = ((newCapacity - 1) / fChunkSize + 1) * fChunkSize;

	Value* newItems = (Value*)realloc(fItems, alignedCapacity * sizeof(Value));
	if (newItems == NULL)
		return B_NO_MEMORY;

	fItems = newItems;
	fCapacity = alignedCapacity;
	return B_OK;
}

// ShrinkToFit
/*!	\brief Reduces capacity to match the current size.

	Releases unused memory by shrinking the capacity to exactly fit the
	current number of elements. This is useful after removing many elements
	to reclaim memory.

	\return
	- \c B_OK: Everything went fine.
	- \c B_NO_MEMORY: Insufficient memory for this operation (original
	  capacity is retained in this case).
*/
_VECTOR_TEMPLATE_LIST
status_t
_VECTOR_CLASS_NAME::ShrinkToFit()
{
	if (fItemCount >= (int32)fCapacity)
		return B_OK;  // Already at minimum capacity

	size_t targetCapacity = fItemCount;
	if (targetCapacity == 0)
		targetCapacity = 1;  // Keep at least one element allocated

	// Align to chunk size
	targetCapacity = ((targetCapacity - 1) / fChunkSize + 1) * fChunkSize;

	if (targetCapacity >= fCapacity)
		return B_OK;  // No shrinking needed after alignment

	Value* newItems = (Value*)realloc(fItems, targetCapacity * sizeof(Value));
	if (newItems == NULL)
		return B_NO_MEMORY;  // Keep original capacity on failure

	fItems = newItems;
	fCapacity = targetCapacity;
	return B_OK;
}

#endif	// _VECTOR_H
