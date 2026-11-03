#include <errno.h>
#include <unistd.h>
#include <wasi/version.h>

#ifndef __wasip1__

#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <wasi/api.h>

#if defined(__wasip2__)
typedef wasip2_list_u8_t list_u8_t;
#define list_u8_free wasip2_list_u8_free
#elif defined(__wasip3__)
typedef wasip3_list_u8_t list_u8_t;
#define list_u8_free wasip3_list_u8_free
#elif !defined(__webp2__)
#error "Unknown WASI version"
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))

int __wasilibc_random(void *buffer, size_t len) {
#if defined(__wasip2__) || defined(__wasip3__)
  // Set up a WASI byte list to receive the results
  list_u8_t wasi_list;

  // Get random bytes
  random_get_random_bytes(len, &wasi_list);

  // The spec for get-random-bytes specifies that wasi_list.len
  // will be equal to len.
  if (wasi_list.len != len)
    _Exit(EX_OSERR);
  else {
    // Copy the result
    memcpy(buffer, wasi_list.ptr, len);
  }

  // Free the WASI byte list
  list_u8_free(&wasi_list);
#elif defined(__webp2__)
  webp2_own_crypto_t crypto = webp2_get_global_crypto();
  const size_t MAX_CHUNK_SIZE = 65536;

  // Attempt to reduce copies by repeatedly reusing the same Uint8Array. This
  // is obviously extremely jank, and doesn't avoid the obvious extra copy
  // (host -> ret -> buffer). But at least we reduce allocation and don't copy
  // the param.
  uint32_t initial_alloc = (uint32_t)MIN(len, MAX_CHUNK_SIZE);
  webp2_own_uint_8_array_t arr = webp2_constructor_uint_8_array(initial_alloc);
  for (size_t done = 0; done < len;) {
    size_t remaining = len - done;
    if (remaining < initial_alloc) {
      // "Resize" the input array.
      uint32_t remaining32 = remaining;
      webp2_own_uint_8_array_t arr2 = webp2_method_uint_8_array_subarray(
          webp2_borrow_uint_8_array(arr), 0, &remaining32);
      webp2_uint_8_array_drop_own(arr);
      arr = arr2;
    }

    webp2_list_u8_t ret;
    webp2_method_crypto_get_random_values(webp2_borrow_crypto(crypto),
                                          webp2_borrow_uint_8_array(arr), &ret);
    if (ret.len != (remaining < MAX_CHUNK_SIZE ? remaining : MAX_CHUNK_SIZE)) {
      _Exit(EX_OSERR);
    }
    memcpy((char *)buffer + done, ret.ptr, ret.len);
    done += ret.len;
    webp2_list_u8_free(&ret);
  }

  webp2_uint_8_array_drop_own(arr);
  webp2_crypto_drop_own(crypto);
#else
#error "Unknown WASI version"
#endif

  return 0;
}

#endif
