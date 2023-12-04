#ifndef GIOVANNI_DICANIO_STRINGPOOL_HPP_INCLUDED
#define GIOVANNI_DICANIO_STRINGPOOL_HPP_INCLUDED

/////////////////////////////////////////////////////////////////////////////////////////
// String Pool Allocator (Version that uses STL instead of Win32 VirtualAlloc)
// -- by Giovanni Dicanio
/////////////////////////////////////////////////////////////////////////////////////////


#include <crtdbg.h>     // _ASSERTE
#include <wchar.h>      // wcslen, wmemcpy
#include <memory>       // std::unique_ptr, std::make_unique
#include <vector>       // std::vector


//---------------------------------------------------------------------------------------
// String Pool Allocator - Efficiently allocates strings from a custom memory pool
//---------------------------------------------------------------------------------------
class StringPoolAllocator
{
public:
    // Initialize the string pool allocator
    StringPoolAllocator();

    // Allocate a string deep-copying it from a [begin, end) character interval.
    // [begin, end) is as in common STL convention (i.e. the end pointer is *excluded*).
    // Throw std::bad_alloc on allocation failure.
    wchar_t* AllocString(const wchar_t* pchBegin, const wchar_t* pchEnd);

    // Allocate a string deep-copying it from a source NUL-terminated string.
    // Throw std::bad_alloc on allocation failure.
    wchar_t* AllocString(const wchar_t* pszSource);


    //
    // Ban Copy
    //
private:
    StringPoolAllocator(const StringPoolAllocator&) = delete;
    StringPoolAllocator& operator=(const StringPoolAllocator&) = delete;


    //
    // IMPLEMENTATION
    //
private:

    // Cch = count in "characters" (i.e. wchar_ts) instead of bytes

    // Maximum size for a single string
    static constexpr size_t kMaxStringLengthCch = 100 * 1000; // wchar_ts

    // Chunk size, in wchar_t's
    static constexpr size_t kChunkSizeCch = 250 * 1000; // wchar_ts

    // Single chunk of the pool allocator.
    // The pool allocator manages a list of chunks (via std::vector).
    // Memory is carved from a chunk by simply increasing a pointer.
    class Chunk
    {
    public:
        // Creates the chunk with its contiguous block of memory
        Chunk()
            : m_pool{ std::make_unique<wchar_t[]>(kChunkSizeCch) }
            , m_poolSizeCch{ kChunkSizeCch }
        {
            // Clear chunk's memory
            std::fill(BasePtr(), EndPtr(), L'\0');
        }

        // Chunk size, in wchar_ts
        size_t Size() const noexcept
        {
            return m_poolSizeCch;
        }

        // Pointer to the beginning of the chunk's memory
        wchar_t* BasePtr() noexcept
        {
            return m_pool.get();
        }

        // Pointer one past the last wchar_t in the chunk's memory
        wchar_t* EndPtr() noexcept
        {
            return m_pool.get() + m_poolSizeCch;
        }

    private:
        // Contiguous block of memory used to serve string allocations
        std::unique_ptr<wchar_t[]> m_pool;

        // Size, in wchar_ts, of the memory managed by this chunk
        size_t m_poolSizeCch; // in wchar_ts
    };

    // The pool allocator keeps track of the various chunks used to allocate memory
    std::vector<Chunk> m_chunks;

    wchar_t*        m_pchNext       = nullptr;  // First available byte in current chunk
    wchar_t*        m_pchLimit      = nullptr;  // One past last available byte in current chunk

    Chunk*          m_pCurrent      = nullptr;  // Observing pointer to the current chunk
                                                // used to serve memory allocations
};


//=======================================================================================
//                          Inline Method Implementations
//=======================================================================================

inline StringPoolAllocator::StringPoolAllocator()
{
}


inline wchar_t* StringPoolAllocator::AllocString(const wchar_t* pchBegin, const wchar_t* pchEnd)
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
    const size_t cch = pchEnd - pchBegin + 1;

    if (cch > kMaxStringLengthCch)
    {
        // Input string is too long!
        throw std::bad_alloc{};
    }

    // If there is enough room in the current chunk, just carve memory from it
    if (m_pchNext + cch <= m_pchLimit)
    {
        // Begin of the newly allocated string:
        // start from the first available slot in the current chunk
        wchar_t* const psz = m_pchNext;

        // There is enough room in the current chunk: so allocation is just a pointer increase :-)
        m_pchNext += cch;

        // Exclude the terminating NUL from the character copy
        if (cch > 1)
        {
            // Copy characters from the input source string to this memory area pointed to by psz
            wmemcpy(psz, pchBegin, cch - 1);
        }

        // The memory allocated in each chunk should already be zero initialized,
        // so there's no need to write the terminating NUL
        _ASSERTE(psz[cch - 1] == L'\0');

        // Return the pointer to the beginning of the newly allocated string
        return psz;
    }

    // There is not enough room in the current chunk: allocate a new block
    m_chunks.push_back(Chunk{});

    // Reset pointers to reference data in the current chunk
    m_pCurrent = &(m_chunks.back());
    m_pchNext  = m_pCurrent->BasePtr();
    m_pchLimit = m_pCurrent->EndPtr();

    // Retry the string allocation using the newly allocated chunk
    return AllocString(pchBegin, pchEnd);
}


inline wchar_t* StringPoolAllocator::AllocString(const wchar_t* pszSource)
{
    _ASSERTE(pszSource != nullptr);
    return AllocString(pszSource, pszSource + wcslen(pszSource));
}


#endif // GIOVANNI_DICANIO_STRINGPOOL_HPP_INCLUDED
