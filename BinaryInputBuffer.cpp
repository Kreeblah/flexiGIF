// //////////////////////////////////////////////////////////
// BinaryInputBuffer.cpp
// Copyright (c) 2011-2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/disclaimer.html
//

#include "BinaryInputBuffer.h"

#include <cassert>

/// read from file
BinaryInputBuffer::BinaryInputBuffer(const std::string& filename)
: m_stream(),
  m_read(0),
  m_bitsLeft(0),
  m_bitBuffer(0),
  m_bitBufferSize(0),
  m_cache(),
  m_cacheOffset(0),
  m_cacheSize(0)
{
  // open file
  m_stream.open(filename.c_str(), std::ios::in | std::ios::binary);
  if (!m_stream)
    return;

  // get file size
  m_stream.seekg(0, std::ios_base::end);
  unsigned int size = (unsigned int)m_stream.tellg();
  m_stream.seekg(0, std::ios_base::beg);

  // load first segment (or even the complete file)
  m_cacheSize = CacheSize;
  if (m_cacheSize > size)
    m_cacheSize = size;
  m_stream.read((char*)m_cache, m_cacheSize);

  // compute total number of bits
  m_bitsLeft = 8 * size;
}


/// get number of bytes read so far
unsigned int BinaryInputBuffer::getNumBytesRead() const
{
  return m_read;
}


/// get number of bits  still available
unsigned int BinaryInputBuffer::getNumBitsLeft() const
{
  return m_bitsLeft;
}


/// true, if no bits left
bool BinaryInputBuffer::empty() const
{
  return m_bitsLeft == 0;
}


/// take a look at the next bits without modifying the buffer
unsigned int BinaryInputBuffer::peekBits(unsigned char numBits)
{
  assert(numBits <= 16);
  assert(numBits <= m_bitsLeft);

  // move 8 bits from stream to buffer
  while (m_bitBufferSize < numBits)
  {
    // get one byte from stream and add to internal buffer
    unsigned int byte = getBufferedByte();
    m_bitBuffer       |= byte << m_bitBufferSize;
    m_bitBufferSize   += 8;
  }

  // return desired bits
  unsigned int bitMask = (1 << numBits) - 1;
  return m_bitBuffer & bitMask;
}


/// get the next bits and increment buffer pointers/offsets
unsigned int BinaryInputBuffer::getBits(unsigned char numBits)
{
  unsigned int result = peekBits(numBits);
  removeBits(numBits);
  return result;
}


/// get 8 bits
unsigned char BinaryInputBuffer::getByte()
{
  return (unsigned char)getBits(8);
}


/// get a single bit
bool BinaryInputBuffer::getBool()
{
  return getBits(1) == 1;
}


/// increment buffer pointers/offsets
void BinaryInputBuffer::removeBits(unsigned char numBits)
{
  // if more bits needs to be removed than are actually available in the buffer
  if (m_bitBufferSize < numBits)
    peekBits(numBits);

  // adjust buffers and counters
  m_bitBuffer    >>= numBits;
  m_bitBufferSize -= numBits;
  m_bitsLeft      -= numBits;
}


/// simple I/O buffering, faster than C lib's built-in functions
unsigned char BinaryInputBuffer::getBufferedByte()
{
  // (re-)fill buffer
  if (m_cacheOffset >= m_cacheSize)
  {
    // read as much as possible (it has to fit into the buffer, though)
    size_t numRead = (m_bitsLeft + 7) / 8;
    if (numRead > m_cacheSize)
      numRead = m_cacheSize;

    m_stream.read((char*)m_cache, numRead);
    m_cacheOffset = 0;
  }

  // count number of read bytes
  m_read++;

  // single byte
  return m_cache[m_cacheOffset++];
}
