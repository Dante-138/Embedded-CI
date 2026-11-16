/*
 * Proof that UndefinedBehaviorSanitizer is on: overflows a signed int, which
 * is undefined behavior in C. UBSan must report "signed integer overflow";
 * ASan does not see it, so this checks UBSan alone.
 * "make proof" fails if this program exits normally.
 *
 * Never part of the normal test suite.
 */
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	(void)argv;
	/* argc is 1, so this is INT_MAX + 1; the compiler cannot see that. */
	const int overflow = INT_MAX - 1 + argc + 1;
	printf("%d\n", overflow);
	return EXIT_SUCCESS;
}
