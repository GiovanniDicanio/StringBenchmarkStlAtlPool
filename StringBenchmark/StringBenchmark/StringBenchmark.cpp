/////////////////////////////////////////////////////////////////////////////////////////
//
// Test ATL CStringW vs. STL wstring vs. Custom String Pool Allocator
//
// By Giovanni Dicanio
//
/////////////////////////////////////////////////////////////////////////////////////////


//
// Define TEST_TINY_STRINGS to run the benchmark with tiny strings
// (STL friendly thanks to SSO).
//
//#define TEST_TINY_STRINGS


//
// Includes
//

#include <crtdbg.h>     // _ASSERTE
#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <atlstr.h>     // CString

#include <windows.h>    // Windows SDK API

#include "StringPool.h" // Custom string pool allocator

using namespace std;


//
// Performance Counter Helpers
//

long long Counter()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

long long Frequency()
{
    LARGE_INTEGER li;
    QueryPerformanceFrequency(&li);
    return li.QuadPart;
}

void PrintTime(const long long start, const long long finish, const char* const s)
{
    cout << s << ": " << (finish - start) * 1000.0 / Frequency() << " ms" << endl;
}


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

bool ComparePool(const wchar_t* psz1, const wchar_t* psz2)
{
    return wcscmp(psz1, psz2) < 0;
}

bool CompareStl(const std::wstring& s1, const std::wstring& s2)
{
    return wcscmp(s1.c_str(), s2.c_str()) < 0;
}

bool CompareAtl(const CString& s1, const CString& s2)
{
    // return wcscmp(s1, s2) < 0;
    // ATL CString already uses wcscmp
    return s1 < s2;
}


//
// Benchmark
//
int main()
{
    cout << "*** String Benchmark -- by Giovanni Dicanio ***\n\n";

    const auto shuffled = []() -> vector<wstring> {
        const wstring lorem[] = {
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

        for (int i = 0; i < (400*1000) ; ++i)
        {
            for (auto& s : lorem)
            {
#ifdef TEST_TINY_STRINGS
                // Tiny strings
                UNREFERENCED_PARAMETER(s);
                v.push_back(L"#" + to_wstring(i));
#else
                v.push_back(s + L" (#" + to_wstring(i) + L")");
#endif
            }
        }

        mt19937 prng(1980);

        shuffle(v.begin(), v.end(), prng);

        return v;
    }();

    const auto shuffled_ptrs = [&]() -> vector<const wchar_t*> {
        vector<const wchar_t*> v;

        for (auto& s : shuffled) {
            v.push_back(s.c_str());
        }

        return v;
    }();

#ifdef TEST_TINY_STRINGS
    cout << "Testing tiny strings (STL friendly thanks to SSO).\n";
#endif

    cout << "String count: " << (shuffled.size() / 1000) << "k\n\n";

    long long start = 0;
    long long finish = 0;

    CStringPoolAllocator stringPool;


    //
    // Measure creation times
    //

    cout << "=== Creation === \n";

    start = Counter();
    vector<ATL::CStringW> atl1(shuffled_ptrs.begin(), shuffled_ptrs.end());
    finish = Counter();
    PrintTime(start, finish, "ATL1");

    start = Counter();
    vector<wstring> stl1 = shuffled;
    finish = Counter();
    PrintTime(start, finish, "STL1");

    start = Counter();
    vector<const wchar_t*> pool1;
    pool1.reserve(shuffled_ptrs.size());
    for (auto psz : shuffled_ptrs) {
        pool1.push_back(stringPool.AllocString(psz));
    }
    finish = Counter();
    PrintTime(start, finish, "POL1");


    start = Counter();
    vector<ATL::CStringW> atl2(shuffled_ptrs.begin(), shuffled_ptrs.end());
    finish = Counter();
    PrintTime(start, finish, "ATL2");

    start = Counter();
    vector<wstring> stl2 = shuffled;
    finish = Counter();
    PrintTime(start, finish, "STL2");

    start = Counter();
    vector<const wchar_t*> pool2;
    pool2.reserve(shuffled_ptrs.size());
    for (auto psz : shuffled_ptrs) {
        pool2.push_back(stringPool.AllocString(psz));
    }
    finish = Counter();
    PrintTime(start, finish, "POL2");


    start = Counter();
    vector<ATL::CStringW> atl3(shuffled_ptrs.begin(), shuffled_ptrs.end());
    finish = Counter();
    PrintTime(start, finish, "ATL3");

    start = Counter();
    vector<wstring> stl3 = shuffled;
    finish = Counter();
    PrintTime(start, finish, "STL3");

    start = Counter();
    vector<const wchar_t*> pool3;
    pool3.reserve(shuffled_ptrs.size());
    for (auto psz : shuffled_ptrs) {
        pool3.push_back(stringPool.AllocString(psz));
    }
    finish = Counter();
    PrintTime(start, finish, "POL3");

    cout << '\n';


    //
    // Measure sorting times
    //

    cout << "=== Sorting === \n";

    start = Counter();
    sort(atl1.begin(), atl1.end(), CompareAtl);
    finish = Counter();
    PrintTime(start, finish, "ATL1");

    start = Counter();
    sort(stl1.begin(), stl1.end(), CompareStl);
    finish = Counter();
    PrintTime(start, finish, "STL1");

    start = Counter();
    sort(pool1.begin(), pool1.end(), ComparePool);
    finish = Counter();
    PrintTime(start, finish, "POL1");


    start = Counter();
    sort(atl2.begin(), atl2.end(), CompareAtl);
    finish = Counter();
    PrintTime(start, finish, "ATL2");

    start = Counter();
    sort(stl2.begin(), stl2.end(), CompareStl);
    finish = Counter();
    PrintTime(start, finish, "STL2");

    start = Counter();
    sort(pool2.begin(), pool2.end(), ComparePool);
    finish = Counter();
    PrintTime(start, finish, "POL2");


    start = Counter();
    sort(atl3.begin(), atl3.end(), CompareAtl);
    finish = Counter();
    PrintTime(start, finish, "ATL3");

    start = Counter();
    sort(stl3.begin(), stl3.end(), CompareStl);
    finish = Counter();
    PrintTime(start, finish, "STL3");

    start = Counter();
    sort(pool3.begin(), pool3.end(), ComparePool);
    finish = Counter();
    PrintTime(start, finish, "POL3");
}
