#pragma once

/////////////////////////////////////////////////////////////////////////////////////////
// String Pool Allocator -- by Giovanni Dicanio
/////////////////////////////////////////////////////////////////////////////////////////


#include <crtdbg.h>     // _ASSERTE
#include <wchar.h>      // wcslen, wmemcpy
#include <new>          // std::bad_alloc

#include <Windows.h>    // Windows Platform SDK


//---------------------------------------------------------------------------------------
// String Pool Allocator - Efficiently allocates strings from a custom memory pool
//---------------------------------------------------------------------------------------
class CStringPoolAllocator
{
public:
    // Initialize the string pool allocator
    CStringPoolAllocator();

    // Release the string pool allocator's resources
    ~CStringPoolAllocator();

    // Allocate a string deep-copying it from a [begin, end) character interval.
    // [begin, end) is as in common STL convention (i.e. the end pointer is *excluded*).
    // Throw std::bad_alloc on error.
    PWSTR AllocString(const WCHAR* pchBegin, const WCHAR* pchEnd);

    // Allocate a string deep-copying it from a NUL-terminated string.
    // Throw std::bad_alloc on error.
    PWSTR AllocString(PCWSTR pszSource);


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
    // Based on The Old New Thing blog post:
    //  https://devblogs.microsoft.com/oldnewthing/20050519-00/?p=35603
    // Loading the dictionary, part 6: Taking advantage of our memory allocation pattern
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
    //      |  m_phdrPrev  |   <--- Pointer to previous chunk header (in a singly-linked list)
    //      +--------------+
    //      |   m_cbSize   |   <--- Total size, in bytes, of the current chunk
    //      +--------------+
    //      |              |
    //      |   Array of   |   <--- Array of WCHARs, used to serve string allocations
    //      |    WCHARs    |        (just increase a pointer in the current allocated block)
    //      |              |
    //      |     ...      |
    //      |              |
    //      +--------------+
    //

    union ChunkHeader
    {
        struct
        {
            // Pointer to the previous chunk in the linked list
            ChunkHeader* m_phdrPrev;

            // Total size, in bytes, of the current chunk
            SIZE_T  m_cbSize;
        };

        // This field is required for proper alignment for WCHAR's that follow the previous
        // chunk header fields
        WCHAR chAlignment;
    };

    enum
    {
        // Minimum chunk size, in bytes
        kcbMinChunkSize = 512 * 1024, // was 32000 in the original code

        // Do not accept strings larger than 1MB WCHARs
        kchMaxCharAlloc = 1024 * 1024
    };

    WCHAR*          m_pchNext       = nullptr;  // First available byte in current chunk
    WCHAR*          m_pchLimit      = nullptr;  // One past last available byte in current chunk
    ChunkHeader*    m_phdrCurrent   = nullptr;  // Current chunk to serve memory allocations
    SIZE_T          m_cbGranularity = 0;        // Allocation granularity (used with VirtualAlloc)

    //
    // Helper Methods
    //

    void Destroy();

    static SIZE_T RoundUp(SIZE_T cb, SIZE_T units);
    static SIZE_T GetAllocationGranularity();
};


//=======================================================================================
//                          Inline Method Implementations
//=======================================================================================

inline CStringPoolAllocator::CStringPoolAllocator()
    : m_cbGranularity(GetAllocationGranularity())
{
}


inline CStringPoolAllocator::~CStringPoolAllocator()
{
    Destroy();
}


inline void CStringPoolAllocator::Destroy()
{
    // For each chunk in the linked list
    ChunkHeader* phdr = m_phdrCurrent;
    while (phdr != nullptr)
    {
        ChunkHeader hdr = *phdr;

        //
        // BUG in the original code:
        // Frees the previous chunk, but leaks the current one.
        //
        // VirtualFree(hdr.m_phdrPrev, hdr.m_cb, MEM_RELEASE);
        //

        // Save pointer to the previous chunk *before* destroying the current chunk
        ChunkHeader* phdrPrev = hdr.m_phdrPrev;

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


inline PWSTR CStringPoolAllocator::AllocString(const WCHAR* pchBegin, const WCHAR* pchEnd)
{
    //
    // L"Hello world"
    //   ^          ^
    //   |          |
    //   |          \
    //   pchBegin    \
    //                pchEnd (points one past the last character)
    //

    // Check input pointers
    _ASSERTE(pchBegin != nullptr);
    _ASSERTE(pchEnd   != nullptr);
    _ASSERTE(pchBegin <= pchEnd);

    // Consider +1 to include the terminating NUL in the string to be allocated
    const SIZE_T cch = pchEnd - pchBegin + 1;

    // If there is enough room in the current chunk, just carve memory from it
    if (m_pchNext + cch <= m_pchLimit)
    {
        // Begin of the newly allocated string:
        // start from the first available slot in the current chunk
        WCHAR* const psz = m_pchNext;

        // There is enough room in the current chunk: so allocation is just a pointer increase :-)
        m_pchNext += cch;

        // Original code:
        //  lstrcpynW(psz, pszBegin, cch);

        // Exclude the terminating NUL from the character copy
        if (cch > 1)
        {
            // Copy characters from the input source string to this memory area pointed to by psz
            wmemcpy(psz, pchBegin, cch - 1);
        }

        // The memory allocated by VirtualAlloc should be zero initialized,
        // so there's no need to write the terminating NUL
        _ASSERTE(psz[cch - 1] == L'\0');

        // Return the pointer to the beginning of the newly allocated string
        return psz;
    }

    // The original code used *goto* OOO (Out of Memory)
    // :(

    // Check that the requested string length doesn't exceed the max length limit
    if (cch > kchMaxCharAlloc)
    {
        // String is too large
        throw std::bad_alloc();
    }

    // There is not enough room in the current chunk: allocate a new block
    const SIZE_T cbAlloc = RoundUp(cch * sizeof(WCHAR) + sizeof(ChunkHeader),
                             m_cbGranularity);
    BYTE* const pbNext = static_cast<BYTE*>(VirtualAlloc(nullptr,
                                                         cbAlloc,
                                                         MEM_COMMIT,
                                                         PAGE_READWRITE));
    if (pbNext == nullptr)
    {
        // Out of memory
        throw std::bad_alloc();
    }

    // Hook the newly allocated chunk to the current linked list
    ChunkHeader* phdrCurrent = reinterpret_cast<ChunkHeader*>(pbNext);
    phdrCurrent->m_phdrPrev = m_phdrCurrent;
    phdrCurrent->m_cbSize = cbAlloc;
    m_phdrCurrent = phdrCurrent;
    m_pchNext = reinterpret_cast<WCHAR*>(phdrCurrent + 1);
    m_pchLimit = reinterpret_cast<WCHAR*>(pbNext + cbAlloc);

    // Retry the string allocation using the newly allocated chunk
    return AllocString(pchBegin, pchEnd);
}


inline PWSTR CStringPoolAllocator::AllocString(PCWSTR pszSource)
{
    _ASSERTE(pszSource != nullptr);
    return AllocString(pszSource, pszSource + wcslen(pszSource));
}


inline SIZE_T CStringPoolAllocator::RoundUp(SIZE_T cb, SIZE_T units)
{
    return ((cb + units - 1) / units) * units;
}


inline SIZE_T CStringPoolAllocator::GetAllocationGranularity()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return RoundUp(sizeof(ChunkHeader) + kcbMinChunkSize,
                   si.dwAllocationGranularity);
}
