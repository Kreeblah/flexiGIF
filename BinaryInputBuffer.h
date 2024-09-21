// //////////////////////////////////////////////////////////
// BinaryInputBuffer.h
// Copyright (c) 2011-2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/disclaimer.html
//

#pragma once

#pragma warning (disable: 4530) // missing exception handling
#include <string>
#include <fstream>

/// read a file bit-wise
class BinaryInputBuffer
{
public:
  /// read from file
  explicit BinaryInputBuffer(const std::string& filename);

  /// get number of bytes read so far
  unsigned int   getNumBytesRead() const;
  /// get number of bits  still available
  unsigned int   getNumBitsLeft()  const;
  /// true, if no bits left
  bool           empty()           const;

  /// take a look at the next bits without advancing the file pointer
  unsigned int   peekBits  (unsigned char numBits); // at most 16 bits
  /// increment buffer pointers/offsets
  void           removeBits(unsigned char numBits); // at most 16 bits
  /// get the next bits and increment buffer pointers/offsets
  unsigned int   getBits   (unsigned char numBits); // at most 16 bits
  /// get 8 bits
  unsigned char  getByte();
  /// get a single bit
  bool           getBool();

private:
  /// simple I/O buffering, faster than C lib's built-in functions
  unsigned char  getBufferedByte();

  /// bit/byte file stream
  std::ifstream m_stream;
  /// total number of bytes read so far
  unsigned int  m_read;
  /// total bits left (initially, it's 8*m_size)
  unsigned int  m_bitsLeft;
  /// store bits until next byte boundary
  unsigned int  m_bitBuffer;
  /// number of valid bits in _bitBuffer
  unsigned char m_bitBufferSize;

  /// default buffer size
  enum { CacheSize = 1024 };

  /// buffer
  unsigned char m_cache[CacheSize];
  /// position of next byte
  unsigned int  m_cacheOffset;
  /// position beyond last valid byte
  unsigned int  m_cacheSize;
};
