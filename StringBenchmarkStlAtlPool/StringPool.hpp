//////////////////////////////////////////////////////////////////////////////////////////
//
// *** String Pool Allocator -- by Giovanni Dicanio <giovanni.dicanio@gmail.com> ***
//
// This is an implementation of a string pool allocator using the Win32 VirtualAlloc API.
//
//////////////////////////////////////////////////////////////////////////////////////////
//
// FILE:    StringPool.hpp
// DESC:    Header-only implementation for the string pool allocator
// AUTHOR:  Giovanni Dicanio
//
// -------------------------------------------------------------------------------------
// Last update: 2026, November 20th
//
// Minimum C++ Language Standard: C++17 (Compiles cleanly also in C++20 mode.)
// IDE: VS 2022
// Code compiles cleanly at /W4.
//
//--------------------------------------------------------------------------------------
// SPDX-License-Identifier: MIT
//////////////////////////////////////////////////////////////////////////////////////////


#pragma once


//----------------------------------------------------------------------------------------
//                                      Includes
//----------------------------------------------------------------------------------------

#include <Windows.h>    // Windows Platform SDK

#include <crtdbg.h>     // _ASSERTE
#include <wchar.h>      // wcslen, wmemcpy

#include <new>          // std::bad_alloc
#include <utility>      // std::move, std::exchange



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

    // Releases all the memory resources allocated by the pool:
    // every string previously allocated becomes invalid.
    void Destroy() noexcept;


    //
    // This pool allocator is movable but no copyable:
    // When moved, strings stay valid, but are owned by the destination allocator.
    //

    CStringPoolAllocator(CStringPoolAllocator&& other) noexcept;
    CStringPoolAllocator& operator=(CStringPoolAllocator&& other) noexcept;


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


inline CStringPoolAllocator::CStringPoolAllocator(CStringPoolAllocator&& other) noexcept
    : m_pchNext(std::exchange(other.m_pchNext, nullptr))
    , m_pchLimit(std::exchange(other.m_pchLimit, nullptr))
    , m_phdrCurrent(std::exchange(other.m_phdrCurrent, nullptr))
    , m_cbGranularity(other.m_cbGranularity)
{
}


inline CStringPoolAllocator& CStringPoolAllocator::operator=(CStringPoolAllocator&& other) noexcept
{
    if (this != &other)
    {
        // Release the chunks currently owned by this pool.
        // NOTE: Strings previously allocated by this pool become invalid.
        Destroy();

        m_pchNext       = std::exchange(other.m_pchNext, nullptr);
        m_pchLimit      = std::exchange(other.m_pchLimit, nullptr);
        m_phdrCurrent   = std::exchange(other.m_phdrCurrent, nullptr);
        m_cbGranularity = other.m_cbGranularity;
    }

    return*this;
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


//
// GIOVANNI_DICANIO_STRINGPOOL_ALIGN_STRINGS toggle:
//
//   1 = align each string to 16 bytes      <-- this is the default
//   0 = packed strings (initial code)
//
// NOTE:
// When building the pool allocator with 16-byte aligned strings,
// I noted speed increase in benchmark results.
// This may be caused by functions like wcscmp, that use a vectorized
// string comparison.
// For example, a fast wcscmp wouldn't compare one wchar_t at a time;
// instead it could load 16 bytes (SSE) or 32 bytes (AVX2) at a time
// from both strings and compare the whole block.
//
#define GIOVANNI_DICANIO_STRINGPOOL_ALIGN_STRINGS       1


inline PWSTR CStringPoolAllocator::AllocString(const WCHAR* pchBegin, const WCHAR* pchEnd)
{
    //
    // L"Hello world"
    //   ^          ^
    //   |          |
    //   pchBegin   pchEnd (points one past the last character)
    //


    _ASSERTE(pchBegin != nullptr);
    _ASSERTE(pchEnd   != nullptr);
    _ASSERTE(pchBegin <= pchEnd);

    // Consider +1 to include the terminating NUL in the string to be allocated
    const SIZE_T cch = pchEnd - pchBegin + 1;

#if GIOVANNI_DICANIO_STRINGPOOL_ALIGN_STRINGS
    // 16 bytes == 8 WCHARs: round the slot size up so the *next* string
    // also starts on a 16-byte boundary (same alignment the Windows heap gives)
    constexpr SIZE_T kchAlign = 16 / sizeof(WCHAR);
    const SIZE_T cchSlot = (cch + kchAlign - 1) & ~(kchAlign - 1);
#else
    // Packed strings
    const SIZE_T cchSlot = cch;
#endif // GIOVANNI_DICANIO_STRINGPOOL_ALIGN_STRINGS

    // If there is enough room in the current chunk, just carve memory from it
    if (cchSlot <= static_cast<SIZE_T>(m_pchLimit - m_pchNext))
    {
        WCHAR* const psz = m_pchNext;

#if GIOVANNI_DICANIO_STRINGPOOL_ALIGN_STRINGS
        // Check 16-byte alignment for the new string
        _ASSERTE((reinterpret_cast<ULONG_PTR>(psz) & 15) == 0);
#endif // GIOVANNI_DICANIO_STRINGPOOL_ALIGN_STRINGS

        m_pchNext += cchSlot;

        if (cch > 1)
        {
            // Copy WCHARs from the input range
            wmemcpy(psz, pchBegin, cch - 1);
        }

        // VirtualAlloc memory is zero-initialized, so the NUL (and the padding) are already 0
        _ASSERTE(psz[cch - 1] == L'\0');

        return psz;
    }

    if (cch > kchMaxCharAlloc)
    {
        throw std::bad_alloc();
    }

    // Size the new chunk using the padded slot size
    const SIZE_T cbAlloc = RoundUp(cchSlot * sizeof(WCHAR) + sizeof(ChunkHeader),
        m_cbGranularity);

    // Allocate memory for the new chunk
    BYTE* const pbNext = static_cast<BYTE*>(VirtualAlloc(nullptr,
                                                         cbAlloc,
                                                         MEM_RESERVE | MEM_COMMIT,
                                                         PAGE_READWRITE));
    if (pbNext == nullptr)
    {
        // Memory allocation failed
        static std::bad_alloc outOfMemory;
        throw outOfMemory;
    }

    // Hook the new chunk in our linked-list of chunks
    ChunkHeader* phdrCurrent = reinterpret_cast<ChunkHeader*>(pbNext);
    phdrCurrent->phdrPrev = m_phdrCurrent;
    phdrCurrent->cbSize = cbAlloc;

    // Update data members to point into the new allocated chunk
    m_phdrCurrent = phdrCurrent;
    m_pchNext = reinterpret_cast<WCHAR*>(phdrCurrent + 1);
    m_pchLimit = reinterpret_cast<WCHAR*>(pbNext + cbAlloc);

    return AllocString(pchBegin, pchEnd);
}


inline void CStringPoolAllocator::Destroy() noexcept
{
    // For each chunk in the linked list
    ChunkHeader* phdr = m_phdrCurrent;
    while (phdr != nullptr)
    {
        ChunkHeader hdr = *phdr;

        // Save pointer to the previous chunk *before* destroying the current chunk
        ChunkHeader* phdrPrev = hdr.phdrPrev;

        // Free the current chunk
        VirtualFree(phdr, 0, MEM_RELEASE);

        // Process the previous chunk
        phdr = phdrPrev;
    }

    // Reset pointers
    m_pchNext = nullptr;
    m_pchLimit = nullptr;
    m_phdrCurrent = nullptr;
}

