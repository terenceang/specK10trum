#ifndef Z80_TEST_H
#define Z80_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Runs the systematic Z80 instruction test suite.
 * Returns the number of failed tests (0 if all passed). */
int run_cpu_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* Z80_TEST_H */
