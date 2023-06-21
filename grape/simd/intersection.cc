/**
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 */

#include "grape/simd/intersection.h"

namespace SIMDCompressionLib {

/**
 * This is often called galloping or exponential search.
 *
 * Used by frogintersectioncardinality below
 *
 * Based on binary search...
 * Find the smallest integer larger than pos such
 * that array[pos]>= min.
 * If none can be found, return array.length.
 * From code by O. Kaser.
 */
static size_t __frogadvanceUntil(const uint32_t *array, const size_t pos,
                                 const size_t length, const size_t min) {
  size_t lower = pos + 1;

  // special handling for a possibly common sequential case
  if ((lower >= length) or (array[lower] >= min)) {
    return lower;
  }

  size_t spansize = 1; // could set larger
  // bootstrap an upper limit

  while ((lower + spansize < length) and (array[lower + spansize] < min))
    spansize *= 2;
  size_t upper = (lower + spansize < length) ? lower + spansize : length - 1;

  if (array[upper] < min) { // means array has no item >= min
    return length;
  }

  // we know that the next-smallest span was too small
  lower += (spansize / 2);

  // else begin binary search
  size_t mid = 0;
  while (lower + 1 != upper) {
    mid = (lower + upper) / 2;
    if (array[mid] == min) {
      return mid;
    } else if (array[mid] < min)
      lower = mid;
    else
      upper = mid;
  }
  return upper;
}

size_t onesidedgallopingintersection(const uint32_t *smallset,
                                     const size_t smalllength,
                                     const uint32_t *largeset,
                                     const size_t largelength, uint32_t *out) {
  if (largelength < smalllength)
    return onesidedgallopingintersection(largeset, largelength, smallset,
                                         smalllength, out);
  if (0 == smalllength)
    return 0;
  const uint32_t *const initout(out);
  size_t k1 = 0, k2 = 0;
  while (true) {
    if (largeset[k1] < smallset[k2]) {
      k1 = __frogadvanceUntil(largeset, k1, largelength, smallset[k2]);
      if (k1 == largelength)
        break;
    }
  midpoint:
    if (smallset[k2] < largeset[k1]) {
      ++k2;
      if (k2 == smalllength)
        break;
    } else {
      *out++ = smallset[k2];
      ++k2;
      if (k2 == smalllength)
        break;
      k1 = __frogadvanceUntil(largeset, k1, largelength, smallset[k2]);
      if (k1 == largelength)
        break;
      goto midpoint;
    }
  }
  return out - initout;
}

// from: http://didawiki.di.unipi.it/doku.php/magistraleinformaticanetworking/ae/ae2019/start#books_notes_etc
// "The magic of Algorithms! "
// Chap. 6, Algorithm 6.1 Intersection based on Mutual Partitioning
//
size_t mutualPartitioningIntersect(const uint32_t* small_set, size_t small_length,
                                   const uint32_t* large_set, size_t large_length,
                                   uint32_t *  result) {
    if ((small_length <= 0) || (large_length <= 0)) {
        return 0;
    }
    if (small_length > large_length) {
        return mutualPartitioningIntersect(large_set, large_length, small_set, small_length, result);
    }
    int mid_index = small_length / 2;
    const auto mid_val = small_set[mid_index];
    auto it = std::lower_bound(large_set, large_set + large_length, mid_val);
    size_t out_num = mutualPartitioningIntersect(small_set, mid_index, large_set, it - large_set, result);
    if (it == large_set + large_length) {
        return out_num;
    }
    result += out_num;
    if (*it == mid_val) {
        *result++ = mid_val;
        ++it;
        ++out_num;
    }
    ++mid_index;
    return out_num + mutualPartitioningIntersect(small_set + mid_index, small_length - mid_index,
                                                 it, large_set + large_length - it, result);
}

/**
 * Fast scalar scheme designed by N. Kurz.
 */
size_t scalar(const uint32_t *A, const size_t lenA, const uint32_t *B,
              const size_t lenB, uint32_t *out) {
  const uint32_t *const initout(out);
  if (lenA == 0 || lenB == 0)
    return 0;

  const uint32_t *endA = A + lenA;
  const uint32_t *endB = B + lenB;

  while (1) {
    while (*A < *B) {
    SKIP_FIRST_COMPARE:
      if (++A == endA)
        return (out - initout);
    }
    while (*A > *B) {
      if (++B == endB)
        return (out - initout);
    }
    if (*A == *B) {
      *out++ = *A;
      if (++A == endA || ++B == endB)
        return (out - initout);
    } else {
      goto SKIP_FIRST_COMPARE;
    }
  }

  return (out - initout); // NOTREACHED
}

size_t match_scalar(const uint32_t *A, const size_t lenA, const uint32_t *B,
                    const size_t lenB, uint32_t *out) {

  const uint32_t *initout = out;
  if (lenA == 0 || lenB == 0)
    return 0;

  const uint32_t *endA = A + lenA;
  const uint32_t *endB = B + lenB;

  while (1) {
    while (*A < *B) {
    SKIP_FIRST_COMPARE:
      if (++A == endA)
        goto FINISH;
    }
    while (*A > *B) {
      if (++B == endB)
        goto FINISH;
    }
    if (*A == *B) {
      *out++ = *A;
      if (++A == endA || ++B == endB)
        goto FINISH;
    } else {
      goto SKIP_FIRST_COMPARE;
    }
  }

FINISH:
  return (out - initout);
}

#ifdef __GNUC__
#define COMPILER_LIKELY(x) __builtin_expect((x), 1)
#define COMPILER_RARELY(x) __builtin_expect((x), 0)
#else
#define COMPILER_LIKELY(x) x
#define COMPILER_RARELY(x) x
#endif

/**
 * Intersections scheme designed by N. Kurz that works very
 * well when intersecting an array with another where the density
 * differential is small (between 2 to 10).
 *
 * It assumes that lenRare <= lenFreq.
 *
 * Note that this is not symmetric: flipping the rare and freq pointers
 * as well as lenRare and lenFreq could lead to significant performance
 * differences.
 *
 * The matchOut pointer can safely be equal to the rare pointer.
 *
 */
size_t v1(const uint32_t *rare, size_t lenRare, const uint32_t *freq,
          size_t lenFreq, uint32_t *matchOut) {
  if(matchOut == freq) { throw invalid_argument("matchOut should not be freq, when in doubt, use a distinct output buffer."); }
  if(lenRare > lenFreq)  { throw invalid_argument("mismatch freq/rare (programming error?)."); }
  const uint32_t *matchOrig = matchOut;
  if (lenFreq == 0 || lenRare == 0)
    return 0;

  const uint64_t kFreqSpace = 2 * 4 * (0 + 1) - 1;
  const uint64_t kRareSpace = 0;

  const uint32_t *stopFreq = &freq[lenFreq] - kFreqSpace;
  const uint32_t *stopRare = &rare[lenRare] - kRareSpace;

  __m128i Rare;

  __m128i F0, F1;

  if (COMPILER_RARELY((rare >= stopRare) || (freq >= stopFreq)))
    goto FINISH_SCALAR;
  uint32_t valRare;
  valRare = rare[0];
  Rare = _mm_set1_epi32(valRare);

  uint64_t maxFreq;
  maxFreq = freq[2 * 4 - 1];
  F0 = _mm_lddqu_si128(reinterpret_cast<const __m128i *>(freq));
  F1 = _mm_lddqu_si128(reinterpret_cast<const __m128i *>(freq + 4));

  if (COMPILER_RARELY(maxFreq < valRare))
    goto ADVANCE_FREQ;

ADVANCE_RARE:
  do {
    *matchOut = valRare;
    rare += 1;
    if (COMPILER_RARELY(rare >= stopRare)) {
      rare -= 1;
      goto FINISH_SCALAR;
    }
    valRare = rare[0]; // for next iteration
    F0 = _mm_cmpeq_epi32(F0, Rare);
    F1 = _mm_cmpeq_epi32(F1, Rare);
    Rare = _mm_set1_epi32(valRare);
    F0 = _mm_or_si128(F0, F1);
#ifdef __SSE4_1__
    if (_mm_testz_si128(F0, F0) == 0)
      matchOut++;
#else
    if (_mm_movemask_epi8(F0))
      matchOut++;
#endif
    F0 = _mm_lddqu_si128(reinterpret_cast<const __m128i *>(freq));
    F1 = _mm_lddqu_si128(reinterpret_cast<const __m128i *>(freq + 4));

  } while (maxFreq >= valRare);

  uint64_t maxProbe;

ADVANCE_FREQ:
  do {
    const uint64_t kProbe = (0 + 1) * 2 * 4;
    const uint32_t *probeFreq = freq + kProbe;

    if (COMPILER_RARELY(probeFreq >= stopFreq)) {
      goto FINISH_SCALAR;
    }
    maxProbe = freq[(0 + 2) * 2 * 4 - 1];

    freq = probeFreq;

  } while (maxProbe < valRare);

  maxFreq = maxProbe;

  F0 = _mm_lddqu_si128(reinterpret_cast<const __m128i *>(freq));
  F1 = _mm_lddqu_si128(reinterpret_cast<const __m128i *>(freq + 4));

  goto ADVANCE_RARE;

  size_t count;
FINISH_SCALAR:
  count = matchOut - matchOrig;

  lenFreq = stopFreq + kFreqSpace - freq;
  lenRare = stopRare + kRareSpace - rare;

  size_t tail = match_scalar(freq, lenFreq, rare, lenRare, matchOut);

  return count + tail;
}

/**
 * This intersection function is similar to v1, but is faster when
 * the difference between lenRare and lenFreq is large, but not too large.

 * It assumes that lenRare <= lenFreq.
 *
 * Note that this is not symmetric: flipping the rare and freq pointers
 * as well as lenRare and lenFreq could lead to significant performance
 * differences.
 *
 * The out pointer can safely be equal to the rare pointer.
 *
 * This function DOES NOT use inline assembly instructions. Just intrinsics.
 */
size_t v3(const uint32_t *rare, const size_t lenRare, const uint32_t *freq,
          const size_t lenFreq, uint32_t *out) {
  if (lenFreq == 0 || lenRare == 0)
    return 0;
  if(out == freq) { throw invalid_argument("matchOut should not be freq, when in doubt, use a distinct output buffer."); }
  if(lenRare > lenFreq)  { throw invalid_argument("mismatch freq/rare (programming error?)."); }
  const uint32_t *const initout(out);
  typedef __m128i vec;
  const uint32_t veclen = sizeof(vec) / sizeof(uint32_t);
  const size_t vecmax = veclen - 1;
  const size_t freqspace = 32 * veclen;
  const size_t rarespace = 1;

  const uint32_t *stopFreq = freq + lenFreq - freqspace;
  const uint32_t *stopRare = rare + lenRare - rarespace;
  if (freq > stopFreq) {
    return scalar(freq, lenFreq, rare, lenRare, out);
  }
  while (freq[veclen * 31 + vecmax] < *rare) {
    freq += veclen * 32;
    if (freq > stopFreq)
      goto FINISH_SCALAR;
  }
  for (; rare < stopRare; ++rare) {
    const uint32_t matchRare = *rare; // nextRare;
    const vec Match = _mm_set1_epi32(matchRare);
    while (freq[veclen * 31 + vecmax] < matchRare) { // if no match possible
      freq += veclen * 32;                           // advance 32 vectors
      if (freq > stopFreq)
        goto FINISH_SCALAR;
    }
    vec Q0, Q1, Q2, Q3;
    if (freq[veclen * 15 + vecmax] >= matchRare) {
      if (freq[veclen * 7 + vecmax] < matchRare) {
        Q0 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9),
                Match));
        Q1 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11),
                Match));

        Q2 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13),
                Match));
        Q3 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15),
                Match));
      } else {
        Q0 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5),
                Match));
        Q1 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7),
                Match));
        Q2 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1),
                Match));
        Q3 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3),
                Match));
      }
    } else {
      if (freq[veclen * 23 + vecmax] < matchRare) {
        Q0 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9 + 16),
                Match));
        Q1 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11 + 16),
                Match));

        Q2 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13 + 16),
                Match));
        Q3 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15 + 16),
                Match));
      } else {
        Q0 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5 + 16),
                Match));
        Q1 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7 + 16),
                Match));
        Q2 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1 + 16),
                Match));
        Q3 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3 + 16),
                Match));
      }
    }
    const vec F0 = _mm_or_si128(_mm_or_si128(Q0, Q1), _mm_or_si128(Q2, Q3));
