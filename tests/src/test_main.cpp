// ============================================================================
//  test_main.cpp - the one file that gives the test program its main().
//
//  Defining DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN before including doctest asks
//  it to generate a main() that finds and runs every TEST_CASE in the program.
//  It must be done in EXACTLY ONE file, which is why this file contains
//  nothing else. Every other test file just includes <doctest/doctest.h>.
// ============================================================================

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
