// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef GOOGLE_PROTOBUF_STUBS_PORT_H_
#define GOOGLE_PROTOBUF_STUBS_PORT_H_

#include <util/generic/string.h>
#include <util/stream/input.h>
#include <util/stream/output.h>
#include <util/system/types.h>
#include <assert.h>
#include <cstdint>
#include <stdlib.h>
#include <cstddef>
#include <string>
#include <string.h>

#include <google/protobuf/stubs/platform_macros.h>

#include <google/protobuf/port_def.inc>

#undef PROTOBUF_LITTLE_ENDIAN
#ifdef _WIN32
  // Assuming windows is always little-endian.
  // TODO(xiaofeng): The PROTOBUF_LITTLE_ENDIAN is not only used for
  // optimization but also for correctness. We should define an
  // different macro to test the big-endian code path in coded_stream.
  #if !defined(PROTOBUF_DISABLE_LITTLE_ENDIAN_OPT_FOR_TEST)
    #define PROTOBUF_LITTLE_ENDIAN 1
  #endif
#if defined(_MSC_VER) && _MSC_VER >= 1300 && !defined(__INTEL_COMPILER)
// If MSVC has "/RTCc" set, it will complain about truncating casts at
// runtime.  This file contains some intentional truncating casts.
#pragma runtime_checks("c", off)
#endif
#else
#ifdef __APPLE__
#include <machine/endian.h>  // __BYTE_ORDER
#elif defined(__FreeBSD__)
#include <sys/endian.h>  // __BYTE_ORDER
#elif (defined(sun) || defined(__sun)) && (defined(__SVR4) || defined(__svr4__))
#error #include <sys/isa_defs.h>  // __BYTE_ORDER
#elif defined(_AIX) || defined(__TOS_AIX__)
#include <sys/machine.h>  // BYTE_ORDER
#else
#if !defined(__QNX__)
#include <endian.h>  // __BYTE_ORDER
#endif
#endif
#if ((defined(__LITTLE_ENDIAN__) && !defined(__BIG_ENDIAN__)) ||   \
     (defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN) || \
     (defined(BYTE_ORDER) && BYTE_ORDER == LITTLE_ENDIAN)) &&      \
    !defined(PROTOBUF_DISABLE_LITTLE_ENDIAN_OPT_FOR_TEST)
#define PROTOBUF_LITTLE_ENDIAN 1
#endif
#endif

// These #includes are for the byte swap functions declared later on.
#ifdef _MSC_VER
#include <stdlib.h>  // NOLINT(build/include)
#include <intrin.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#elif defined(__linux__) || defined(__ANDROID__) || defined(__CYGWIN__)
#include <byteswap.h>  // IWYU pragma: export
#endif

// Legacy: some users reference these (internal-only) macros even though we
// don't need them any more.
#if defined(_MSC_VER) && defined(PROTOBUF_USE_DLLS)
  #ifdef LIBPROTOBUF_EXPORTS
    #define LIBPROTOBUF_EXPORT __declspec(dllexport)
  #else
    #define LIBPROTOBUF_EXPORT __declspec(dllimport)
  #endif
  #ifdef LIBPROTOC_EXPORTS
    #define LIBPROTOC_EXPORT   __declspec(dllexport)
  #else
    #define LIBPROTOC_EXPORT   __declspec(dllimport)
  #endif
#else
  #define LIBPROTOBUF_EXPORT
  #define LIBPROTOC_EXPORT
#endif

#define PROTOBUF_RUNTIME_DEPRECATED(message) PROTOBUF_DEPRECATED_MSG(message)
#define GOOGLE_PROTOBUF_RUNTIME_DEPRECATED(message) \
  PROTOBUF_DEPRECATED_MSG(message)

// ===================================================================
// from google3/base/port.h

#if (defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103L || \
     (defined(_MSC_VER) && _MSC_VER >= 1900))
// Define this to 1 if the code is compiled in C++11 mode; leave it
// undefined otherwise.  Do NOT define it to 0 -- that causes
// '#ifdef LANG_CXX11' to behave differently from '#if LANG_CXX11'.
#define LANG_CXX11 1
#else
#error "Protobuf requires at least C++11."
#endif

using TProtoStringType = TString;

