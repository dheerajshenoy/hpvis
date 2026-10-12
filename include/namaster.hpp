#pragma once

// namaster.h is C99: it uses `double complex`, which C++ lacks. GCC and Clang
// accept the `_Complex` keyword in C++ as an extension, so map `complex` to it
// only while the header is read.
#include <complex.h>
#include <complex>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
// HEALPix's datatypes.h already defines fcomplex (as std::complex<float>)
#define complex _Complex
#define fcomplex nmt_fcomplex
extern "C"
{
#include <namaster.h>
}
#undef fcomplex
#undef complex
#pragma GCC diagnostic pop
