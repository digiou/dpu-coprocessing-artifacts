#include <time.h>

static double timespec_diff_sec(const struct timespec *end, const struct timespec *start) {
    double sec  = (double)(end->tv_sec  - start->tv_sec);
    double nsec = (double)(end->tv_nsec - start->tv_nsec);
    return sec + (nsec / 1e9);
}