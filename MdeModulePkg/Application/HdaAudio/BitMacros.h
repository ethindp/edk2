/*
 BitMacros.h
 project: bit array C library
 url: https://github.com/noporpoise/BitArray/
 author: Isaac Turner <turner.isaac@gmail.com>
 license: Public Domain, no warranty
 date: Dec 2013
 Modified to work in EFI environment in Jul 2021
*/

#ifndef BITSET_H_
#define BITSET_H_

#include <Uefi.h>

// trailing_zeros is number of least significant zeros
// leading_zeros is number of most significant zeros
#if defined(_WIN32) || defined(_WIN64)
  #define trailing_zeros(x) ({ __typeof(x) _r; _BitScanReverse64(&_r, x); _r; })
  #define leading_zeros(x) ({ __typeof(x) _r; _BitScanForward64(&_r, x); _r; })
#else
  #define trailing_zeros(x) ((x) ? (__typeof(x))__builtin_ctzll(x) : (__typeof(x))sizeof(x)*8)
  #define leading_zeros(x) ((x) ? (__typeof(x))__builtin_clzll(x) : (__typeof(x))sizeof(x)*8)
#endif

// Get index of top set bit. If x is 0 return nbits
#define top_set_bit(x) ((x) ? sizeof(x)*8-leading_zeros(x)-1 : sizeof(x)*8)

#define roundup_bits2bytes(bits)   (((bits)+7)/8)
#define roundup_bits2words32(bits) (((bits)+31)/32)
#define roundup_bits2words64(bits) (((bits)+63)/64)

// Round a number up to the nearest number that is a power of two
#define roundup2pow(x) (1UL << (64 - leading_zeros(x)))

#define rot32(x,r) (((x)<<(r)) | ((x)>>(32-(r))))
#define rot64(x,r) (((x)<<(r)) | ((x)>>(64-(r))))

// need to check for length == 0, undefined behaviour if uint64_t >> 64 etc
#define bitmask(nbits,type) ((nbits) ? ~(type)0 >> (sizeof(type)*8-(nbits)): (type)0)
#define bitmask32(nbits) bitmask(nbits,uint32_t)
#define bitmask64(nbits) bitmask(nbits,uint64_t)

// A possibly faster way to combine two words with a mask
//#define bitmask_merge(a,b,abits) ((a & abits) | (b & ~abits))
#define bitmask_merge(a,b,abits) (b ^ ((a ^ b) & abits))

// Swap lowest four bits. A nibble is 4 bits (i.e. half a byte)
#define rev_nibble(x) ((((x)&1)<<3)|(((x)&2)<<1)|(((x)&4)>>1)|(((x)&8)>>3))

//
// Bit array (bitset)
//
// bitsetX_wrd(): get word for a given position
// bitsetX_idx(): get index within word for a given position
#define _VOLPTR(x) ((volatile __typeof(x) *)(&(x)))
#define _VOLVALUE(x) (*_VOLPTR(x))

#define _TYPESHIFT(arr,word,shift) \
        ((__typeof(*(arr)))((__typeof(*(arr)))(word) << (shift)))

#define bitsetX_wrd(wrdbits,pos) ((pos) / (wrdbits))
#define bitsetX_idx(wrdbits,pos) ((pos) % (wrdbits))

#define bitset32_wrd(pos) ((pos) >> 5)
#define bitset32_idx(pos) ((pos) & 31)

#define bitset64_wrd(pos) ((pos) >> 6)
#define bitset64_idx(pos) ((pos) & 63)

