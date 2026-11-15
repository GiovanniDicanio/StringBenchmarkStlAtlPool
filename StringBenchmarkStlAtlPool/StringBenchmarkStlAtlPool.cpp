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
// #define TEST_TINY_STRINGS     1
//


//---------------------------------------------------------------------------------------
//                              Includes
//---------------------------------------------------------------------------------------

#include "StringPool.hpp"   // Custom string pool allocator

#include <Windows.h>        // Windows SDK API

#include <atlstr.h>         // CString

#include <crtdbg.h>         // _ASSERTE
#include <wchar.h>          // wcscmp

#include <algorithm>        // std::shuffle, std::sort
#include <iostream>         // std::cout
#include <random>           // std::mt19937
#include <sstream>          // std::wistringstream
#include <string>           // std::wstring
#include <vector>           // std::vector


using std::cout;
using std::vector;
using std::wstring;
using ATL::CStringW;


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
// In VS 2022 the source files and lines are the following:
//
// - std::wstring:
//      File: __msvc_string_view.hpp
//      Line: 294
//
// - CString:
//      File: cstringt.h
//      Line: 572
//
//
// To make string comparisons *uniform*, I introduced the following helper functions
// that always call wcscmp.
//
//---------------------------------------------------------------------------------------

inline bool ComparePool(const wchar_t* psz1, const wchar_t* psz2)
{
    return wcscmp(psz1, psz2) < 0;
}

inline bool CompareStl(const wstring& s1, const wstring& s2)
{
    return wcscmp(s1.c_str(), s2.c_str()) < 0;
}

inline bool CompareAtl(const CStringW& s1, const CStringW& s2)
{
    return wcscmp(s1.GetString(), s2.GetString()) < 0;
}


//---------------------------------------------------------------------------------------
// Benchmark
//---------------------------------------------------------------------------------------

#ifndef TEST_TINY_STRINGS

// Build string data set for ordinary benchmark (no SSO)
vector<wstring> BuildStringsForBenchmark()
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

    // Split the lorem lines into a vocabulary of words
    vector<wstring> words;
    for (const auto& line : lorem)
    {
        std::wistringstream iss(line);
        wstring word;
        while (iss >> word)
        {
            words.push_back(word);
        }
    }

#ifdef _DEBUG
    constexpr int kStringCount = 80;                // few strings in slow debug builds
#else
    constexpr int kStringCount = 8 * 400 * 1000;    // 3'200'000 in release builds
#endif // _DEBUG

    constexpr size_t kMinLength = 40;   // > SSO capacity (7 wchar_t's)
    constexpr size_t kMaxLength = 100;

    std::mt19937 prng(1987);            // 1987 : Amiga 500! :)

    // To pick random words
    std::uniform_int_distribution<size_t> wordDist(0, words.size() - 1);

    // To pick random lengths
    std::uniform_int_distribution<size_t> lengthDist(kMinLength, kMaxLength);

    vector<wstring> v;
    v.reserve(kStringCount);

    for (int i = 0; i < kStringCount; ++i)
    {
        // Random target length, independent of the string content
        const size_t length = lengthDist(prng);

        // Fill with random words, then cut to the target length
        wstring s;
        while (s.size() < length)
        {
            s += words[wordDist(prng)];
            s += L' ';
        }
        s.resize(length);

        // Unique suffix
        s += L" (#" + std::to_wstring(i) + L")";

        v.push_back(std::move(s));
    }

    std::shuffle(v.begin(), v.end(), prng);

    return v;
}

#else

// Build string data set with small strings that enable SSO
vector<wstring> BuildStringsForBenchmark()
{

#ifdef _DEBUG
    constexpr int kStringCount = 80;                // few strings in slow debug builds
#else
    constexpr int kStringCount = 8 * 400 * 1000;    // 3'200'000 in release builds
#endif // _DEBUG

    std::mt19937 prng(1987);            // 1987 : Amiga 500! :)

    vector<wstring> v;
    v.reserve(kStringCount);

    for (int i = 0; i < kStringCount; ++i)
    {
        // Tiny strings
        v.push_back(L"#" + std::to_wstring(i));
    }

    std::shuffle(v.begin(), v.end(), prng);

    return v;
}

#endif // TEST_TINY_STRINGS


int main()
{
    cout << "\n ******************************************************";
    cout << "\n **  String Benchmark (2026) -- by Giovanni Dicanio  **";
    cout << "\n **                                                  **";
    cout << "\n **      Compare ATL vs. STL vs. Pool Allocator      **";
    cout << "\n ****************************************************** \n";

    //
    // Prepare string data for the benchmark
    //

    cout << "\nBuilding string data for the benchmark...";

    // Build a vector of shuffled strings that will be used for the benchmark
    const auto shuffled = BuildStringsForBenchmark();

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

    cout << "Done.\n\n";


#ifdef TEST_TINY_STRINGS
        cout << "Testing tiny strings (STL friendly thanks to SSO). \n\n";
#endif // TEST_TINY_STRINGS


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

    CStringPoolAllocator stringPool;


    //
    // Measure creation times
    // ----------------------
    //

    cout << "=== Creation === \n";

    //
    // Creation #1
    //

    start = PerfCounter();
    vector<CStringW> atl1(shuffled_ptrs.begin(), shuffled_ptrs.end());
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
        pool1.push_back(stringPool.DupString(psz));
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
    vector<CStringW> atl2(shuffled_ptrs.begin(), shuffled_ptrs.end());
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
        pool2.push_back(stringPool.DupString(psz));
    }
    finish = PerfCounter();
    PrintTime(start, finish, "POL2");


    //
    // Creation #3
    //

    start = PerfCounter();
    vector<CStringW> atl3(shuffled_ptrs.begin(), shuffled_ptrs.end());
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
        pool3.push_back(stringPool.DupString(psz));
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

