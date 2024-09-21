// //////////////////////////////////////////////////////////
// LzwDecoder.cpp
// Copyright (c) 2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/smallzwflexigif-lossless-gif-lzw-optimization/

#include "LzwDecoder.h"

#include <fstream>

#define ALLOW_VERBOSE
#ifdef  ALLOW_VERBOSE
#include <iostream>
#include <iomanip>
#endif

namespace
{
  /// placeholder for "no parent code"
  const int          NoPrevious    = -1;
  /// GIFs have a valid end-of-stream token but not compress' LZW variant
  const unsigned int NoEndOfStream = 0xFFFFFFFF;
}

// statics
bool LzwDecoder::verbose = false;


/// parse LZW bitstream
LzwDecoder::LzwDecoder(BinaryInputBuffer& input, bool isGif,
                       unsigned char minCodeSize, unsigned char maxCodeSize,
                       unsigned int expectedNumberOfBytes)
: m_input(input),
  m_bytes(),
  m_isGif(isGif),
  m_codeSize(0),
  m_bitsLeftInCurrentBlock(0),
  m_numBitsOriginalLZW(0)
{
  decompress(expectedNumberOfBytes, minCodeSize, maxCodeSize);
}


/// return minimum LZW code size
unsigned char LzwDecoder::getCodeSize() const
{
  return m_codeSize;
}


/// return decompressed data
const LzwDecoder::Bytes& LzwDecoder::getBytes() const
{
  return m_bytes;
}


/// for statistics only: true number of compressed bits
unsigned int LzwDecoder::getNumCompressedBits() const
{
  return m_numBitsOriginalLZW;
}


/// convert code to bytes and store in buffer
void LzwDecoder::decode(Bytes& buffer, unsigned int code, const std::vector<BackReference>& lut) const
{
  // single byte: exists in lookup table only
  if (lut[code].length == 1)
  {
    buffer.push_back(lut[code].last);
    return;
  }

  // old iterative decoder
  int length = lut[code].length;
  // resize buffer
  buffer.resize(buffer.size() + length);
  unsigned char* pos = &buffer[buffer.size() - 1];
  // copy bytes while walking backwards
  while (length-- > 0)
  {
    *pos-- = lut[code].last;
    code   = lut[code].previous;
  }
}


