/*
 * ICM — a test harness small enough to read in one go.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * The modules under test are plain C, and so are their tests: no C++ test
 * framework to download, no mocking. Each test file is one executable that
 * CTest runs; each test function names the requirement it covers, so
 * tools/check_traceability.py can find it.
 */
#ifndef CHECK_H
#define CHECK_H

#include <math.h>
#include <stdio.h>

static int check_count;
static int check_failures;

#define CHECK(cond)                                                        \
  do {                                                                     \
    check_count++;                                                         \
    if (!(cond)) {                                                         \
      check_failures++;                                                    \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,    \
              #cond);                                                      \
    }                                                                      \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                              \
  do {                                                                     \
    check_count++;                                                         \
    double a_ = (double)(a), b_ = (double)(b);                             \
    if (fabs(a_ - b_) > (eps)) {                                           \
      check_failures++;                                                    \
      fprintf(stderr, "%s:%d: CHECK_NEAR failed: %s = %g, %s = %g\n",      \
              __FILE__, __LINE__, #a, a_, #b, b_);                         \
    }                                                                      \
  } while (0)

#define RUN(test)                                                          \
  do {                                                                     \
    int before_ = check_failures;                                          \
    test();                                                                \
    printf("[%s] %s\n", check_failures == before_ ? "  OK  " : " FAIL ",   \
           #test);                                                         \
  } while (0)

#define CHECK_REPORT()                                                     \
  (printf("%d checks, %d failed\n", check_count, check_failures),          \
   check_failures == 0 ? 0 : 1)

#endif /* CHECK_H */
