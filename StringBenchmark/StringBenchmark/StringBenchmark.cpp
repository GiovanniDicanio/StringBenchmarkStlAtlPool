/////////////////////////////////////////////////////////////////////////////////////////
//
// Test ATL's CStringW vs. STL's wstring vs. Custom String Pool Allocator
//
// By Giovanni Dicanio
//
/////////////////////////////////////////////////////////////////////////////////////////


//
// Define TEST_TINY_STRINGS to run the benchmark with tiny strings
// (STL friendly thanks to SSO).
//
// Uncomment the following line for testing with tiny strings:
//#define TEST_TINY_STRINGS     1
//


//---------------------------------------------------------------------------------------
//                              Includes
//---------------------------------------------------------------------------------------

#include <crtdbg.h>         // _ASSERTE
#include <algorithm>        // std::shuffle, std::sort
#include <iostream>         // std::cout
#include <random>           // std::mt19937
#include <string>           // std::wstring
#include <vector>           // std::vector

#include <atlstr.h>         // CString

#include <windows.h>        // Windows SDK API

#include "StringPool.hpp"   // Custom string pool allocator


using std::cout;
using std::vector;
using std::wstring;



//---------------------------------------------------------------------------------------
// Performance Counter Helpers
//---------------------------------------------------------------------------------------

inline long long PerfCounter() noexcept
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

inline long long PerfFrequency() noexcept
{
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    return li.QuadPart;
}

inline void PrintTime(const long long start, const long long finish, const char* const message)
{
    cout << message << ": " << (finish - start) * 1000.0 / PerfFrequency() << " ms" << std::endl;
}


//---------------------------------------------------------------------------------------
//
// Making Uniform String Comparisons
// =================================
//
// Using "Step Into Specific", I found that in the VS 2019 implementation,
// std::wstring uses wmemcmp for comparisons (file: <xstring>; line: 235).
// On the other hand, ATL CString uses wcscmp (file: <cstringt.h>; line: 564).
//
// To make string comparisons uniform, I introduced the following helper functions
// to always call wcscmp.
//
//---------------------------------------------------------------------------------------

inline bool ComparePool(const wchar_t* psz1, const wchar_t* psz2)
{
    return wcscmp(psz1, psz2) < 0;
}

inline bool CompareStl(const std::wstring& s1, const std::wstring& s2)
{
    return wcscmp(s1.c_str(), s2.c_str()) < 0;
}

inline bool CompareAtl(const CString& s1, const CString& s2)
{
    // return wcscmp(s1, s2) < 0;
    // ATL CString already uses wcscmp
    return s1 < s2;
}