#ifdef __SSE4_1__
    if (_mm_testz_si128(F0, F0)) {
#else
    if (!_mm_movemask_epi8(F0)) {
#endif
    } else {
      *out++ = matchRare;
    }
  }

FINISH_SCALAR:
  return (out - initout) + scalar(freq, stopFreq + freqspace - freq, rare,
                                  stopRare + rarespace - rare, out);
}

/**
 * This is the SIMD galloping function. This intersection function works well
 * when lenRare and lenFreq have vastly different values.
 *
 * It assumes that lenRare <= lenFreq.
 *
 * Note that this is not symmetric: flipping the rare and freq pointers
 * as well as lenRare and lenFreq could lead to significant performance
 * differences.
 *
 * The out pointer can safely be equal to the rare pointer.
 *
 * This function DOES NOT use assembly. It only relies on intrinsics.
 */
size_t SIMDgalloping(const uint32_t *rare, const size_t lenRare,
                     const uint32_t *freq, const size_t lenFreq,
                     uint32_t *out) {
  if (lenFreq == 0 || lenRare == 0)
    return 0;
  if(out == freq) { throw invalid_argument("matchOut should not be freq, when in doubt, use a distinct output buffer."); }
  if(lenRare > lenFreq)  { throw invalid_argument("mismatch freq/rare (programming error?)."); }
  const uint32_t *const initout(out);
  typedef __m128i vec;
  const uint32_t veclen = sizeof(vec) / sizeof(uint32_t);
  const size_t vecmax = veclen - 1;
  const size_t freqspace = 32 * veclen;
  const size_t rarespace = 1;

  const uint32_t *stopFreq = freq + lenFreq - freqspace;
  const uint32_t *stopRare = rare + lenRare - rarespace;
  if (freq > stopFreq) {
    return scalar(freq, lenFreq, rare, lenRare, out);
  }
  for (; rare < stopRare; ++rare) {
    const uint32_t matchRare = *rare; // nextRare;
    const vec Match = _mm_set1_epi32(matchRare);

    if (freq[veclen * 31 + vecmax] < matchRare) { // if no match possible
      uint32_t offset = 1;
      if (freq + veclen * 32 > stopFreq) {
        freq += veclen * 32;
        goto FINISH_SCALAR;
      }
      while (freq[veclen * offset * 32 + veclen * 31 + vecmax] <
             matchRare) { // if no match possible
        if (freq + veclen * (2 * offset) * 32 <= stopFreq) {
          offset *= 2;
        } else if (freq + veclen * (offset + 1) * 32 <= stopFreq) {
          offset = static_cast<uint32_t>((stopFreq - freq) / (veclen * 32));
          // offset += 1;
          if (freq[veclen * offset * 32 + veclen * 31 + vecmax] < matchRare) {
            freq += veclen * offset * 32;
            goto FINISH_SCALAR;
          } else {
            break;
          }
        } else {
          freq += veclen * offset * 32;
          goto FINISH_SCALAR;
        }
      }
      uint32_t lower = offset / 2;
      while (lower + 1 != offset) {
        const uint32_t mid = (lower + offset) / 2;
        if (freq[veclen * mid * 32 + veclen * 31 + vecmax] < matchRare)
          lower = mid;
        else
          offset = mid;
      }
      freq += veclen * offset * 32;
    }
    vec Q0, Q1, Q2, Q3;
    if (freq[veclen * 15 + vecmax] >= matchRare) {
      if (freq[veclen * 7 + vecmax] < matchRare) {
        Q0 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9),
                Match));
        Q1 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11),
                Match));

        Q2 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13),
                Match));
        Q3 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15),
                Match));
      } else {
        Q0 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5),
                Match));
        Q1 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7),
                Match));
        Q2 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1),
                Match));
        Q3 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3),
                Match));
      }
    } else {
      if (freq[veclen * 23 + vecmax] < matchRare) {
        Q0 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 8 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 9 + 16),
                Match));
        Q1 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 10 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 11 + 16),
                Match));

        Q2 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 12 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 13 + 16),
                Match));
        Q3 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 14 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 15 + 16),
                Match));
      } else {
        Q0 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 4 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 5 + 16),
                Match));
        Q1 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 6 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 7 + 16),
                Match));
        Q2 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 0 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 1 + 16),
                Match));
        Q3 = _mm_or_si128(
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 2 + 16),
                Match),
            _mm_cmpeq_epi32(
                _mm_loadu_si128(reinterpret_cast<const vec *>(freq) + 3 + 16),
                Match));
      }
    }
    const vec F0 = _mm_or_si128(_mm_or_si128(Q0, Q1), _mm_or_si128(Q2, Q3));