/// decompress data, first parameter is a hint to avoid memory reallocations, GIFs are limited to code size 12, .Z => 16
void LzwDecoder::decompress(unsigned int expectedNumberOfBytes, unsigned char minCodeSize, unsigned char maxCodeSize)
{
  // initial bits per token
  m_codeSize = minCodeSize;

#ifdef ALLOW_VERBOSE
  if (verbose && m_isGif)
    std::cout << ", " << (int)minCodeSize << " bits" << std::endl;
  const char* pixel = m_isGif ? "pixel" : "byte";
#endif

  // special codes
  const unsigned int clear       = 1 << minCodeSize;
  const unsigned int endOfStream = m_isGif ? clear + 1 : NoEndOfStream;
  const unsigned int maxColor    = clear - 1;
  const unsigned int MaxToken    = 1 << maxCodeSize;

  /// temporary look-up table for decompression
  std::vector<BackReference> lut(m_isGif ? clear + 2 : clear + 1);
  lut.reserve(MaxToken);
  // set initial contents
  for (unsigned int i = 0; i <= maxColor; i++)
  {
    lut[i].previous = NoPrevious;
    lut[i].last     = (unsigned char)i;
    lut[i].length   = 1;
    lut[i].pos      = NoPrevious;
  }

  unsigned char codeSize = minCodeSize + 1;

  m_bitsLeftInCurrentBlock = 0;

  // delete old data
  m_bytes.clear();
  m_bytes.reserve(expectedNumberOfBytes);

  // pass through first token
  int prevToken = NoPrevious;
  int token     = getLzwBits(codeSize);
  while (token == clear)
    token = getLzwBits(codeSize);

  if (token >= (int)lut.size())
  {
#ifdef ALLOW_VERBOSE
    std::cerr << "found initial token " << token << " but only " << lut.size() << " dictionary entries" << std::endl;
#endif
    throw "invalid token";
  }

  if (token != endOfStream)
    m_bytes.push_back((unsigned char)token);

  // counters
  unsigned int numTokensTotal = 1;
  unsigned int numTokensBlock = 1;
  unsigned int numBitsBlock   = codeSize;
  unsigned int numBitsTotal   = codeSize;
  unsigned int uptoLastBlock  = 0;

  while (token != endOfStream)
  {
    // one more bit per code ?
    unsigned int powerOfTwo = 1 << codeSize;
    if (lut.size() == powerOfTwo && codeSize < maxCodeSize)
      codeSize++;

    // quick hack: compress' LZW algorithm doesn't have an end-of-file code
    if (!m_isGif && codeSize > m_input.getNumBitsLeft())
      break; // abort if not enough bits left

    // next token
    prevToken = token;
    token = getLzwBits(codeSize);
    if (token > (int)lut.size())
    {
#ifdef ALLOW_VERBOSE
      std::cerr << "found token " << token << " (" << (int)codeSize << " bits, "
                << pixel << " " << m_bytes.size() << ") but only " << lut.size() << " dictionary entries" << std::endl;
#endif
      throw "invalid token";
    }

    // counters
    numTokensTotal++;
    numTokensBlock++;
    numBitsBlock += codeSize;
    numBitsTotal += codeSize;

    // reset dictionary ?
    bool reset = false;
    while (token == clear)
    {
#ifdef ALLOW_VERBOSE
      if (verbose)
      {
        std::cout << "restart @ "  << std::setw(7) << m_bytes.size()
                  << "    \tbits=" << std::setw(8) << numBitsTotal << " +" << numBitsBlock
                  << std::setprecision(3) << std::fixed
                  << "   \tbits/"  << pixel << "=" << numBitsBlock / float(m_bytes.size() - uptoLastBlock)
                  << "   \ttokens=+" << numTokensBlock << "/" << numTokensTotal
                  << "   \tdict="  << std::setw(4) << lut.size() << std::endl;
      }
#endif

      // delete all codes with 2+ bytes
      if (m_isGif)
      {
        lut.resize(clear + 2);
      }
      else
      {
        // one entry less => no end-of-file token
        lut.resize(clear + 1);

        // bits leftover in the current byte must be ignored
        if (m_numBitsOriginalLZW % 8 != 0)
        {
          unsigned int skipBits = 8 - (m_numBitsOriginalLZW % 8);
          getLzwBits(skipBits);
        }

        // continue with next byte, remove "garbage" bytes, too
        // a block's number of token has to be a multiple of 8
        unsigned int mod8 = numTokensBlock & 7;
        unsigned int gap  = mod8 == 0 ? 0 : 8 - mod8;
        // the following does the same computation in one line (I like that weird bit magic ...)
        //unsigned int gap = (-(int)numTokensBlock) & 7; // from https://www.ioccc.org/2015/mills2/hint.html 

        while (gap-- > 0)
          m_input.getBits(codeSize); // remember, a token takes 16 bits when approaching the LZW restart

        // by the way: the GZIP sources have this formula (it doesn't compute the offset directly, though, and accounts for cases where <=16 bits/token):
        //posbits = ((posbits-1) + ((n_bits<<3)-(posbits-1+(n_bits<<3))%(n_bits<<3)));
      }

      // reset code size
      codeSize = minCodeSize + 1;

      // fetch first token
      prevToken = NoPrevious;
      token = getLzwBits(codeSize);

      // counters
      numTokensTotal++;
      numTokensBlock = 1;
      numBitsBlock   = codeSize;
      numBitsTotal  += codeSize;
      uptoLastBlock  = (unsigned int)m_bytes.size();

      // copy first token to output
      if (token > (int)maxColor)
        throw "block starts with an undefined value/color";
      m_bytes.push_back((unsigned char)token);

      reset = true;
    }

    if (reset)
      continue;

    // done ?
    if (token == endOfStream)
      break;

    // new LZW code
    BackReference add;
    add.previous = prevToken;
    add.length   = lut[prevToken].length + 1;
    add.pos      = (unsigned int)m_bytes.size();

    // look up token in dictionary
    if (token >= (int)lut.size())
    {
      // broken stream ?
      if (token != lut.size())
        throw "unknown token found";
      if (lut.size() >= MaxToken)
        throw "dictionary too large";

      // unknown token:
      // output LAST + LAST[0]
      // add    LAST + LAST[0]
      decode(m_bytes, prevToken, lut);
      add.last = m_bytes[add.pos];
      m_bytes.push_back(add.last);
    }
    else
    {
      // known token:
      // output TOKEN
      // add    LAST + TOKEN[0]
      // add new chunk to table (but no more than 2^12)
      decode(m_bytes, token, lut);
      add.last = m_bytes[add.pos];
    }

    // add LZW code to the dictionary:
    // the if-condition is required in case the dictionary is full and there hasn't been a clear-token
    if (lut.size() < MaxToken)
      lut.push_back(add);
  }

#ifdef ALLOW_VERBOSE
  if (verbose)
    std::cout << "finish  @ "  << std::setw(7) << m_bytes.size()
              << "    \tbits=" << std::setw(8) << numBitsTotal << " +" << numBitsBlock
              << std::setprecision(3) << std::fixed
              << "   \tbits/"  << pixel << "=" << std::setw(4) << numBitsBlock / float(m_bytes.size() - uptoLastBlock)
              << "   \ttokens=+" << numTokensBlock << "/" << numTokensTotal
              << "   \tdict="  << std::setw(4) << lut.size() << std::endl;
#endif

  // skip remaining bits
  unsigned int unusedBits = m_bitsLeftInCurrentBlock;
  // process files where a few bytes are wasted
  if (unusedBits >= 8)
  {
#ifdef ALLOW_VERBOSE
    std::cerr << "too many bits left in current block (" << unusedBits << ")" << std::endl;
#endif
    while (unusedBits > 8)
    {
      getLzwBits(8);
      unusedBits -= 8;
    }
  }
  // ... and now actually skip the unused bits
  token = getLzwBits(unusedBits); // TODO: isn't it always 0 for non-GIFs ?

  // GIF only: a zero-sized block must follow
  if (m_isGif)
  {
    if (m_input.getByte() != 0)
      throw "LZW data is not properly terminated";
  }

  // don't include the "garbage" bits in the final count
  m_numBitsOriginalLZW -= unusedBits;

  // okay, we're done !
}