//
// Bit functions on arrays
//
#define bitset2_get(arr,wrd,idx)     (((arr)[wrd] >> (idx)) & 0x1)
#define bitset2_set(arr,wrd,idx)     ((arr)[wrd] |=  _TYPESHIFT(arr,1,idx))
#define bitset2_del(arr,wrd,idx)     ((arr)[wrd] &=~ _TYPESHIFT(arr,1,idx))
#define bitset2_tgl(arr,wrd,idx)     ((arr)[wrd] ^=  _TYPESHIFT(arr,1,idx))
#define bitset2_or(arr,wrd,idx,bit)  ((arr)[wrd] |=  _TYPESHIFT(arr,bit,idx))
#define bitset2_xor(arr,wrd,idx,bit) ((arr)[wrd]  = ~((arr)[wrd] ^ (~_TYPESHIFT(arr,bit,idx))))
#define bitset2_and(arr,wrd,idx,bit) ((arr)[wrd] &= (_TYPESHIFT(arr,bit,idx) | ~_TYPESHIFT(arr,1,idx)))
#define bitset2_cpy(arr,wrd,idx,bit) ((arr)[wrd]  = ((arr)[wrd] &~ _TYPESHIFT(arr,1,idx)) | _TYPESHIFT(arr,bit,idx))

//
// Auto detect size of type from pointer
//
#define bitset_wrd(arr,pos) bitsetX_wrd(sizeof(*(arr))*8,pos)
#define bitset_idx(arr,pos) bitsetX_idx(sizeof(*(arr))*8,pos)
#define bitset_op(func,arr,pos)      func(arr, bitset_wrd(arr,pos), bitset_idx(arr,pos))
#define bitset_op2(func,arr,pos,bit) func(arr, bitset_wrd(arr,pos), bitset_idx(arr,pos), bit)

// Auto-detect type size: bit functions
#define bitset_get(arr,pos)     bitset_op(bitset2_get, arr, pos)
#define bitset_set(arr,pos)     bitset_op(bitset2_set, arr, pos)
#define bitset_del(arr,pos)     bitset_op(bitset2_del, arr, pos)
#define bitset_tgl(arr,pos)     bitset_op(bitset2_tgl, arr, pos)
#define bitset_or(arr,pos,bit)  bitset_op2(bitset2_or, arr, pos, bit)
#define bitset_xor(arr,pos,bit) bitset_op2(bitset2_xor, arr, pos, bit)
#define bitset_and(arr,pos,bit) bitset_op2(bitset2_and, arr, pos, bit)
#define bitset_cpy(arr,pos,bit) bitset_op2(bitset2_cpy, arr, pos, bit)

// Clearing a word does not return a meaningful value
#define bitset_clear_word(arr,pos) ((arr)[bitset_wrd(arr,pos)] = 0)

/*
 * Byteswapping
 */

/* clang uses these to check for features */
#ifndef __has_feature
#define __has_feature(x) 0
#endif

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

/* GCC versions < 4.3 do not have __builtin_bswapX() */
#if ( defined(__clang__) && !__has_builtin(__builtin_bswap64) ) ||             \
    ( !defined(__clang__) && defined(__GNUC__) && defined(__GNUC_MINOR__) &&   \
      ( (__GNUC__ < 4)  || (__GNUC__ == 4 && __GNUC_MINOR__ < 3)) )
  #define byteswap64(x) ( (((UINT64)(x) << 56))                       | \
                          (((UINT64)(x) << 40) & 0xff000000000000ULL) | \
                          (((UINT64)(x) << 24) & 0xff0000000000ULL)   | \
                          (((UINT64)(x) <<  8) & 0xff00000000ULL)     | \
                          (((UINT64)(x) >>  8) & 0xff000000ULL)       | \
                          (((UINT64)(x) >> 24) & 0xff0000ULL)         | \
                          (((UINT64)(x) >> 40) & 0xff00ULL)           | \
                          (((UINT64)(x) >> 56)) )

  #define byteswap32(x) ( (((UINT32)(x) << 24))                       | \
                          (((UINT32)(x) <<  8) & 0xff0000U)           | \
                          (((UINT32)(x) >>  8) & 0xff00U)             | \
                          (((UINT32)(x) >> 24)) )

  /* uint16_t type might be bigger than 2 bytes, so need to mask */
  #define byteswap16(x) ( (((UINT16)(x) & 0xff) << 8) | \
                          (((UINT16)(x) >> 8) & 0xff) )
#else
  #define byteswap64(x) __builtin_bswap64(x)
  #define byteswap32(x) __builtin_bswap64(x)
  #define byteswap16(x) __builtin_bswap64(x)
#endif

#endif /* BITSET_H_ */