//---------------------------------------------------------------------------------------
// Benchmark
//---------------------------------------------------------------------------------------
int main()
{
    cout << " *** String Benchmark (2023) -- by Giovanni Dicanio *** \n";
    cout << "     [STL-based version] \n\n";

    // Build a vector of shuffled strings that will be used for the benchmark
    const auto shuffled = []() -> vector<wstring>
    {
        const wstring lorem[] =
        {
            L"Lorem ipsum dolor sit amet, consectetuer adipiscing elit.",
            L"Maecenas porttitor congue massa. Fusce posuere, magna sed",
            L"pulvinar ultricies, purus lectus malesuada libero,",
            L"sit amet commodo magna eros quis urna.",
            L"Nunc viverra imperdiet enim. Fusce est. Vivamus a tellus.",
            L"Pellentesque habitant morbi tristique senectus et netus et",
            L"malesuada fames ac turpis egestas. Proin pharetra nonummy pede.",
            L"Mauris et orci. [*** add more chars to prevent SSO ***]"
        };

        vector<wstring> v;

#ifdef _DEBUG
        // Just a few strings in *slow running* debug builds
        constexpr int kStringRepeatCount = 10;
#else
        // Lots of strings in release builds
        constexpr int kStringRepeatCount = 400 * 1000; // 400K
#endif // _DEBUG

        for (int i = 0; i < kStringRepeatCount ; ++i)
        {
            for (const auto & s : lorem)
            {
#ifdef TEST_TINY_STRINGS
                // Tiny strings
                UNREFERENCED_PARAMETER(s);
                v.push_back(L"#" + std::to_wstring(i));
#else
                v.push_back(s + L" (#" + std::to_wstring(i) + L")");
#endif
            }
        }

        std::mt19937 prng(1987);    // 1987 : Amiga 500! :)

        std::shuffle(v.begin(), v.end(), prng);

        return v;
    }();

    // shuffled_ptrs is a vector of raw *observing* pointers to the previous shuffled strings
    const auto shuffled_ptrs = [&]() -> vector<const wchar_t*>
    {
        vector<const wchar_t*> v;

        for (const auto& s : shuffled)
        {
            v.push_back(s.c_str());
        }

        return v;
    }();

#ifdef TEST_TINY_STRINGS
    cout << "Testing tiny strings (STL friendly thanks to SSO).\n";
#endif

    cout << "String count: ";
    if (shuffled.size() > 1000)
    {
        cout << (shuffled.size() / 1000) << "k\n\n";
    }
    else
    {
        cout << shuffled.size() << "\n\n";
    }

    long long start = 0;
    long long finish = 0;

    StringPoolAllocator stringPool;


    //
    // Measure creation times
    // ----------------------
    //

    cout << "=== Creation === \n";

    //
    // Creation #1
    //

    start = PerfCounter();
    vector<ATL::CStringW> atl1(shuffled_ptrs.begin(), shuffled_ptrs.end());
    finish = PerfCounter();
    PrintTime(start, finish, "ATL1");

    start = PerfCounter();
//  vector<wstring> stl1 = shuffled; // <-- this would be unfairly advantageous for vector<wstring>
    vector<wstring> stl1(shuffled_ptrs.begin(), shuffled_ptrs.end());
    finish = PerfCounter();
    PrintTime(start, finish, "STL1");

    start = PerfCounter();
    vector<const wchar_t*> pool1;
    pool1.reserve(shuffled_ptrs.size());
    for (auto psz : shuffled_ptrs)
    {
        pool1.push_back(stringPool.AllocString(psz));
    }
    finish = PerfCounter();
    PrintTime(start, finish, "POL1");

    //
    // Sanity check in debug builds - the vectors should contain the same strings
    //
#ifdef _DEBUG
    for (size_t i = 0; i < shuffled_ptrs.size(); i++)
    {
        ATLASSERT(wcscmp(shuffled_ptrs[i], atl1[i].GetString()) == 0);
        ATLASSERT(wcscmp(shuffled_ptrs[i], stl1[i].c_str()    ) == 0);
        ATLASSERT(wcscmp(shuffled_ptrs[i], pool1[i]           ) == 0);
    }
#endif // _DEBUG

    //
    // Creation #2
    //

    start = PerfCounter();
    vector<ATL::CStringW> atl2(shuffled_ptrs.begin(), shuffled_ptrs.end());
    finish = PerfCounter();
    PrintTime(start, finish, "ATL2");

    start = PerfCounter();
    vector<wstring> stl2(shuffled_ptrs.begin(), shuffled_ptrs.end());
    finish = PerfCounter();
    PrintTime(start, finish, "STL2");

    start = PerfCounter();
    vector<const wchar_t*> pool2;
    pool2.reserve(shuffled_ptrs.size());
    for (auto psz : shuffled_ptrs)
    {
        pool2.push_back(stringPool.AllocString(psz));
    }
    finish = PerfCounter();
    PrintTime(start, finish, "POL2");


    //
    // Creation #3
    //

    start = PerfCounter();
    vector<ATL::CStringW> atl3(shuffled_ptrs.begin(), shuffled_ptrs.end());
    finish = PerfCounter();
    PrintTime(start, finish, "ATL3");

    start = PerfCounter();
    vector<wstring> stl3(shuffled_ptrs.begin(), shuffled_ptrs.end());
    finish = PerfCounter();
    PrintTime(start, finish, "STL3");

    start = PerfCounter();
    vector<const wchar_t*> pool3;
    pool3.reserve(shuffled_ptrs.size());
    for (auto psz : shuffled_ptrs)
    {
        pool3.push_back(stringPool.AllocString(psz));
    }
    finish = PerfCounter();
    PrintTime(start, finish, "POL3");

    cout << '\n';


    //
    // Measure sorting times
    // ---------------------
    //

    cout << "=== Sorting === \n";

    //
    // Sort #1
    //

    start = PerfCounter();
    sort(atl1.begin(), atl1.end(), CompareAtl);
    finish = PerfCounter();
    PrintTime(start, finish, "ATL1");

    start = PerfCounter();
    sort(stl1.begin(), stl1.end(), CompareStl);
    finish = PerfCounter();
    PrintTime(start, finish, "STL1");

    start = PerfCounter();
    sort(pool1.begin(), pool1.end(), ComparePool);
    finish = PerfCounter();
    PrintTime(start, finish, "POL1");


    //
    // Sort #2
    //

    start = PerfCounter();
    sort(atl2.begin(), atl2.end(), CompareAtl);
    finish = PerfCounter();
    PrintTime(start, finish, "ATL2");

    start = PerfCounter();
    sort(stl2.begin(), stl2.end(), CompareStl);
    finish = PerfCounter();
    PrintTime(start, finish, "STL2");

    start = PerfCounter();
    sort(pool2.begin(), pool2.end(), ComparePool);
    finish = PerfCounter();
    PrintTime(start, finish, "POL2");


    //
    // Sort #3
    //

    start = PerfCounter();
    sort(atl3.begin(), atl3.end(), CompareAtl);
    finish = PerfCounter();
    PrintTime(start, finish, "ATL3");

    start = PerfCounter();
    sort(stl3.begin(), stl3.end(), CompareStl);
    finish = PerfCounter();
    PrintTime(start, finish, "STL3");

    start = PerfCounter();
    sort(pool3.begin(), pool3.end(), ComparePool);
    finish = PerfCounter();
    PrintTime(start, finish, "POL3");
}
