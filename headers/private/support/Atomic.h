/*
 * Copyright 2024, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * C++14-compatible atomic wrapper for Haiku
 * Note: This is NOT std::atomic - uses BPrivate namespace
 */
#ifndef _HAIKU_BPRIVATE_ATOMIC_H_
#define _HAIKU_BPRIVATE_ATOMIC_H_

#include <SupportDefs.h>

#ifdef __cplusplus

#include <type_traits>
#include <cstring>  // for std::memcpy

namespace BPrivate {

/* Memory ordering enum - C++14 compatible */
enum class MemoryOrder {
	Relaxed = B_MEMORY_ORDER_RELAXED,
	Consume = B_MEMORY_ORDER_CONSUME,
	Acquire = B_MEMORY_ORDER_ACQUIRE,
	Release = B_MEMORY_ORDER_RELEASE,
	AcqRel = B_MEMORY_ORDER_ACQ_REL,
	SeqCst = B_MEMORY_ORDER_SEQ_CST
};

/* Atomic flag - lock-free primitive required by C++11 */
class AtomicFlag
{
private:
	int32 fFlag;
	
public:
	AtomicFlag() noexcept : fFlag(0) {}
	
	AtomicFlag(const AtomicFlag&) = delete;
	AtomicFlag& operator=(const AtomicFlag&) = delete;
	
	bool TestAndSet(MemoryOrder order = MemoryOrder::SeqCst) noexcept
	{
		return atomic_exchange_ordered(&fFlag, 1, 
		                              static_cast<memory_order_t>(order)) != 0;
	}
	
	void Clear(MemoryOrder order = MemoryOrder::SeqCst) noexcept
	{
		atomic_store_ordered(&fFlag, 0, static_cast<memory_order_t>(order));
	}
};

/* Primary template - only enabled for 4 and 8 byte types
 * Uses C++14 compatible tag dispatch pattern instead of C++17 constexpr if */
template<typename T>
class Atomic
{
private:
	static_assert(sizeof(T) == 4 || sizeof(T) == 8, 
	             "Atomic only supports 4 or 8 byte types");
	
#if __GNUC__ > 5 || defined(__clang__)
	// Only check is_trivially_copyable if compiler supports it
	static_assert(std::is_trivially_copyable<T>::value, 
	             "Atomic type must be trivially copyable");
#endif
	
	/* Use aligned storage to avoid strict aliasing issues
	 * This ensures proper alignment for atomic operations */
	typename std::aligned_storage<sizeof(T), sizeof(T)>::type fStorage;
	
public:
	Atomic() noexcept = default;
	
	Atomic(T desired) noexcept
	{
		Store(desired);
	}
	
	Atomic(const Atomic&) = delete;
	Atomic& operator=(const Atomic&) = delete;
	
