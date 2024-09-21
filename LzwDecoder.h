// //////////////////////////////////////////////////////////
// LzwDecoder.h
// Copyright (c) 2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/flexigif-lossless-gif-lzw-optimization/

#pragma once

#include "BinaryInputBuffer.h"

#include <vector>
#include <string>

/// decode a GIF file, support for writing the same file with optimized LZW bitstream
/** errors throw an exception (const char*) **/
class LzwDecoder
{
public:
  // -------------------- data types --------------------
  /// a continuous block of bytes
  typedef std::vector<unsigned char> Bytes;

  /// parse LZW bitstream
  explicit LzwDecoder(BinaryInputBuffer& input, bool isGif = true, unsigned char minCodeSize = 8, unsigned char maxCodeSize = 12, unsigned int expectedNumberOfBytes = 16*1024);

  /// return minimum LZW code size
  unsigned char getCodeSize() const;
  /// return decompressed data
  const Bytes&  getBytes()    const;

  /// for statistics only: true number of compressed bits
  unsigned int  getNumCompressedBits() const;

  /// show debug output
  static bool verbose;

private:
  /// decompress data, first parameter is a hint to avoid memory reallocations, GIFs are limited to code size 12, .Z => 16
  void  decompress(unsigned int expectedNumberOfBytes, unsigned char minCodeSize, unsigned char maxCodeSize);

  /// read bits from current LZW block, start with next block if needed
  unsigned int getLzwBits(unsigned char numBits);

  /// tree node for an LZW code: its parent code and its last byte
  struct BackReference
  {
    int  length;   // match length
    int  previous; // code of parent (contains everything except for the last byte), if negative then there is no parent
    unsigned char last; // last byte of the match

    int   pos;      // position of a match in the output stream (often there are several candidates)
  };
  /// convert code to bytes and store in buffer
  void decode(Bytes& buffer, unsigned int code, const std::vector<BackReference>& lut) const;

  /// read file bit-by-bit
  BinaryInputBuffer& m_input;
  /// decoded file (pixels/bytes)
  Bytes         m_bytes;

  /// if true, then GIF's LZW data is grouped in blocks of 255 bytes each
  bool          m_isGif;
  /// minimum bits per LZW code
  unsigned char m_codeSize;
  /// LZW data is split in blocks, keep track of the remaining bits in the current block
  unsigned int  m_bitsLeftInCurrentBlock;
  /// total number of bits of original LZW compressed image (without block lengths)
  unsigned int  m_numBitsOriginalLZW;
};
