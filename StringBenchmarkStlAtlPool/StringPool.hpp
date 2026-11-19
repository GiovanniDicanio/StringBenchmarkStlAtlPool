//////////////////////////////////////////////////////////////////////////////////////////
//
// *** String Pool Allocator -- by Giovanni Dicanio <giovanni.dicanio@gmail.com> ***
//
// This is an implementation of a string pool allocator using the STL.
// There are no Win32 API calls here. The memory chunks are owned by std::unique_ptr's
// stored in a std::vector.
//
//////////////////////////////////////////////////////////////////////////////////////////
//
// FILE:    StringPool.hpp
// DESC:    Header-only implementation of the string pool allocator
// AUTHOR:  Giovanni Dicanio
//
// -------------------------------------------------------------------------------------
// Last update: 2026, November 19th
//
// Minimum C++ Language Standard: C++17
// IDE: VS 2022
// Code compiles cleanly at /W4.
//
//--------------------------------------------------------------------------------------
// SPDX-License-Identifier: MIT
//////////////////////////////////////////////////////////////////////////////////////////


#ifndef GIOVANNI_DICANIO_STRINGPOOLALLOCATOR_HPP_INCLUDED
#define GIOVANNI_DICANIO_STRINGPOOLALLOCATOR_HPP_INCLUDED


//---------------------------------------------------------------------------------------
//                                  Includes
//---------------------------------------------------------------------------------------

#include <crtdbg.h>     // _ASSERTE
#include <stddef.h>     // size_t
#include <wchar.h>      // wcslen

#include <memory>       // std::unique_ptr
#include <new>          // std::align_val_t, std::bad_alloc, std::bad_array_new_length
#include <utility>      // std::move, std::exchange
#include <vector>       // std::vector



//----------------------------------------------------------------------------------------
// String Pool Allocator - Efficiently allocates strings from a custom memory pool
//----------------------------------------------------------------------------------------
class CStringPoolAllocator
{
public:

    // Default chunk size, in bytes
    static constexpr size_t kcbDefaultMinChunkSize = 512 * 1024; // 512 KB

    // Alignment, in bytes, of every string returned by AllocString
    static constexpr size_t kcbAlignement = 16;


    // Initialize the string pool allocator
    CStringPoolAllocator() noexcept;

    // Initialize the string pool allocator, passing a value for the minimum chunk size in bytes.
    // Since chunks should be of comfortably large size, consider passing values >= 32000.
    // (The default value for this parameter is 512KB.)
    explicit CStringPoolAllocator(size_t cbMinChunkSize) noexcept;


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


public:

    // Release the string pool allocator's resources:
    // memory is released by std::vector and std::unique_ptr
    ~CStringPoolAllocator() = default;



    // Allocate a string deep-copying it from a [begin, end) character interval.
    // [begin, end) is as in common STL convention (i.e. the end pointer is *excluded*).
    // Throw std::bad_alloc on allocation failure.
    // May also throw std::bad_array_new_length.
    [[nodiscard]] wchar_t* AllocString(const wchar_t* pchBegin, const wchar_t* pchEnd);

    // Allocate a string deep-copying it from a source NUL-terminated string.
    // Throw std::bad_alloc on allocation failure.
    // May also throw std::bad_array_new_length.
    [[nodiscard]] wchar_t* DupString(const wchar_t* pszSource);


    // Release all the memory: every string previously allocated becomes invalid
    void Clear() noexcept;


    //
    // IMPLEMENTATION
    //
private:

    //
    // This pool allocator basically maintains a list of chunks,
    // and string allocations are served carving memory (with a simple pointer increase)
    // from these chunks.
    // When there isn't enough memory in the current chunk, a whole new chunk is allocated
    // to serve the current and following memory allocations.
    //


    // Minimum chunk size, in bytes
    static constexpr size_t kcbMinChunkSize = 4096;

    // Number of wchar_ts in a 16-byte slot
    static constexpr int kchSlot = kcbAlignement / sizeof(wchar_t);

    static_assert(kcbAlignement % sizeof(wchar_t) == 0,
                  "Alignment must be a multiple of wchar_t size.");


    // Do not accept strings larger than 1 "MB" wchar_t's
    // (Since sizeof(wchar_t) == 2 [byte], this limit is basically 2MB)
    static constexpr int kchMaxCharAlloc = 1024 * 1024;


    struct AlignedChunkDeleter
    {
        void operator()(wchar_t* p) const noexcept
        {
            ::operator delete[](p, std::align_val_t{ kcbAlignement });
        }
    };

    using ChunkPtr = std::unique_ptr<wchar_t[], AlignedChunkDeleter>;

    std::vector<ChunkPtr> m_chunks;         // Owned memory chunks