#ifdef __SSE4_1__
    if (_mm_testz_si128(F0, F0)) {
#else
    if (!_mm_movemask_epi8(F0)) {
#endif
    } else {
      *out++ = matchRare;
    }
  }

FINISH_SCALAR:
  return (out - initout) + scalar(freq, stopFreq + freqspace - freq, rare,
                                  stopRare + rarespace - rare, out);
}

/**
 * Our main heuristic.
 *
 * The out pointer can be set1 if length1<=length2,
 * or else it can be set2 if length1>length2.
 */
size_t SIMDintersection(const uint32_t *set1, const size_t length1,
                        const uint32_t *set2, const size_t length2,
                        uint32_t *out) {
  if (((length1 > length2) && (out == set1)) ||  ((length2 > length1) && (out == set2))) {
    throw invalid_argument("out should not be equal to the largest array.");
  }
  if ((length1 == 0) or (length2 == 0))
    return 0;

  if ((1000 * length1 <= length2) or (1000 * length2 <= length1)) {
    if (length1 <= length2)
      return SIMDgalloping(set1, length1, set2, length2, out);
    else
      return SIMDgalloping(set2, length2, set1, length1, out);
  }

  if ((50 * length1 <= length2) or (50 * length2 <= length1)) {
    if (length1 <= length2)
      return v3(set1, length1, set2, length2, out);
    else
      return v3(set2, length2, set1, length1, out);
  }
  if (length1 == length2) {
      if(out == set1)
        return v1(set1, length1, set2, length2, out);
      else 
        return v1(set2, length2, set1, length1, out);
  }
  if (length1 <= length2)
    return v1(set1, length1, set2, length2, out);
  else
    return v1(set2, length2, set1, length1, out);
}

#ifdef __AVX2__

size_t v1_avx2
(const uint32_t *rare, size_t lenRare,
 const uint32_t *freq, size_t lenFreq,
 uint32_t *matchOut) {
    assert(lenRare <= lenFreq);
    const uint32_t *matchOrig = matchOut;
    if (lenFreq == 0 || lenRare == 0) return 0;

    const uint64_t kFreqSpace = 2 * 4 * (0 + 1) - 1;
    const uint64_t kRareSpace = 0;

    const uint32_t *stopFreq = &freq[lenFreq] - kFreqSpace;
    const uint32_t *stopRare = &rare[lenRare] - kRareSpace;

    __m256i  Rare;

    __m256i F;

    if (COMPILER_RARELY( (rare >= stopRare) || (freq >= stopFreq) )) goto FINISH_SCALAR;
    uint32_t valRare;
    valRare = rare[0];
    Rare = _mm256_set1_epi32(valRare);

    uint64_t maxFreq;
    maxFreq = freq[2 * 4 - 1];
    F = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(freq));


    if (COMPILER_RARELY(maxFreq < valRare)) goto ADVANCE_FREQ;

ADVANCE_RARE:
    do {
        *matchOut = valRare;
        valRare = rare[1]; // for next iteration
        rare += 1;
        if (COMPILER_RARELY(rare >= stopRare)) {
            rare -= 1;
            goto FINISH_SCALAR;
        }
        F =  _mm256_cmpeq_epi32(F,Rare);
        Rare = _mm256_set1_epi32(valRare);
        if(_mm256_testz_si256(F,F) == 0)
          matchOut ++;
        F = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(freq));

    } while (maxFreq >= valRare);

    uint64_t maxProbe;

ADVANCE_FREQ:
    do {
        const uint64_t kProbe = (0 + 1) * 2 * 4;
        const uint32_t *probeFreq = freq + kProbe;
        maxProbe = freq[(0 + 2) * 2 * 4 - 1];

        if (COMPILER_RARELY(probeFreq >= stopFreq)) {
            goto FINISH_SCALAR;
        }

        freq = probeFreq;

    } while (maxProbe < valRare);

    maxFreq = maxProbe;

    F = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(freq));


    goto ADVANCE_RARE;

    size_t count;
