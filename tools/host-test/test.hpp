/*
 * XYORAS Access — a very small test framework for the host tests.
 * Copyright (C) 2026 XYORAS Access contributors. GPL-3.0; see LICENSE.
 *
 * Deliberately dependency-free: these tests must build with nothing but a C++
 * compiler, on a machine that has no package manager set up for host builds.
 * Pulling in a framework would be a bigger obstacle than the framework solves.
 */
#ifndef XYORAS_HOST_TEST_HPP
#define XYORAS_HOST_TEST_HPP

#include <cstdio>
#include <string>

namespace test {

    inline int &Failures(void) { static int n = 0; return n; }
    inline int &Checks(void)   { static int n = 0; return n; }

    inline void Section(const char *name)
    {
        std::printf("\n  %s\n", name);
    }

    inline void Check(bool condition, const std::string &what)
    {
        ++Checks();
        if (condition)
        {
            std::printf("    [ok]   %s\n", what.c_str());
        }
        else
        {
            ++Failures();
            std::printf("    [FAIL] %s\n", what.c_str());
        }
    }

    template <typename A, typename B>
    inline void Equal(const A &actual, const B &expected, const std::string &what)
    {
        ++Checks();
        if (actual == static_cast<A>(expected))
        {
            std::printf("    [ok]   %s\n", what.c_str());
        }
        else
        {
            ++Failures();
            std::printf("    [FAIL] %s\n", what.c_str());
            std::printf("           expected %lld, got %lld\n",
                        static_cast<long long>(expected),
                        static_cast<long long>(actual));
        }
    }

    inline void EqualStr(const std::string &actual, const std::string &expected,
                         const std::string &what)
    {
        ++Checks();
        if (actual == expected)
        {
            std::printf("    [ok]   %s\n", what.c_str());
        }
        else
        {
            ++Failures();
            std::printf("    [FAIL] %s\n", what.c_str());
            std::printf("           expected \"%s\"\n", expected.c_str());
            std::printf("           got      \"%s\"\n", actual.c_str());
        }
    }

    inline int Report(const char *suite)
    {
        std::printf("\n  %s: %d checks, %d failed\n\n", suite, Checks(), Failures());
        return Failures() == 0 ? 0 : 1;
    }
}

#endif