	/* C++14 compatible Load - uses tag dispatch instead of constexpr if */
	T Load(MemoryOrder order = MemoryOrder::SeqCst) const noexcept
	{
		return LoadImpl(order, std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* C++14 compatible Store - uses tag dispatch */
	void Store(T desired, MemoryOrder order = MemoryOrder::SeqCst) noexcept
	{
		StoreImpl(desired, order, std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* Exchange operation */
	T Exchange(T desired, MemoryOrder order = MemoryOrder::SeqCst) noexcept
	{
		return ExchangeImpl(desired, order, std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* Compare-exchange weak (can spuriously fail) */
	bool CompareExchangeWeak(T& expected, T desired,
	                        MemoryOrder success = MemoryOrder::SeqCst,
	                        MemoryOrder failure = MemoryOrder::SeqCst) noexcept
	{
		return CasWeakImpl(expected, desired, success, failure,
		                  std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* Compare-exchange strong (never spuriously fails) */
	bool CompareExchangeStrong(T& expected, T desired,
	                          MemoryOrder success = MemoryOrder::SeqCst,
	                          MemoryOrder failure = MemoryOrder::SeqCst) noexcept
	{
		return CasStrongImpl(expected, desired, success, failure,
		                    std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* Operators for convenience */
	T operator=(T desired) noexcept
	{
		Store(desired);
		return desired;
	}
	
	operator T() const noexcept
	{
		return Load();
	}
	
private:
	/* Load implementation for 4-byte types using tag dispatch */
	T LoadImpl(MemoryOrder order, std::integral_constant<size_t, 4>) const noexcept
	{
		int32 temp = atomic_load_ordered(
		    reinterpret_cast<int32*>(const_cast<decltype(fStorage)*>(&fStorage)),
		    static_cast<memory_order_t>(order));
		T result;
		std::memcpy(&result, &temp, sizeof(T));
		return result;
	}
	
	/* Load implementation for 8-byte types using tag dispatch */
	T LoadImpl(MemoryOrder order, std::integral_constant<size_t, 8>) const noexcept
	{
		int64 temp = atomic_load64_ordered(
		    reinterpret_cast<int64*>(const_cast<decltype(fStorage)*>(&fStorage)),
		    static_cast<memory_order_t>(order));
		T result;
		std::memcpy(&result, &temp, sizeof(T));
		return result;
	}
	
	/* Store implementation for 4-byte types */
	void StoreImpl(T desired, MemoryOrder order, 
	              std::integral_constant<size_t, 4>) noexcept
	{
		int32 temp;
		std::memcpy(&temp, &desired, sizeof(T));
		atomic_store_ordered(
		    reinterpret_cast<int32*>(&fStorage),
		    temp,
		    static_cast<memory_order_t>(order));
	}
	
	/* Store implementation for 8-byte types */
	void StoreImpl(T desired, MemoryOrder order,
	              std::integral_constant<size_t, 8>) noexcept
	{
		int64 temp;
		std::memcpy(&temp, &desired, sizeof(T));
		atomic_store64_ordered(
		    reinterpret_cast<int64*>(&fStorage),
		    temp,
		    static_cast<memory_order_t>(order));
	}
	
	/* Exchange implementation for 4-byte types */
	T ExchangeImpl(T desired, MemoryOrder order,
	              std::integral_constant<size_t, 4>) noexcept
	{
		int32 des_temp;
		std::memcpy(&des_temp, &desired, sizeof(T));
		
		int32 old_temp = atomic_exchange_ordered(
		    reinterpret_cast<int32*>(&fStorage),
		    des_temp,
		    static_cast<memory_order_t>(order));
		
		T result;
		std::memcpy(&result, &old_temp, sizeof(T));
		return result;
	}
	
	/* Exchange implementation for 8-byte types */
	T ExchangeImpl(T desired, MemoryOrder order,
	              std::integral_constant<size_t, 8>) noexcept
	{
		int64 des_temp;
		std::memcpy(&des_temp, &desired, sizeof(T));
		
		int64 old_temp = atomic_exchange64_ordered(
		    reinterpret_cast<int64*>(&fStorage),
		    des_temp,
		    static_cast<memory_order_t>(order));
		
		T result;
		std::memcpy(&result, &old_temp, sizeof(T));
		return result;
	}
	
	/* Weak CAS implementation for 4-byte types */
	bool CasWeakImpl(T& expected, T desired, MemoryOrder success, MemoryOrder failure,
	                std::integral_constant<size_t, 4>) noexcept
	{
		int32 exp_temp, des_temp;
		std::memcpy(&exp_temp, &expected, sizeof(T));
		std::memcpy(&des_temp, &desired, sizeof(T));
		
		bool result = atomic_compare_exchange_weak_ordered(
		    reinterpret_cast<int32*>(&fStorage),
		    &exp_temp,
		    des_temp,
		    static_cast<memory_order_t>(success),
		    static_cast<memory_order_t>(failure));
		
		std::memcpy(&expected, &exp_temp, sizeof(T));
		return result;
	}
	
	/* Weak CAS implementation for 8-byte types */
	bool CasWeakImpl(T& expected, T desired, MemoryOrder success, MemoryOrder failure,
	                std::integral_constant<size_t, 8>) noexcept
	{
		int64 exp_temp, des_temp;
		std::memcpy(&exp_temp, &expected, sizeof(T));
		std::memcpy(&des_temp, &desired, sizeof(T));
		
		bool result = atomic_compare_exchange_weak64_ordered(
		    reinterpret_cast<int64*>(&fStorage),
		    &exp_temp,
		    des_temp,
		    static_cast<memory_order_t>(success),
		    static_cast<memory_order_t>(failure));
		
		std::memcpy(&expected, &exp_temp, sizeof(T));
		return result;
	}
	
	/* Strong CAS implementation for 4-byte types */
	bool CasStrongImpl(T& expected, T desired, MemoryOrder success, MemoryOrder failure,
	                  std::integral_constant<size_t, 4>) noexcept
	{
		int32 exp_temp, des_temp;
		std::memcpy(&exp_temp, &expected, sizeof(T));
		std::memcpy(&des_temp, &desired, sizeof(T));
		
		bool result = atomic_compare_exchange_strong_ordered(
		    reinterpret_cast<int32*>(&fStorage),
		    &exp_temp,
		    des_temp,
		    static_cast<memory_order_t>(success),
		    static_cast<memory_order_t>(failure));
		
		std::memcpy(&expected, &exp_temp, sizeof(T));
		return result;
	}
	
	/* Strong CAS implementation for 8-byte types */
	bool CasStrongImpl(T& expected, T desired, MemoryOrder success, MemoryOrder failure,
	                  std::integral_constant<size_t, 8>) noexcept
	{
		int64 exp_temp, des_temp;
		std::memcpy(&exp_temp, &expected, sizeof(T));
		std::memcpy(&des_temp, &desired, sizeof(T));
		
		bool result = atomic_compare_exchange_strong64_ordered(
		    reinterpret_cast<int64*>(&fStorage),
		    &exp_temp,
		    des_temp,
		    static_cast<memory_order_t>(success),
		    static_cast<memory_order_t>(failure));
		
		std::memcpy(&expected, &exp_temp, sizeof(T));
		return result;
	}
};

/* Specialization for integral types to add arithmetic operations */
template<typename T>
class AtomicIntegral : public Atomic<T>
{
private:
	static_assert(std::is_integral<T>::value, "AtomicIntegral requires integral type");
	
public:
	using Atomic<T>::Atomic;
	using Atomic<T>::operator=;
	
	/* Fetch-and-add operation */
	T FetchAdd(T arg, MemoryOrder order = MemoryOrder::SeqCst) noexcept
	{
		return FetchAddImpl(arg, order, std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* Fetch-and-sub operation */
	T FetchSub(T arg, MemoryOrder order = MemoryOrder::SeqCst) noexcept
	{
		return FetchSubImpl(arg, order, std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* Fetch-and-and operation */
	T FetchAnd(T arg, MemoryOrder order = MemoryOrder::SeqCst) noexcept
	{
		return FetchAndImpl(arg, order, std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* Fetch-and-or operation */
	T FetchOr(T arg, MemoryOrder order = MemoryOrder::SeqCst) noexcept
	{
		return FetchOrImpl(arg, order, std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* Fetch-and-xor operation */
	T FetchXor(T arg, MemoryOrder order = MemoryOrder::SeqCst) noexcept
	{
		return FetchXorImpl(arg, order, std::integral_constant<size_t, sizeof(T)>());
	}
	
	/* Convenience operators */
	T operator++(int) noexcept { return FetchAdd(1); }
	T operator--(int) noexcept { return FetchSub(1); }
	T operator++() noexcept { return FetchAdd(1) + 1; }
	T operator--() noexcept { return FetchSub(1) - 1; }
	T operator+=(T arg) noexcept { return FetchAdd(arg) + arg; }
	T operator-=(T arg) noexcept { return FetchSub(arg) - arg; }
	T operator&=(T arg) noexcept { return FetchAnd(arg) & arg; }
	T operator|=(T arg) noexcept { return FetchOr(arg) | arg; }
	T operator^=(T arg) noexcept { return FetchXor(arg) ^ arg; }
	
private:
	/* FetchAdd implementation for 4-byte types */
	T FetchAddImpl(T arg, MemoryOrder order,
	              std::integral_constant<size_t, 4>) noexcept
	{
		int32 arg_temp;
		std::memcpy(&arg_temp, &arg, sizeof(T));
		
		int32 old = atomic_fetch_add_ordered(
		    reinterpret_cast<int32*>(&this->fStorage),
		    arg_temp,
		    static_cast<memory_order_t>(order));
		
		T result;
		std::memcpy(&result, &old, sizeof(T));
		return result;
	}
	
	/* FetchAdd implementation for 8-byte types - REQUIRES 64-bit fetch_add API */
	T FetchAddImpl(T arg, MemoryOrder order,
	              std::integral_constant<size_t, 8>) noexcept
	{
		/* Note: This requires atomic_fetch_add64_ordered to be implemented
		 * For now, fall back to CAS loop */
		T expected = this->Load(MemoryOrder::Relaxed);
		while (!this->CompareExchangeWeak(expected, expected + arg, order, MemoryOrder::Relaxed)) {
			// Retry on spurious failure or value change
		}
		return expected;
	}
	
	/* FetchSub implementation for 4-byte types */
	T FetchSubImpl(T arg, MemoryOrder order,
	              std::integral_constant<size_t, 4>) noexcept
	{
		int32 arg_temp;
		std::memcpy(&arg_temp, &arg, sizeof(T));
		
		int32 old = atomic_fetch_sub_ordered(
		    reinterpret_cast<int32*>(&this->fStorage),
		    arg_temp,
		    static_cast<memory_order_t>(order));
		
		T result;
		std::memcpy(&result, &old, sizeof(T));
		return result;
	}
	
	/* FetchSub implementation for 8-byte types - uses CAS loop */
	T FetchSubImpl(T arg, MemoryOrder order,
	              std::integral_constant<size_t, 8>) noexcept
	{
		T expected = this->Load(MemoryOrder::Relaxed);
		while (!this->CompareExchangeWeak(expected, expected - arg, order, MemoryOrder::Relaxed)) {
			// Retry
		}
		return expected;
	}
	
	/* FetchAnd implementation for 4-byte types */
	T FetchAndImpl(T arg, MemoryOrder order,
	              std::integral_constant<size_t, 4>) noexcept
	{
		int32 arg_temp;
		std::memcpy(&arg_temp, &arg, sizeof(T));
		
		int32 old = atomic_fetch_and_ordered(
		    reinterpret_cast<int32*>(&this->fStorage),
		    arg_temp,
		    static_cast<memory_order_t>(order));
		
		T result;
		std::memcpy(&result, &old, sizeof(T));
		return result;
	}
	
	/* FetchAnd implementation for 8-byte types - uses CAS loop */
	T FetchAndImpl(T arg, MemoryOrder order,
	              std::integral_constant<size_t, 8>) noexcept
	{
		T expected = this->Load(MemoryOrder::Relaxed);
		while (!this->CompareExchangeWeak(expected, expected & arg, order, MemoryOrder::Relaxed)) {
			// Retry
		}
		return expected;
	}
	
	/* FetchOr implementation for 4-byte types */
	T FetchOrImpl(T arg, MemoryOrder order,
	             std::integral_constant<size_t, 4>) noexcept
	{
		int32 arg_temp;
		std::memcpy(&arg_temp, &arg, sizeof(T));
		
		int32 old = atomic_fetch_or_ordered(
		    reinterpret_cast<int32*>(&this->fStorage),
		    arg_temp,
		    static_cast<memory_order_t>(order));
		
		T result;
		std::memcpy(&result, &old, sizeof(T));
		return result;
	}
	
	/* FetchOr implementation for 8-byte types - uses CAS loop */
	T FetchOrImpl(T arg, MemoryOrder order,
	             std::integral_constant<size_t, 8>) noexcept
	{
		T expected = this->Load(MemoryOrder::Relaxed);
		while (!this->CompareExchangeWeak(expected, expected | arg, order, MemoryOrder::Relaxed)) {
			// Retry
		}
		return expected;
	}
	
	/* FetchXor implementation for 4-byte types */
	T FetchXorImpl(T arg, MemoryOrder order,
	              std::integral_constant<size_t, 4>) noexcept
	{
		int32 arg_temp;
		std::memcpy(&arg_temp, &arg, sizeof(T));
		
		int32 old = atomic_fetch_xor_ordered(
		    reinterpret_cast<int32*>(&this->fStorage),
		    arg_temp,
		    static_cast<memory_order_t>(order));
		
		T result;
		std::memcpy(&result, &old, sizeof(T));
		return result;
	}
	
	/* FetchXor implementation for 8-byte types - uses CAS loop */
	T FetchXorImpl(T arg, MemoryOrder order,
	              std::integral_constant<size_t, 8>) noexcept
	{
		T expected = this->Load(MemoryOrder::Relaxed);
		while (!this->CompareExchangeWeak(expected, expected ^ arg, order, MemoryOrder::Relaxed)) {
			// Retry
		}
		return expected;
	}
};

/* Type aliases using Haiku naming conventions */
typedef Atomic<bool>          AtomicBool;
typedef AtomicIntegral<int32>         AtomicInt32;
typedef AtomicIntegral<int64>         AtomicInt64;
typedef AtomicIntegral<uint32>        AtomicUInt32;
typedef AtomicIntegral<uint64>        AtomicUInt64;

} // namespace BPrivate

#endif // __cplusplus

#endif /* _HAIKU_BPRIVATE_ATOMIC_H_ */