FINISH_SCALAR:
    count = matchOut - matchOrig;

    lenFreq = stopFreq + kFreqSpace - freq;
    lenRare = stopRare + kRareSpace - rare;

    size_t tail = match_scalar(freq, lenFreq, rare, lenRare, matchOut);

    return count + tail;
}

size_t v3_avx2(const uint32_t *rare, const size_t lenRare,
        const uint32_t *freq, const size_t lenFreq, uint32_t * out) {
    if (lenFreq == 0 || lenRare == 0)
        return 0;
    assert(lenRare <= lenFreq);
    const uint32_t * const initout (out);
    typedef __m256i vec;
    const uint32_t veclen = sizeof(vec) / sizeof(uint32_t);
    const size_t vecmax = veclen - 1;
    const size_t freqspace = 32 * veclen;
    const size_t rarespace = 1;

    const uint32_t *stopFreq = freq + lenFreq - freqspace;
    const uint32_t *stopRare = rare + lenRare - rarespace;
    if (freq > stopFreq) {
        return scalar(freq, lenFreq, rare, lenRare, out);
    }
    while (freq[veclen * 31 + vecmax] < *rare) {
        freq += veclen * 32;
        if (freq > stopFreq)
            goto FINISH_SCALAR;
    }
    for (; rare < stopRare; ++rare) {
        const uint32_t matchRare = *rare;//nextRare;
        const vec Match = _mm256_set1_epi32(matchRare);
        while (freq[veclen * 31 + vecmax] < matchRare) { // if no match possible
            freq += veclen * 32; // advance 32 vectors
            if (freq > stopFreq)
                goto FINISH_SCALAR;
        }
        vec Q0,Q1,Q2,Q3;
        if(freq[veclen * 15 + vecmax] >= matchRare  ) {
        if(freq[veclen * 7 + vecmax] < matchRare  ) {
            Q0 = _mm256_or_si256(
            		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 8), Match),
					_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 9), Match));
            Q1 = _mm256_or_si256(
            		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 10), Match),
					_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 11), Match));

            Q2 = _mm256_or_si256(
            		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 12), Match),
					_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 13), Match));
            Q3 = _mm256_or_si256(
            		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 14), Match),
					_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 15), Match));
        } else {
            Q0 = _mm256_or_si256(
            		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 4), Match),
					_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 5), Match));
            Q1 = _mm256_or_si256(
            		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 6), Match),
					_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 7), Match));
            Q2 = _mm256_or_si256(
            		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 0), Match),
					_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 1), Match));
            Q3 = _mm256_or_si256(
            		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 2), Match),
					_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 3), Match));
        }
        }
        else
        {
            if(freq[veclen * 23 + vecmax] < matchRare  ) {
                Q0 = _mm256_or_si256(
                		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 8 + 16), Match),
						_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 9 + 16), Match));
                Q1 = _mm256_or_si256(
                		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 10+ 16), Match),
						_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 11+ 16), Match));

                Q2 = _mm256_or_si256(
                		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 12+ 16), Match),
						_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 13+ 16), Match));
                Q3 = _mm256_or_si256(
                		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 14+ 16), Match),
						_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 15+ 16), Match));
            } else {
                Q0 = _mm256_or_si256(
                		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 4+ 16), Match),
						_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 5+ 16), Match));
                Q1 = _mm256_or_si256(
                		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 6+ 16), Match),
						_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 7+ 16), Match));
                Q2 = _mm256_or_si256(
                		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 0+ 16), Match),
						_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 1+ 16), Match));
                Q3 = _mm256_or_si256(
                		_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 2+ 16), Match),
						_mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 3+ 16), Match));
            }

        }
        const vec F0 = _mm256_or_si256(_mm256_or_si256(Q0, Q1),_mm256_or_si256(Q2, Q3));
        if (_mm256_testz_si256(F0, F0)) {
        } else {
            *out++ = matchRare;
        }
    }

    FINISH_SCALAR: return (out - initout) + scalar(freq,
            stopFreq + freqspace - freq, rare, stopRare + rarespace - rare, out);
}

size_t SIMDgalloping_avx2(const uint32_t *rare, const size_t lenRare,
        const uint32_t *freq, const size_t lenFreq, uint32_t * out) {
    if (lenFreq == 0 || lenRare == 0)
        return 0;
    assert(lenRare <= lenFreq);
    const uint32_t * const initout (out);
    typedef __m256i vec;
    const uint32_t veclen = sizeof(vec) / sizeof(uint32_t);
    const size_t vecmax = veclen - 1;
    const size_t freqspace = 32 * veclen;
    const size_t rarespace = 1;

    const uint32_t *stopFreq = freq + lenFreq - freqspace;
    const uint32_t *stopRare = rare + lenRare - rarespace;
    if (freq > stopFreq) {
        return scalar(freq, lenFreq, rare, lenRare, out);
    }
    for (; rare < stopRare; ++rare) {
        const uint32_t matchRare = *rare;//nextRare;
        const vec Match = _mm256_set1_epi32(matchRare);

        if (freq[veclen * 31 + vecmax] < matchRare) { // if no match possible
            uint32_t offset = 1;
            if (freq + veclen  * 32 > stopFreq) {
                freq += veclen * 32;
                goto FINISH_SCALAR;
            }
            while (freq[veclen * offset * 32 + veclen * 31 + vecmax]
                    < matchRare) { // if no match possible
                if (freq + veclen * (2 * offset ) * 32 <= stopFreq) {
                    offset *= 2;
                } else if (freq + veclen * (offset + 1) * 32 <= stopFreq) {
                    offset = static_cast<uint32_t>((stopFreq - freq ) / (veclen * 32));
                    //offset += 1;
                    if (freq[veclen * offset * 32 + veclen * 31 + vecmax]
                                    < matchRare) {
                       freq += veclen * offset * 32;
                       goto FINISH_SCALAR;
                    } else {
                       break;
                    }
                } else {
                    freq += veclen * offset * 32;
                    goto FINISH_SCALAR;
                }
            }
            uint32_t lower = offset / 2;
            while (lower + 1 != offset) {
                const uint32_t mid = (lower + offset) / 2;
                if (freq[veclen * mid * 32 + veclen * 31 + vecmax]
                        < matchRare)
                    lower = mid;
                else
                    offset = mid;
            }
            freq += veclen * offset * 32;
        }
        vec Q0,Q1,Q2,Q3;
        if (freq[veclen * 15 + vecmax] >= matchRare) {
            if (freq[veclen * 7 + vecmax] < matchRare) {
                Q0
                        = _mm256_or_si256(
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 8), Match),
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 9), Match));
                Q1 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 10),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 11),
                                Match));

                Q2 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 12),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 13),
                                Match));
                Q3 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 14),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 15),
                                Match));
            } else {
                Q0
                        = _mm256_or_si256(
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 4), Match),
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 5), Match));
                Q1
                        = _mm256_or_si256(
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 6), Match),
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 7), Match));
                Q2
                        = _mm256_or_si256(
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 0), Match),
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 1), Match));
                Q3
                        = _mm256_or_si256(
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 2), Match),
                                _mm256_cmpeq_epi32(
                                        _mm256_loadu_si256((vec *) freq + 3), Match));
            }
        } else {
            if (freq[veclen * 23 + vecmax] < matchRare) {
                Q0 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 8 + 16),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 9 + 16),
                                Match));
                Q1 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 10 + 16),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 11 + 16),
                                Match));

                Q2 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 12 + 16),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 13 + 16),
                                Match));
                Q3 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 14 + 16),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 15 + 16),
                                Match));
            } else {
                Q0 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 4 + 16),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 5 + 16),
                                Match));
                Q1 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 6 + 16),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 7 + 16),
                                Match));
                Q2 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 0 + 16),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 1 + 16),
                                Match));
                Q3 = _mm256_or_si256(
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 2 + 16),
                                Match),
                        _mm256_cmpeq_epi32(_mm256_loadu_si256((vec *) freq + 3 + 16),
                                Match));
            }

        }
        const vec F0 = _mm256_or_si256(_mm256_or_si256(Q0, Q1),_mm256_or_si256(Q2, Q3));
        if (_mm256_testz_si256(F0, F0)) {
        } else {
            *out++ = matchRare;
        }
    }

    FINISH_SCALAR: return (out - initout) + scalar(freq,
            stopFreq + freqspace - freq, rare, stopRare + rarespace - rare, out);
}

