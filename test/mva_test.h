#ifndef _MVA_TEST_H
#define _MVA_TEST_H

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(_arr) (sizeof(_arr)/sizeof(_arr[0]))
#endif

#define MVA_CHECK(cond, _ret, reason, ...) if (cond) { printf((reason), ## __VA_ARGS__); return (_ret); } else {}
#endif
