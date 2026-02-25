#pragma once
#include <cstdio>
#include <cassert>

#define RATH_ASSERT(cond, msg)                                           \
do { if (!(cond)) {                                                  \
fprintf(stderr, "[RathEngine] Assert: %s | %s:%d\n",            \
msg, __FILE__, __LINE__);                                \
assert(false);                                                   \
}} while(0)