/**
 * Our main heuristic.
 *
 * The out pointer can be set1 if length1<=length2,
 * or else it can be set2 if length1>length2.
 */
size_t SIMDintersection_avx2(const uint32_t *set1, const size_t length1,
                        const uint32_t *set2, const size_t length2,
                        uint32_t *out) {
  if ((length1 == 0) or (length2 == 0))
    return 0;

  if ((1000 * length1 <= length2) or (1000 * length2 <= length1)) {
    if (length1 <= length2)
      return SIMDgalloping_avx2(set1, length1, set2, length2, out);
    else
      return SIMDgalloping_avx2(set2, length2, set1, length1, out);
  }

  if ((50 * length1 <= length2) or (50 * length2 <= length1)) {
    if (length1 <= length2)
      return v3_avx2(set1, length1, set2, length2, out);
    else
      return v3_avx2(set2, length2, set1, length1, out);
  }

  if (length1 <= length2)
    return v1_avx2(set1, length1, set2, length2, out);
  else
    return v1_avx2(set2, length2, set1, length1, out);
}

#endif



/**
 * More or less from
 * http://highlyscalable.wordpress.com/2012/06/05/fast-intersection-sorted-lists-sse/
 */
const static __m128i shuffle_mask[16] = {
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 7, 6, 5, 4),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 11, 10, 9, 8),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 11, 10, 9, 8, 3, 2, 1, 0),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 11, 10, 9, 8, 7, 6, 5, 4),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 15, 14, 13, 12),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 15, 14, 13, 12, 3, 2, 1, 0),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 15, 14, 13, 12, 7, 6, 5, 4),
    _mm_set_epi8(15, 14, 13, 12, 15, 14, 13, 12, 7, 6, 5, 4, 3, 2, 1, 0),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 15, 14, 13, 12, 11, 10, 9, 8),
    _mm_set_epi8(15, 14, 13, 12, 15, 14, 13, 12, 11, 10, 9, 8, 3, 2, 1, 0),
    _mm_set_epi8(15, 14, 13, 12, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4),
    _mm_set_epi8(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0),
};
// precomputed dictionary

/**
 * Taken almost verbatim from
 * http://highlyscalable.wordpress.com/2012/06/05/fast-intersection-sorted-lists-sse/
 *
 * It is not safe for out to be either A or B.
 */
size_t highlyscalable_intersect_SIMD(const uint32_t *A, const size_t s_a,
                                     const uint32_t *B, const size_t s_b,
                                     uint32_t *out) {
  assert(out != A);
  assert(out != B);
  const uint32_t *const initout(out);
  size_t i_a = 0, i_b = 0;

  // trim lengths to be a multiple of 4
  size_t st_a = (s_a / 4) * 4;
  size_t st_b = (s_b / 4) * 4;

  while (i_a < st_a && i_b < st_b) {
    //[ load segments of four 32-bit elements
    __m128i v_a = _mm_loadu_si128((__m128i *)&A[i_a]);
    __m128i v_b = _mm_loadu_si128((__m128i *)&B[i_b]);
    //]

    //[ move pointers
    const uint32_t a_max = A[i_a + 3];
    const uint32_t b_max = B[i_b + 3];
    i_a += (a_max <= b_max) * 4;
    i_b += (a_max >= b_max) * 4;
    //]

    //[ compute mask of common elements
    const uint32_t cyclic_shift = _MM_SHUFFLE(0, 3, 2, 1);
    __m128i cmp_mask1 = _mm_cmpeq_epi32(v_a, v_b); // pairwise comparison
    v_b = _mm_shuffle_epi32(v_b, cyclic_shift);    // shuffling
    __m128i cmp_mask2 = _mm_cmpeq_epi32(v_a, v_b); // again...
    v_b = _mm_shuffle_epi32(v_b, cyclic_shift);
    __m128i cmp_mask3 = _mm_cmpeq_epi32(v_a, v_b); // and again...
    v_b = _mm_shuffle_epi32(v_b, cyclic_shift);
    __m128i cmp_mask4 = _mm_cmpeq_epi32(v_a, v_b); // and again.
    __m128i cmp_mask = _mm_or_si128(
        _mm_or_si128(cmp_mask1, cmp_mask2),
        _mm_or_si128(cmp_mask3, cmp_mask4)); // OR-ing of comparison masks
    // convert the 128-bit mask to the 4-bit mask
    const int mask = _mm_movemask_ps(_mm_castsi128_ps(cmp_mask));
    //]

    //[ copy out common elements
    const __m128i p = _mm_shuffle_epi8(v_a, shuffle_mask[mask]);
    _mm_storeu_si128((__m128i *)out, p);
    out += _mm_popcnt_u32(mask); // a number of elements is a weight of the mask
                                 //]
  }

  // intersect the tail using scalar intersection
  while (i_a < s_a && i_b < s_b) {
    if (A[i_a] < B[i_b]) {
      i_a++;
    } else if (B[i_b] < A[i_a]) {
      i_b++;
    } else {
      *out++ = B[i_b];
      ;
      i_a++;
      i_b++;
    }
  }

  return out - initout;
}

/**
 * Version optimized by D. Lemire
 * starting from
 * http://highlyscalable.wordpress.com/2012/06/05/fast-intersection-sorted-lists-sse/
 *
 * The main difference is that we break the data dependency and maximizes
 * superscalar execution.
 *
 * It is not safe for out to be either A or B.
 */