namespace google {
namespace protobuf {

using ConstStringParam = const TProtoStringType &;

typedef unsigned int uint;

typedef int8_t int8;
typedef int16_t int16;
typedef arc_i32 int32;
typedef arc_i64 int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef arc_ui32 uint32;
typedef arc_ui64 uint64;

static const int32 kint32max = 0x7FFFFFFF;
static const int32 kint32min = -kint32max - 1;
static const int64 kint64max = arc_i64{0x7FFFFFFFFFFFFFFF};
static const int64 kint64min = -kint64max - 1;
static const uint32 kuint32max = 0xFFFFFFFFu;
static const uint64 kuint64max = arc_ui64{0xFFFFFFFFFFFFFFFFu};

#if defined(ADDRESS_SANITIZER) || defined(THREAD_SANITIZER) ||\
    defined(MEMORY_SANITIZER)

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus
uint16_t __sanitizer_unaligned_load16(const void *p);
arc_ui32 __sanitizer_unaligned_load32(const void *p);
arc_ui64 __sanitizer_unaligned_load64(const void *p);
void __sanitizer_unaligned_store16(void *p, uint16_t v);
void __sanitizer_unaligned_store32(void *p, arc_ui32 v);
void __sanitizer_unaligned_store64(void *p, arc_ui64 v);
#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

inline uint16_t GOOGLE_UNALIGNED_LOAD16(const void *p) {
  return __sanitizer_unaligned_load16(p);
}

inline arc_ui32 GOOGLE_UNALIGNED_LOAD32(const void *p) {
  return __sanitizer_unaligned_load32(p);
}

inline arc_ui64 GOOGLE_UNALIGNED_LOAD64(const void *p) {
  return __sanitizer_unaligned_load64(p);
}

inline void GOOGLE_UNALIGNED_STORE16(void *p, uint16_t v) {
  __sanitizer_unaligned_store16(p, v);
}

inline void GOOGLE_UNALIGNED_STORE32(void *p, arc_ui32 v) {
  __sanitizer_unaligned_store32(p, v);
}

inline void GOOGLE_UNALIGNED_STORE64(void *p, arc_ui64 v) {
  __sanitizer_unaligned_store64(p, v);
}

#elif defined(GOOGLE_PROTOBUF_USE_UNALIGNED) && GOOGLE_PROTOBUF_USE_UNALIGNED

#define GOOGLE_UNALIGNED_LOAD16(_p) (*reinterpret_cast<const uint16_t *>(_p))
#define GOOGLE_UNALIGNED_LOAD32(_p) (*reinterpret_cast<const arc_ui32 *>(_p))
#define GOOGLE_UNALIGNED_LOAD64(_p) (*reinterpret_cast<const arc_ui64 *>(_p))

#define GOOGLE_UNALIGNED_STORE16(_p, _val) (*reinterpret_cast<uint16_t *>(_p) = (_val))
#define GOOGLE_UNALIGNED_STORE32(_p, _val) (*reinterpret_cast<arc_ui32 *>(_p) = (_val))
#define GOOGLE_UNALIGNED_STORE64(_p, _val) (*reinterpret_cast<arc_ui64 *>(_p) = (_val))

#else
inline uint16_t GOOGLE_UNALIGNED_LOAD16(const void *p) {
  uint16_t t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline arc_ui32 GOOGLE_UNALIGNED_LOAD32(const void *p) {
  arc_ui32 t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline arc_ui64 GOOGLE_UNALIGNED_LOAD64(const void *p) {
  arc_ui64 t;
  memcpy(&t, p, sizeof t);
  return t;
}

inline void GOOGLE_UNALIGNED_STORE16(void *p, uint16_t v) {
  memcpy(p, &v, sizeof v);
}

inline void GOOGLE_UNALIGNED_STORE32(void *p, arc_ui32 v) {
  memcpy(p, &v, sizeof v);
}

inline void GOOGLE_UNALIGNED_STORE64(void *p, arc_ui64 v) {
  memcpy(p, &v, sizeof v);
}
#endif

#if defined(GOOGLE_PROTOBUF_OS_NACL) \
    || (defined(__ANDROID__) && defined(__clang__) \
        && (__clang_major__ == 3 && __clang_minor__ == 8) \
        && (__clang_patchlevel__ < 275480))
# define GOOGLE_PROTOBUF_USE_PORTABLE_LOG2
#endif

// The following guarantees declaration of the byte swap functions.
#ifdef _MSC_VER
#define bswap_16(x) _byteswap_ushort(x)
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)

#elif defined(__APPLE__)
// Mac OS X / Darwin features
#define bswap_16(x) OSSwapInt16(x)
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)

#elif !defined(__linux__) && !defined(__ANDROID__) && !defined(__CYGWIN__)

#ifndef bswap_16
static inline uint16_t bswap_16(uint16_t x) {
  return static_cast<uint16_t>(((x & 0xFF) << 8) | ((x & 0xFF00) >> 8));
}
#define bswap_16(x) bswap_16(x)
#endif

#ifndef bswap_32
static inline arc_ui32 bswap_32(arc_ui32 x) {
  return (((x & 0xFF) << 24) |
          ((x & 0xFF00) << 8) |
          ((x & 0xFF0000) >> 8) |
          ((x & 0xFF000000) >> 24));
}
#define bswap_32(x) bswap_32(x)
#endif

#ifndef bswap_64
static inline arc_ui64 bswap_64(arc_ui64 x) {
  return (((x & arc_ui64{0xFFu}) << 56) | ((x & arc_ui64{0xFF00u}) << 40) |
          ((x & arc_ui64{0xFF0000u}) << 24) |
          ((x & arc_ui64{0xFF000000u}) << 8) |
          ((x & arc_ui64{0xFF00000000u}) >> 8) |
          ((x & arc_ui64{0xFF0000000000u}) >> 24) |
          ((x & arc_ui64{0xFF000000000000u}) >> 40) |
          ((x & arc_ui64{0xFF00000000000000u}) >> 56));
}
#define bswap_64(x) bswap_64(x)
#endif

#endif

// ===================================================================
// from google3/util/bits/bits.h

class Bits {
 public:
  static arc_ui32 Log2FloorNonZero(arc_ui32 n) {
#if defined(__GNUC__)
  return 31 ^ static_cast<arc_ui32>(__builtin_clz(n));
#elif defined(_MSC_VER)
  unsigned long where;
  _BitScanReverse(&where, n);
  return where;
#else
  return Log2FloorNonZero_Portable(n);
#endif
  }

