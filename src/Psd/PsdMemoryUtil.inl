// Copyright 2011-2020, Molecular Matters GmbH <office@molecular-matters.com>
// See LICENSE.txt for licensing details (2-clause BSD License: https://opensource.org/licenses/BSD-2-Clause)

namespace memoryUtil
{
	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	template <typename T>
	inline T* Allocate(Allocator* allocator)
	{
		PSD_ASSERT_NOT_NULL(allocator);
		static_assert(util::IsPod<T>::value == true, "Type T must be a POD.");

		return static_cast<T*>(allocator->Allocate(sizeof(T), PSD_ALIGN_OF(T)));
	}


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	template <typename T>
	inline T* AllocateArray(Allocator* allocator, size_t count)
	{
		PSD_ASSERT_NOT_NULL(allocator);
		static_assert(util::IsPod<T>::value == true, "Type T must be a POD.");

		return static_cast<T*>(allocator->Allocate(sizeof(T)*count, PSD_ALIGN_OF(T)));
	}


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	template <typename T>
	inline void Free(Allocator* allocator, T*& ptr)
	{
		PSD_ASSERT_NOT_NULL(allocator);

		allocator->Free(ptr);
		ptr = nullptr;
	}


	// ---------------------------------------------------------------------------------------------------------------------
	// ---------------------------------------------------------------------------------------------------------------------
	template <typename T>
	inline void FreeArray(Allocator* allocator, T*& ptr)
	{
		PSD_ASSERT_NOT_NULL(allocator);

		allocator->Free(ptr);
		ptr = nullptr;
	}
}