size_t lemire_highlyscalable_intersect_SIMD(const uint32_t *A, const size_t s_a,
                                            const uint32_t *B, const size_t s_b,
                                            uint32_t *out) {
  assert(out != A);
  assert(out != B);
  const uint32_t *const initout(out);
  size_t i_a = 0, i_b = 0;
  const static uint32_t cyclic_shift1 = _MM_SHUFFLE(0, 3, 2, 1);
  const static uint32_t cyclic_shift2 = _MM_SHUFFLE(1, 0, 3, 2);
  const static uint32_t cyclic_shift3 = _MM_SHUFFLE(2, 1, 0, 3);

  // trim lengths to be a multiple of 4
  size_t st_a = (s_a / 4) * 4;
  size_t st_b = (s_b / 4) * 4;
  if (i_a < st_a && i_b < st_b) {
    __m128i v_a, v_b;
    v_a = MM_LOAD_SI_128((__m128i *)&A[i_a]);
    v_b = MM_LOAD_SI_128((__m128i *)&B[i_b]);
    while (true) {
      const __m128i cmp_mask1 =
          _mm_cmpeq_epi32(v_a, v_b); // pairwise comparison
      const __m128i cmp_mask2 = _mm_cmpeq_epi32(
          v_a, _mm_shuffle_epi32(v_b, cyclic_shift1)); // again...
      __m128i cmp_mask = _mm_or_si128(cmp_mask1, cmp_mask2);
      const __m128i cmp_mask3 = _mm_cmpeq_epi32(
          v_a, _mm_shuffle_epi32(v_b, cyclic_shift2)); // and again...
      cmp_mask = _mm_or_si128(cmp_mask, cmp_mask3);
      const __m128i cmp_mask4 = _mm_cmpeq_epi32(
          v_a, _mm_shuffle_epi32(v_b, cyclic_shift3)); // and again.
      cmp_mask = _mm_or_si128(cmp_mask, cmp_mask4);
      // convert the 128-bit mask to the 4-bit mask
      const int mask = _mm_movemask_ps(*reinterpret_cast<__m128 *>(&cmp_mask));
      // copy out common elements
      const __m128i p = _mm_shuffle_epi8(v_a, shuffle_mask[mask]);

      _mm_storeu_si128((__m128i *)out, p);
      out +=
          _mm_popcnt_u32(mask); // a number of elements is a weight of the mask

      const uint32_t a_max = A[i_a + 3];
      if (a_max <= B[i_b + 3]) {
        i_a += 4;
        if (i_a >= st_a)
          break;
        v_a = MM_LOAD_SI_128((__m128i *)&A[i_a]);
      }
      if (a_max >= B[i_b + 3]) {
        i_b += 4;
        if (i_b >= st_b)
          break;
        v_b = MM_LOAD_SI_128((__m128i *)&B[i_b]);
      }
    }
  }

  // intersect the tail using scalar intersection
  while (i_a < s_a && i_b < s_b) {
    if (A[i_a] < B[i_b]) {
      i_a++;
    } else if (B[i_b] < A[i_a]) {
      i_b++;
    } else {
      *out++ = B[i_b];
      i_a++;
      i_b++;
    }
  }

  return out - initout;
}


size_t intersect_scalar_branchless(const uint32_t *list1, size_t size1, const uint32_t *list2, size_t size2, uint32_t *result){
	const uint32_t *end1 = list1+size1, *end2 = list2+size2, *endresult;
	asm(".intel_syntax noprefix;"
		"xor rax, rax;"
		"xor rbx, rbx;"
		"xor rcx, rcx;"
	"1: "
		"cmp %[list1], %[end1];"  // list1 != end1
		"je 2f;"
		"cmp %[list2], %[end2];"  // list2 != end2
		"je 2f;"

		"mov r10d, [%q[list2]];"  // saved in r10d as value is only 4 byte wide
		"cmp [%q[list1]], r10d;"  // compare *list1 and *list2
		"setle al;"               // set al=1 if lower or equal
		"setge bl;"               // set bl=1 if greater or equal
		"sete  cl;"               // set cl=1 if equal, a bit quicker than: and rax, rbx;

		"mov [%q[endresult]], r10d;"  // always save, is overwritten when not equal

		"lea %q[list1], [%q[list1] + rax*4];"         // list1++, if lower or equal
		"lea %q[list2], [%q[list2] + rbx*4];"         // list2++, if greater or equal
		"lea %q[endresult], [%q[endresult] + rcx*4];" // result++, if equal

		"jmp 1b;"       // to loop head
	"2: "
		".att_syntax;"

		: [endresult]"=r"(endresult)
		: [list1]"r"(list1), [list2]"r"(list2), [end1]"r"(end1), [end2]"r"(end2),
			"0"(result)
		: "%rax","%rbx","%rcx", "%r10", "memory", "cc"
	);
	return endresult-result;
}

static uint32_t *shuffle_mask_avx;
void prepare_shuffling_dictionary_avx(){
	shuffle_mask_avx = (uint32_t*)aligned_alloc(32, 256*8*sizeof(uint32_t));
	for(uint32_t i=0; i<256; ++i){
		int count=0, rest=7;
		for(int b=0; b<8; ++b){
			if(i & (1 << b)){
				// n index at pos p - move nth element to pos p
				shuffle_mask_avx[i*8 + count] = b; // move all set bits to beginning
				++count;
			}else{
				shuffle_mask_avx[i*8 + rest] = b; // move rest at the end
				--rest;
			}
		}
	}
}

