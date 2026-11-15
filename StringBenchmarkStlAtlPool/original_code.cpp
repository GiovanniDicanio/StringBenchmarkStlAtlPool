////////////////////////////////////////////////////////////////////////////////
// Original code from The Old New Thing blog post:
//
// May 19th, 2005
//
// Loading the dictionary, part 6: 
// Taking advantage of our memory allocation pattern
//
// https://devblogs.microsoft.com/oldnewthing/20050519-00/?p=35603/
//
//
// NOTE:
//
// The original code has bugs in the ~StringPool() destructor:
// it frees the previous chunk, but leaks the current one:
//
// VirtualFree(hdr.m_phdrPrev, hdr.m_cb, MEM_RELEASE);
//
// Moreover, in the original code there is a mismatch with the size 
// of the memory chunk:
// 
// hdr.m_cb is the size of the current chunk, but that is passed 
// as the size of the previous chunk (hdr.m_phdrPrev).
//
// --------------------------------------------------------------------------
//
// Code copy-and-pasted from the aforementioned blog post follows
//
////////////////////////////////////////////////////////////////////////////////

class StringPool
{
public:
 StringPool();
 ~StringPool();
 LPWSTR AllocString(const WCHAR* pszBegin, const WCHAR* pszEnd);
private:
 union HEADER {
  struct {
   HEADER* m_phdrPrev;
   SIZE_T  m_cb;
  };
  WCHAR alignment;
 };
 enum { MIN_CBCHUNK = 32000,
        MAX_CHARALLOC = 1024*1024 };
private:
 WCHAR*  m_pchNext;   // first available byte
 WCHAR*  m_pchLimit;  // one past last available byte
 HEADER* m_phdrCur;   // current block
 DWORD   m_dwGranularity;
}; // colorization fixed 25 May

inline RoundUp(DWORD cb, DWORD units)
{
    return ((cb + units - 1) / units) * units;
}

StringPool::StringPool()
 : m_pchNext(NULL), m_pchLimit(NULL), m_phdrCur(NULL)
{
 SYSTEM_INFO si;
 GetSystemInfo(&si);
 m_dwGranularity = RoundUp(sizeof(HEADER) + MIN_CBCHUNK,
                           si.dwAllocationGranularity);
}

LPWSTR StringPool::AllocString(const WCHAR* pszBegin, const WCHAR* pszEnd)
{
 size_t cch = pszEnd - pszBegin + 1;
 LPWSTR psz = m_pchNext;
 if (m_pchNext + cch <= m_pchLimit) {
  m_pchNext += cch;
  lstrcpynW(psz, pszBegin, cch);
  return psz;
 }
 if (cch > MAX_CHARALLOC) goto OOM;
 DWORD cbAlloc = RoundUp(cch * sizeof(WCHAR) + sizeof(HEADER),
                                                          m_dwGranularity);
 BYTE* pbNext = reinterpret_cast<BYTE*>(
                  VirtualAlloc(NULL, cbAlloc, MEM_COMMIT, PAGE_READWRITE));
 if (!pbNext) {
OOM:
  static std::bad_alloc OOM;
  throw(OOM);
 }
 m_pchLimit = reinterpret_cast<WCHAR*>(pbNext + cbAlloc);
 HEADER* phdrCur = reinterpret_cast<HEADER*>(pbNext);
 phdrCur->m_phdrPrev = m_phdrCur;
 phdrCur->m_cb = cbAlloc;
 m_phdrCur = phdrCur;
 m_pchNext = reinterpret_cast<WCHAR*>(phdrCur + 1);
 return AllocString(pszBegin, pszEnd);
}

StringPool::~StringPool()
{
 HEADER* phdr = m_phdrCur;
 while (phdr) {
  HEADER hdr = *phdr;
  VirtualFree(hdr.m_phdrPrev, hdr.m_cb, MEM_RELEASE);
  phdr = hdr.m_phdrPrev;
 }
}