  static arc_ui32 Log2FloorNonZero64(arc_ui64 n) {
    // Older versions of clang run into an instruction-selection failure when
    // it encounters __builtin_clzll:
    // https://bugs.chromium.org/p/nativeclient/issues/detail?id=4395
    // This includes arm-nacl-clang and clang in older Android NDK versions.
    // To work around this, when we build with those we use the portable
    // implementation instead.
#if defined(__GNUC__) && !defined(GOOGLE_PROTOBUF_USE_PORTABLE_LOG2)
  return 63 ^ static_cast<arc_ui32>(__builtin_clzll(n));
#elif defined(_MSC_VER) && defined(_M_X64)
  unsigned long where;
  _BitScanReverse64(&where, n);
  return where;
#else
  return Log2FloorNonZero64_Portable(n);
#endif
  }
 private:
  static int Log2FloorNonZero_Portable(arc_ui32 n) {
    if (n == 0)
      return -1;
    int log = 0;
    arc_ui32 value = n;
    for (int i = 4; i >= 0; --i) {
      int shift = (1 << i);
      arc_ui32 x = value >> shift;
      if (x != 0) {
        value = x;
        log += shift;
      }
    }
    assert(value == 1);
    return log;
  }

  static int Log2FloorNonZero64_Portable(arc_ui64 n) {
    const arc_ui32 topbits = static_cast<arc_ui32>(n >> 32);
    if (topbits == 0) {
      // Top bits are zero, so scan in bottom bits
      return static_cast<int>(Log2FloorNonZero(static_cast<arc_ui32>(n)));
    } else {
      return 32 + static_cast<int>(Log2FloorNonZero(topbits));
    }
  }
};

// ===================================================================
// from google3/util/endian/endian.h
PROTOBUF_EXPORT arc_ui32 ghtonl(arc_ui32 x);

class BigEndian {
 public:
#ifdef PROTOBUF_LITTLE_ENDIAN

  static uint16_t FromHost16(uint16_t x) { return bswap_16(x); }
  static uint16_t ToHost16(uint16_t x) { return bswap_16(x); }

  static arc_ui32 FromHost32(arc_ui32 x) { return bswap_32(x); }
  static arc_ui32 ToHost32(arc_ui32 x) { return bswap_32(x); }

  static arc_ui64 FromHost64(arc_ui64 x) { return bswap_64(x); }
  static arc_ui64 ToHost64(arc_ui64 x) { return bswap_64(x); }

  static bool IsLittleEndian() { return true; }

#else

  static uint16_t FromHost16(uint16_t x) { return x; }
  static uint16_t ToHost16(uint16_t x) { return x; }

  static arc_ui32 FromHost32(arc_ui32 x) { return x; }
  static arc_ui32 ToHost32(arc_ui32 x) { return x; }

  static arc_ui64 FromHost64(arc_ui64 x) { return x; }
  static arc_ui64 ToHost64(arc_ui64 x) { return x; }

  static bool IsLittleEndian() { return false; }

#endif /* ENDIAN */

  // Functions to do unaligned loads and stores in big-endian order.
  static uint16_t Load16(const void *p) {
    return ToHost16(GOOGLE_UNALIGNED_LOAD16(p));
  }

  static void Store16(void *p, uint16_t v) {
    GOOGLE_UNALIGNED_STORE16(p, FromHost16(v));
  }

  static arc_ui32 Load32(const void *p) {
    return ToHost32(GOOGLE_UNALIGNED_LOAD32(p));
  }

  static void Store32(void *p, arc_ui32 v) {
    GOOGLE_UNALIGNED_STORE32(p, FromHost32(v));
  }

  static arc_ui64 Load64(const void *p) {
    return ToHost64(GOOGLE_UNALIGNED_LOAD64(p));
  }

  static void Store64(void *p, arc_ui64 v) {
    GOOGLE_UNALIGNED_STORE64(p, FromHost64(v));
  }
};

}  // namespace protobuf
}  // namespace google

#include <google/protobuf/port_undef.inc>

#endif  // GOOGLE_PROTOBUF_STUBS_PORT_H_