size_t intersect_vector_avx_asm(const uint32_t *list1, size_t size1, const uint32_t *list2, size_t size2, uint32_t *result){
	size_t count=0, i_a=0, i_b=0;
	size_t st_a = (size1 / 8) * 8;
	size_t st_b = (size2 / 8) * 8;

	asm(".intel_syntax noprefix;"

		"xor rax, rax;"
		"xor rbx, rbx;"
		"xor r9, r9;"
	"1: "
 		"cmp %[i_a], %[st_a];"
 		"je 2f;"
		"cmp %[i_b], %[st_b];"
		"je 2f;"

		"vmovdqa ymm1, [%q[list1] + %q[i_a]*4];" // elements are 4 byte
		"vmovdqa ymm2, [%q[list2] + %q[i_b]*4];"

		"mov r8d, [%q[list1] + %q[i_a]*4 + 28];" //int32_t a_max = list1[i_a+7];
		"cmp r8d, [%q[list2] + %q[i_b]*4 + 28];"
		"setbe al;"
		"setae bl;"
		"lea %q[i_a], [%q[i_a] + rax*8];"
		"lea %q[i_b], [%q[i_b] + rbx*8];"

		"vpcmpeqd ymm10, ymm1, ymm2;"
		"vperm2f128 ymm6, ymm2, ymm2, 1;"
		"vpermilps ymm3, ymm2, 0x39;"
		"vpermilps ymm4, ymm2, 0x4e;"
		"vpcmpeqd ymm11, ymm1, ymm3;"
		"vpermilps ymm5, ymm2, 0x93;"
		"vpcmpeqd ymm4, ymm1, ymm4;"
		"vpermilps ymm7, ymm6, 0x39;"
		"vpermilps ymm8, ymm6, 0x4e;"
		"vpermilps ymm9, ymm6, 0x93;"
		"vpcmpeqd ymm5, ymm1, ymm5;"

		"vpor ymm10, ymm10, ymm11;"
		"vpcmpeqd ymm12, ymm1, ymm6;"
		"vpor ymm4, ymm4, ymm5;"

		"vpcmpeqd ymm13, ymm1, ymm7;"
		"vpcmpeqd ymm8, ymm1, ymm8;"
		"vpcmpeqd ymm9, ymm1, ymm9;"

		"vpor ymm12, ymm12, ymm13;"
		"vpor ymm8, ymm8, ymm9;"

		"vpor ymm10, ymm10, ymm4;"
		"vpor ymm12, ymm12, ymm8;"

		"vpor ymm10, ymm10, ymm12;"

		"vmovmskps r9d, ymm10;"

		//4 * 8 * r9 => 5x shift
		//"shlx r8, r9, ;" //no immediate, need register for constant 5
		"movsxd r8, r9d;"
		"shl r8, 5;"
		"vmovdqa ymm0, [%q[shuffle_mask] + r8];"
		"vpermd ymm0, ymm0, ymm1;"
		"vmovdqu [%q[result] + %q[count]*4], ymm0;"

		"popcnt r9d, r9d;"
		"add %q[count], r9;"

 		"jmp 1b;"
	"2: "
		".att_syntax;"
		: [count]"+r"(count), [i_a]"+r"(i_a), [i_b]"+r"(i_b)
		: [st_a]"r"(st_a), [st_b]"r"(st_b),
			[list1]"r"(list1), [list2]"r"(list2),
			[result]"r"(result), [shuffle_mask]"r"(shuffle_mask_avx)
		: "%rax", "%rbx", "%r8", "%r9",
			"ymm0","ymm1","ymm2","ymm3","ymm4",
			"ymm5","ymm6","ymm7","ymm8","ymm9",
			"ymm10","ymm11","ymm12","ymm13","ymm14","ymm15",
			"memory", "cc"
	);
	// intersect the tail using scalar intersection
	count += intersect_scalar_branchless(
		list1+i_a, size1-i_a, list2+i_b, size2-i_b, result+count
	);
	return count;
}

size_t intersect_scalar(const uint32_t *list1, size_t size1, const uint32_t *list2, size_t size2, uint32_t *result){
	size_t counter=0;
	const uint32_t *end1 = list1+size1, *end2 = list2+size2;
	// hard to get only the loop instructions, now only a tiny check at the top wrong
	while(list1 != end1 && list2 != end2){
		if(*list1 < *list2){
			list1++;
		}else if(*list1 > *list2){
			list2++;
		}else{
			result[counter++] = *list1;
			list1++; list2++;
		}
	}
	return counter;
}

#if 0
size_t intersect_vector_avx512_2intersect(const uint32_t *list1, size_t size1, const uint32_t *list2, size_t size2, uint32_t *result){
	size_t count=0, i_a=0, i_b=0;
	size_t st_a = (size1 / 16) * 16;
	size_t st_b = (size2 / 16) * 16;

	while(i_a < st_a && i_b < st_b){
		__m512i v_a = _mm512_load_epi32(&list1[i_a]);
		__m512i v_b = _mm512_load_epi32(&list2[i_b]);

		int32_t a_max = list1[i_a+15];
		int32_t b_max = list2[i_b+15];
		i_a += (a_max <= b_max) * 16;
		i_b += (a_max >= b_max) * 16;

		__mmask16 k_a, k_b;
		_mm512_2intersect_epi32(v_a, v_b, &k_a, &k_b);

		_mm512_mask_compressstoreu_epi32(&result[count], k_a, v_a);

		count += _mm_popcnt_u32(k_a);
	}
	// intersect the tail using scalar intersection
	count += intersect_scalar(list1+i_a, size1-i_a, list2+i_b, size2-i_b, result+count);

	return count;
}
#endif

size_t intersect_vector_avx512_conflict(const uint32_t *list1, size_t size1, const uint32_t *list2, size_t size2, uint32_t *result){
	size_t count=0, i_a=0, i_b=0;
	size_t st_a = (size1 / 8) * 8;
	size_t st_b = (size2 / 8) * 8;

	__m512i vzero = _mm512_setzero_epi32();
	while(i_a < st_a && i_b < st_b){
		__m256i v_a = _mm256_load_si256((__m256i*)&list1[i_a]);
		__m256i v_b = _mm256_load_si256((__m256i*)&list2[i_b]);
		// __m256i v_a = _mm256_lddqu_si256((__m256i*)&list1[i_a]);
		// __m256i v_b = _mm256_lddqu_si256((__m256i*)&list2[i_b]);

		int32_t a_max = list1[i_a+7];
		int32_t b_max = list2[i_b+7];
		i_a += (a_max <= b_max) * 8;
		i_b += (a_max >= b_max) * 8;

		__m512i vpool = _mm512_inserti32x8(_mm512_castsi256_si512(v_a), v_b, 1);
		__m512i vconflict = _mm512_conflict_epi32(vpool);
		// _mm512_movepi32_mask doesn't work, use comparison with zero
		__mmask16 kconflict = _mm512_cmpneq_epi32_mask(vconflict, vzero);

		_mm512_mask_compressstoreu_epi32(&result[count], kconflict, vpool);

		count += _mm_popcnt_u32(kconflict);
	}
	// intersect the tail using scalar intersection
	count += intersect_scalar(list1+i_a, size1-i_a, list2+i_b, size2-i_b, result+count);

	return count;
}

static __m512i *shuffle_vectors;
void prepare_shuffle_vectors() {
	uint32_t* arr = (uint32_t*)aligned_alloc(32, 16*15*sizeof(uint32_t));
	uint32_t start=1;
	for(uint32_t i=0; i<15; ++i){
		uint32_t counter = start;
		for(uint32_t j=0; j<16; ++j){
			arr[i*16 + j] = counter % 16;
			++counter;
		}
		++start;
	}
	shuffle_vectors = reinterpret_cast<__m512i*>(arr);
}

