//////////////////////////////////////////////////////////////////////////////////////////
// FILE:    StringPool.cpp
// DESC:    Implementation of the string pool allocator.
// AUTHOR:  Giovanni Dicanio
//--------------------------------------------------------------------------------------
// SPDX-License-Identifier: MIT
//////////////////////////////////////////////////////////////////////////////////////////


#include "StringPool.hpp"


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


PWSTR CStringPoolAllocator::AllocString(const WCHAR* pchBegin, const WCHAR* pchEnd)
{
    _ASSERTE(pchBegin != nullptr);
    _ASSERTE(pchEnd != nullptr);
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


void CStringPoolAllocator::Destroy() noexcept
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