    wchar_t* m_pchNext      = nullptr;      // First available wchar_t in current chunk
    size_t   m_cchFree      = 0;            // Number of available wchar_t's in current chunk
    size_t   m_cchChunkSize = 0;            // Size of current chunk, in wchar_t's


    //
    // Helper Methods
    //

    // Round a wchar_t count up to a number of 16-byte slots
    [[nodiscard]] static size_t RoundUpToSlot(size_t cch) noexcept;

    // Allocate a new 16-byte aligned chunk of cch wchar_t's, owned by m_chunks
    [[nodiscard]] wchar_t* NewChunk(size_t cch);
};


//========================================================================================
//                          Inline Method Implementations
//========================================================================================

inline CStringPoolAllocator::CStringPoolAllocator() noexcept
    : CStringPoolAllocator(kcbDefaultMinChunkSize)
{
}


inline CStringPoolAllocator::CStringPoolAllocator(size_t cbChunkSize) noexcept
    : m_cchChunkSize(RoundUpToSlot((cbChunkSize < kcbMinChunkSize ? kcbMinChunkSize : cbChunkSize)
                                   / sizeof(wchar_t)))
{
}


inline CStringPoolAllocator::CStringPoolAllocator(CStringPoolAllocator&& other) noexcept
    : m_chunks(std::move(other.m_chunks))
    , m_pchNext(std::exchange(other.m_pchNext, nullptr))
    , m_cchFree(std::exchange(other.m_cchFree, 0))
    , m_cchChunkSize(other.m_cchChunkSize)
{
    other.m_chunks.clear();
}


inline CStringPoolAllocator& CStringPoolAllocator::operator=(CStringPoolAllocator&& other) noexcept
{
    if (this != &other)
    {
        m_chunks = std::move(other.m_chunks);
        m_pchNext = std::exchange(other.m_pchNext, nullptr);
        m_cchFree = std::exchange(other.m_cchFree, 0);
        m_cchChunkSize = other.m_cchChunkSize;
        other.m_chunks.clear();
    }

    return* this;
}


inline void CStringPoolAllocator::Clear() noexcept
{
    m_chunks.clear();
    m_pchNext = nullptr;
    m_cchFree = 0;
}


inline wchar_t* CStringPoolAllocator::DupString(const wchar_t* pszSource)
{
    _ASSERTE(pszSource != nullptr);
    return AllocString(pszSource, pszSource + wcslen(pszSource));
}


inline size_t CStringPoolAllocator::RoundUpToSlot(size_t cch) noexcept
{
    return (cch + kchSlot - 1) / kchSlot * kchSlot;
}


inline wchar_t* CStringPoolAllocator::NewChunk(size_t cch)
{
    if (cch > kchMaxCharAlloc)
    {
        throw std::bad_array_new_length();
    }

    // NOTE: The memory allocated and returned by new[] here is *uninitialized*:
    // AllocString writes every NUL-terminator *explicitly*
    ChunkPtr chunk(static_cast<wchar_t*>(
        ::operator new[](cch * sizeof(wchar_t), std::align_val_t{ kcbAlignement })));

    // NOTE on exception safety:
    // If push_back throws, chunk still owns the memory and will release it
    m_chunks.push_back(std::move(chunk));

    // Get a pointer to the beginning of the wchar_t array memory
    return m_chunks.back().get();
}


inline wchar_t* CStringPoolAllocator::AllocString(const wchar_t* pchBegin, const wchar_t* pchEnd)
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
    const size_t cch = static_cast<size_t>(pchEnd - pchBegin) + 1;

    // Round up to whole 16-byte slots, so the *next* string is 16-byte aligned, too
    const size_t cchSlot = RoundUpToSlot(cch);

    wchar_t* psz = nullptr;

    // If there is enough room in the current chunk, just carve memory from it
    if (cchSlot <= m_cchFree)
    {
        // Enough room in current chunk: allocation is just a pointer increase :)

        psz = m_pchNext;
        m_pchNext += cchSlot;
        m_cchFree -= cchSlot;
    }
    else if (cchSlot > m_cchChunkSize)
    {
        // The requested string allocation is larger than the whole chunk:
        // Give the string its own dedicated chunk, and keep serving
        // the following strings from the current chunk
        psz = NewChunk(cchSlot);
    }
    else
    {
        // Not enough room in the current chunk: start a new chunk
        psz = NewChunk(m_cchChunkSize);
        m_pchNext = psz + cchSlot;
        m_cchFree = m_cchChunkSize - cchSlot;
    }

    // Copy the characters, then write the terminating NUL.
    //
    // NOTE: Chunk memory was returned by new[] and was *not* zero-initialized,
    // so we *need* to explicitly write the terminating NUL (zero).
    wmemcpy(psz, pchBegin, cch - 1);
    psz[cch - 1] = L'\0';

    return psz;
}


#endif // GIOVANNI_DICANIO_STRINGPOOLALLOCATOR_HPP_INCLUDED