size_t intersect_vector_avx512_asm(const uint32_t *list1, size_t size1, const uint32_t *list2, size_t size2, uint32_t *result){
	size_t count=0, i_a=0, i_b=0;
	size_t st_a = (size1 / 16) * 16;
	size_t st_b = (size2 / 16) * 16;


	asm(".intel_syntax noprefix;"

		"vmovdqa32 zmm0 , [%[shuffle_vectors]];"
		"vmovdqa32 zmm1 , [%[shuffle_vectors] + 0x040];"
		"vmovdqa32 zmm2 , [%[shuffle_vectors] + 0x080];"
		"vmovdqa32 zmm3 , [%[shuffle_vectors] + 0x0c0];"
		"vmovdqa32 zmm4 , [%[shuffle_vectors] + 0x100];"
		"vmovdqa32 zmm5 , [%[shuffle_vectors] + 0x140];"
		"vmovdqa32 zmm6 , [%[shuffle_vectors] + 0x180];"
		"vmovdqa32 zmm7 , [%[shuffle_vectors] + 0x1c0];"
		"vmovdqa32 zmm8 , [%[shuffle_vectors] + 0x200];"
		"vmovdqa32 zmm9 , [%[shuffle_vectors] + 0x240];"
		"vmovdqa32 zmm10, [%[shuffle_vectors] + 0x280];"
		"vmovdqa32 zmm11, [%[shuffle_vectors] + 0x2c0];"
		"vmovdqa32 zmm12, [%[shuffle_vectors] + 0x300];"
		"vmovdqa32 zmm13, [%[shuffle_vectors] + 0x340];"
		"vmovdqa32 zmm14, [%[shuffle_vectors] + 0x380];"

		"xor rax, rax;"
		"xor rbx, rbx;"
		"xor r9, r9;"
	"1: "
		"cmp %[i_a], %[st_a];"
		"je 2f;"
		"cmp %[i_b], %[st_b];"
		"je 2f;"

		"vmovdqa32 zmm15, [%q[list1] + %q[i_a]*4];" // elements are 4 byte
		"vmovdqa32 zmm16, [%q[list2] + %q[i_b]*4];"

		// increase i_a and i_b
		"mov r8d, [%q[list1] + %q[i_a]*4 + 60];" // int32_t a_max = list1[i_a+15];
		"cmp r8d, [%q[list2] + %q[i_b]*4 + 60];" // 15*4 = 60
		"setbe al;"
		"setae bl;"
		//"lea %q[i_a], [%q[i_a] + rax*8];" //no *16 in address mode
		//"lea %q[i_b], [%q[i_b] + rbx*8];"
		"shl rax, 4;"
		"shl rbx, 4;"
		"add %q[i_a], rax;"
		"add %q[i_b], rbx;"

		"vpcmpeqd k1, zmm15, zmm16;"
		"vpermd zmm17, zmm0, zmm16;"
		"vpcmpeqd k2, zmm15, zmm17;"
		"vpermd zmm18, zmm1, zmm16;"
		"vpcmpeqd k3, zmm15, zmm18;"
		"vpermd zmm19, zmm2, zmm16;"
		"vpcmpeqd k4, zmm15, zmm19;"

		"korw k1, k1, k2;"
		"korw k3, k3, k4;"
		"korw k1, k1, k3;"

		"vpermd zmm20, zmm3, zmm16;"
		"vpcmpeqd k5, zmm15, zmm20;"
		"vpermd zmm21, zmm4, zmm16;"
		"vpcmpeqd k6, zmm15, zmm21;"
		"vpermd zmm22, zmm5, zmm16;"
		"vpcmpeqd k7, zmm15, zmm22;"
		"vpermd zmm23, zmm6, zmm16;"
		"vpcmpeqd k2, zmm15, zmm23;"

		"korw k5, k5, k6;"
		"korw k7, k7, k2;"
		"korw k5, k5, k7;"

		"vpermd zmm24, zmm7, zmm16;"
		"vpcmpeqd k3, zmm15, zmm24;"
		"vpermd zmm25, zmm8, zmm16;"
		"vpcmpeqd k4, zmm15, zmm25;"
		"vpermd zmm26, zmm9, zmm16;"
		"vpcmpeqd k6, zmm15, zmm26;"
		"vpermd zmm27, zmm10, zmm16;"
		"vpcmpeqd k7, zmm15, zmm27;"

		"korw k3, k3, k4;"
		"korw k6, k6, k7;"
		"korw k3, k3, k6;"

		"vpermd zmm28, zmm11, zmm16;"
		"vpcmpeqd k2, zmm15, zmm28;"
		"vpermd zmm29, zmm12, zmm16;"
		"vpcmpeqd k4, zmm15, zmm29;"
		"vpermd zmm30, zmm13, zmm16;"
		"vpcmpeqd k6, zmm15, zmm30;"
		"vpermd zmm31, zmm14, zmm16;"
		"vpcmpeqd k7, zmm15, zmm31;"

		"korw k2, k2, k4;"
		"korw k6, k6, k7;"
		"korw k2, k2, k6;"

		"korw k1, k1, k5;"
		"korw k3, k3, k2;"
		"korw k1, k1, k3;"

		"vpcompressd [%q[result] + %q[count]*4] %{k1%}, zmm15;"
		"kmovw r9d, k1;"

		"popcnt r9d, r9d;"
		"add %q[count], r9;"

		"jmp 1b;"
	"2: "
		".att_syntax;"
		: [count]"+r"(count), [i_a]"+r"(i_a), [i_b]"+r"(i_b)
		: [st_a]"r"(st_a), [st_b]"r"(st_b),
			[list1]"r"(list1), [list2]"r"(list2),
			[result]"r"(result), [shuffle_vectors]"r"(shuffle_vectors)
		: "%rax", "%rbx", "%r8", "%r9",
			"zmm0","zmm1","zmm2","zmm3","zmm4","zmm5","zmm6","zmm7",
			"zmm8","zmm9","zmm10","zmm11","zmm12","zmm13","zmm14","zmm15",
			"zmm16","zmm17","zmm18","zmm19","zmm20","zmm21","zmm22","zmm23",
			"zmm24","zmm25","zmm26","zmm27","zmm28","zmm29","zmm30","zmm31",
			"memory", "cc"
	);
	// intersect the tail using scalar intersection
	count += intersect_scalar_branchless(
		list1+i_a, size1-i_a, list2+i_b, size2-i_b, result+count
	);
	return count;
}

inline std::map<std::string, intersectionfunction>
initializeintersectionfactory() {
  std::map<std::string, intersectionfunction> schemes;
  schemes["simd"] = SIMDintersection;
  schemes["galloping"] = onesidedgallopingintersection;
  schemes["mut_part"] = mutualPartitioningIntersect;
  schemes["scalar"] = scalar;
  schemes["v1"] = v1;
  schemes["v3"] = v3;
  schemes["simdgalloping"] = SIMDgalloping;
#ifdef __AVX2__
  schemes["simd_avx2"] = SIMDintersection_avx2;
  schemes["v1_avx2"] = v1_avx2;
  schemes["v3_avx2"] = v3_avx2;
  schemes["simdgalloping_avx2"] = SIMDgalloping_avx2;
#endif
  schemes["highlyscalable_intersect_SIMD"] = highlyscalable_intersect_SIMD;
  schemes["lemire_highlyscalable_intersect_SIMD"] =
      lemire_highlyscalable_intersect_SIMD;
	prepare_shuffling_dictionary_avx();
	prepare_shuffle_vectors();
  schemes["avx_asm"] = intersect_vector_avx_asm;
  schemes["scalar_branchless_asm"] = intersect_scalar_branchless;
  schemes["avx512_conflict"] = intersect_vector_avx512_conflict;
  schemes["avx512_asm"] = intersect_vector_avx512_asm;

  return schemes;
}

std::map<std::string, intersectionfunction>
    IntersectionFactory::intersection_schemes = initializeintersectionfactory();

} // namespace SIMDCompressionLib
