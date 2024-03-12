#ifndef UTILS_MACRO_H
#define UTILS_MACRO_H
#pragma once

#define ARRAY_SIZE(a) ((sizeof a) / (sizeof a[0]))

#ifdef __GNUC__
#	define UNUSED __attribute__((__unused__))
#else
#	define UNUSED
#endif

#endif /* UTILS_MACRO_H */