/// read bits from current LZW block, start with next block if needed
unsigned int LzwDecoder::getLzwBits(unsigned char numBits)
{
  // degenerated case
  if (numBits == 0)
    return 0;

  m_numBitsOriginalLZW += numBits;

  // compress has a simple LZW format, just read the bits ...
  if (!m_isGif)
    return m_input.getBits(numBits);

  // ... but GIF encapsulates the data in blocks of at most 255, each block is preceded by a length byte

  // enough bits available in the current block ?
  if (numBits <= m_bitsLeftInCurrentBlock)
  {
    m_bitsLeftInCurrentBlock -= numBits;
    return m_input.getBits(numBits);
  }

  // cross block border
  unsigned int  low   = 0;
  unsigned char shift = 0;
  // fetch all bits from current block ("low" bits of the token)
  if (m_bitsLeftInCurrentBlock > 0)
  {
    low      = m_input.getBits(m_bitsLeftInCurrentBlock);
    shift    = m_bitsLeftInCurrentBlock;
    numBits -= m_bitsLeftInCurrentBlock;
  }

  // read size of new block
  m_bitsLeftInCurrentBlock = 8 * m_input.getByte();
  if (m_bitsLeftInCurrentBlock < numBits)
    throw "too few bits available in unlzw";

  // and get remaining bits ("high" bits)
  unsigned int high = m_input.getBits(numBits) << shift;
  m_bitsLeftInCurrentBlock -= numBits;

  return low | high;
}
