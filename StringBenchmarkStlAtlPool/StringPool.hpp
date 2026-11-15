//////////////////////////////////////////////////////////////////////////////////////////
//
// *** String Pool Allocator -- by Giovanni Dicanio <giovanni.dicanio@gmail.com> ***
//
// This is an implementation of a string pool allocator using the Win32 VirtualAlloc API.
//
//////////////////////////////////////////////////////////////////////////////////////////
//
// FILE:    StringPool.hpp
// DESC:    Header file for the string pool allocator
// AUTHOR:  Giovanni Dicanio
//
// -------------------------------------------------------------------------------------
// Last update: 2026, November 14th
//
// Minimum C++ Language Standard: C++17
// IDE: VS 2022
// Code compiles cleanly at /W4.
//
//--------------------------------------------------------------------------------------
// SPDX-License-Identifier: MIT
//////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <crtdbg.h>     // _ASSERTE
#include <wchar.h>      // wcslen, wmemcpy
#include <new>          // std::bad_alloc

#include <Windows.h>    // Windows Platform SDK


//----------------------------------------------------------------------------------------
// String Pool Allocator - Efficiently allocates strings from a custom memory pool
//----------------------------------------------------------------------------------------
class CStringPoolAllocator
{
public:
    // Initialize the string pool allocator
    CStringPoolAllocator() noexcept;

    // Initialize the string pool allocator, passing a value for the minimum chunk size in bytes.
    // Since chunks should be of comfortably large size, consider passing values >= 32000.
    // (The default value for this parameter is 512KB.)
    explicit CStringPoolAllocator(SIZE_T cbMinChunkSize) noexcept;

    // Release the string pool allocator's resources
    ~CStringPoolAllocator() noexcept;

    // Allocate a string deep-copying it from a [begin, end) character interval.
    // [begin, end) is as in common STL convention (i.e. the end pointer is *excluded*).
    // Throw std::bad_alloc on allocation failure.
    [[nodiscard]] PWSTR AllocString(const WCHAR* pchBegin, const WCHAR* pchEnd);

    // Allocate a string deep-copying it from a source NUL-terminated string.
    // Throw std::bad_alloc on allocation failure.
    [[nodiscard]] PWSTR DupString(PCWSTR pszSource);


    //
    // Ban Copy
    //
private:
    CStringPoolAllocator(const CStringPoolAllocator&) = delete;
    CStringPoolAllocator& operator=(const CStringPoolAllocator&) = delete;


    //
    // IMPLEMENTATION
    //
private:

    //
    // Based on The Old New Thing blog post (with many of modifications):
    //
    //      https://devblogs.microsoft.com/oldnewthing/20050519-00/?p=35603
    //      Loading the dictionary, part 6: Taking advantage of our memory allocation pattern
    //

    //
    // This pool allocator basically maintains a singly-linked list of chunks,
    // and string allocations are served carving memory (with a simple pointer increase)
    // from these chunks.
    // When there isn't enough memory in the current chunk, a whole new chunk is allocated
    // to serve the current and following memory allocations.
    //
    //
    //      +--------------+
    //      |   phdrPrev   |   <--- Pointer to previous chunk header (in a singly-linked list)
    //      +--------------+
    //      |    cbSize    |   <--- Total size, in bytes, of the current chunk
    //      +--------------+
    //      |              |
    //      |   Array of   |   <--- Array of WCHARs, used to serve string allocations
    //      |    WCHARs    |        (just increase a pointer in the current allocated block)
    //      |              |
    //      |     ...      |
    //      |              |
    //      +--------------+
    //


    // Temporarily disable the warning C4324 that triggers is 32-bit x86 builds,
    // as the padding for alignment is *intended* behavior.
#pragma warning(push)
#pragma warning(disable: 4324)  // structure was padded due to alignment specifier (intended)

    // 16-byte aligned so that the WCHARs following the header
    // start on a 16-byte boundary on both x86 and x64
    struct alignas(16) ChunkHeader
    {
        // Pointer to the previous chunk in the linked list
        ChunkHeader* phdrPrev;

        // Total size, in bytes, of the current chunk
        SIZE_T  cbSize;
    };

#pragma warning(pop)

/*
    NOTE:
    An alternative could be adding an explicit padding field in the structure in 32-bit builds:

    struct ChunkHeader
    {
        // Pointer to the previous chunk in the linked list
        ChunkHeader* phdrPrev;

        // Total size, in bytes, of the current chunk
        SIZE_T  cbSize;

#ifndef _WIN64
        // On 32-bit x86 the two fields above take 8 bytes:
        // pad to 16 bytes so the strings that follow the header are 16-byte aligned.
        BYTE padding[8];
#endif // _WIN64
    };

*/
    static_assert(sizeof(ChunkHeader) % 16 == 0,
                  "ChunkHeader size must keep the following string data 16-byte aligned");


    // Default value for minimum chunk size, in bytes
    static constexpr int kcbDefaultMinChunkSize = 512 * 1024; // 512KB; was 32000 in the original code

    // Do not accept strings larger than 1 "MB" WCHARs
    // (Since sizeof(WCHAR) == 2 [byte], this limit is basically 2MB)
    static constexpr int kchMaxCharAlloc = 1024 * 1024;


    WCHAR*          m_pchNext       = nullptr;  // First available byte in current chunk
    WCHAR*          m_pchLimit      = nullptr;  // One past last available byte in current chunk
    ChunkHeader*    m_phdrCurrent   = nullptr;  // Current chunk to serve memory allocations
    SIZE_T          m_cbGranularity = 0;        // Allocation granularity for our chunks


    //
    // Helper Methods
    //

    void Destroy() noexcept;

    [[nodiscard]] static SIZE_T RoundUp(SIZE_T cb, SIZE_T units) noexcept;
    [[nodiscard]] SIZE_T GetAllocationGranularity(SIZE_T cbMinChunkSize = kcbDefaultMinChunkSize) noexcept;
};


//========================================================================================
//                          Inline Method Implementations
//========================================================================================

inline CStringPoolAllocator::CStringPoolAllocator() noexcept
    : m_cbGranularity(GetAllocationGranularity())
{
}


inline CStringPoolAllocator::CStringPoolAllocator(SIZE_T cbMinChunkSize) noexcept
    : m_cbGranularity(GetAllocationGranularity(cbMinChunkSize))
{
}


inline CStringPoolAllocator::~CStringPoolAllocator() noexcept
{
    Destroy();
}


inline PWSTR CStringPoolAllocator::DupString(PCWSTR pszSource)
{
    _ASSERTE(pszSource != nullptr);
    return AllocString(pszSource, pszSource + wcslen(pszSource));
}


inline SIZE_T CStringPoolAllocator::RoundUp(SIZE_T cb, SIZE_T units) noexcept
{
    return ((cb + units - 1) / units) * units;
}


inline SIZE_T CStringPoolAllocator::GetAllocationGranularity(SIZE_T cbMinChunkSize) noexcept
{
    // Chunks should have a comfortably large size
    _ASSERTE(cbMinChunkSize >= 32000);


    // For the allocation granularity, choose the next multiple of the system allocation granularity
    // (si.dwAllocationGranularity) that is at least sizeof(ChunkHeader) + kcbMinChunkSize.
    //
    // Note that a chunk is supposed to be a "comfortably large" block of memory.
    // In this way, we enforce a minimum chunk size to avoid having a huge number of tiny chunks.

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return RoundUp(sizeof(ChunkHeader) + cbMinChunkSize,
                   si.dwAllocationGranularity);
}

