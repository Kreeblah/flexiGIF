// //////////////////////////////////////////////////////////
// LzwEncoder.cpp
// Copyright (c) 2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/flexigif-lossless-gif-lzw-optimization/

// Nayuki implemented a similar encoder in Java two years earlier:
// https://www.nayuki.io/page/gif-optimizer-java
// (albeit much slower and without non-greedy parsing)

#include "LzwEncoder.h"

#define ALLOW_VERBOSE
#ifdef  ALLOW_VERBOSE
#include <iostream>
#include <iomanip>
#endif

// local stuff
namespace
{
  /// indicates an invalid entry in the dictionary
  const int Unknown = -1;
}

/// set uncompressed data
LzwEncoder::LzwEncoder(const RawData& data, bool isGif)
: m_data(data),
  m_dictionary(),
  m_dictSize(0),
  m_maxDictionary(), // see a few lines below
  m_maxCodeLength(isGif ? 12 : 16),
  m_isGif(isGif)
{
  m_maxDictionary = (1 << m_maxCodeLength) - 1;
}


/// add string at m_data[from...from+length] to the dictionary and return its code
int LzwEncoder::addCode(unsigned int from, unsigned int length)
{
  // first literal
  int code = m_data[from++];

  // walk through dictionary in order to find the code for the matching without the last byte
  for (unsigned int i = 1; i < length; i++)
  {
    unsigned char oneByte = m_data[from++];
    code = m_dictionary[code][oneByte];
  }

  // the new string is a known code plus a new byte (the last one)
  if (from < m_data.size())
  {
    unsigned char lastByte = m_data[from];
    // insert at the end of the dictionary
    if (m_dictSize < m_maxDictionary)
    {
      // don't overwrite (needed for non-greedy algorithm where a code isn't unique)
      if (m_dictionary[code][lastByte] == Unknown)
        m_dictionary[code][lastByte] = m_dictSize;

      m_dictSize++;
    }
  }

  return code;
}


/// return length of longest match, beginning at m_data[from], limited to maxLength
unsigned int LzwEncoder::findMatch(unsigned int from, unsigned int maxLength)
{
  // there is always a LZW code for the first byte
  int code = m_data[from++];

  // try to match second, third, ... byte, too
  for (unsigned int length = 1; length < maxLength; length++)
  {
    unsigned char oneByte = m_data[from++];
    code = m_dictionary[code][oneByte];
    // no continuation => return number of matching byte
    if (code == Unknown)
      return length;
  }

  // matched all the way (quite unlikely, yet possible)
  return maxLength;
}


// for debugging only, not used in release compilation
LzwEncoder::RawData LzwEncoder::debugDecode(int code) const
{
  RawData result;
  if (code == Unknown)
    return result;
  if (code >= (int)m_dictionary.size())
    return result; // actually an error ...

  // brute-force search ...
  int search = code;
  while (code > 255)
  {
    if (search > code)
      search = code;

    search--;
    if (search < 0)
      break;

    for (unsigned int next = 0; next <= 255; next++)
      if (m_dictionary[search][next] == code)
      {
        result.insert(result.begin(), char(next));
        code = search;
        break;
      }
  }

  result.insert(result.begin(), char(code));
  return result;
}

// for debugging only, not used in release compilation
int LzwEncoder::findCode(unsigned int from, unsigned int maxLength) const
{
  // there is always a LZW code for the first byte
  int code = m_data[from++];

  // try to match second, third, ... byte, too
  for (unsigned int length = 1; length < maxLength; length++)
  {
    unsigned char oneByte = m_data[from++];
    int prevCode = code;
    code = m_dictionary[code][oneByte];
    // no continuation => return number of matching byte
    if (code == Unknown)
      return prevCode;
  }

  // matched all the way (quite unlikely, yet possible)
  return code;
}


/// optimize a single block and update results in m_best
LzwEncoder::BitStream LzwEncoder::optimizePartial(unsigned int from, unsigned int maxLength, bool emitBitStream, bool isFinal, OptimizationSettings optimize)
{
  // allocate memory
  if (m_best.empty())
    m_best.resize(m_data.size() / optimize.alignment + 1 + 1);

  // length of current block
  unsigned int length = (unsigned int)m_data.size() - from;
  if (length > maxLength && maxLength != 0)
    length = maxLength;

  // LZW encoded data
  BitStream result;
  if (emitBitStream)
    result.reserve(length * m_maxCodeLength); // reserve a bit too much ... just to avoid any reallocations

  // index for m_best
  if (optimize.alignment == 0)
    optimize.alignment = 1;
  unsigned int fromAligned = from / optimize.alignment;
#ifdef ALLOW_VERBOSE
  if (from % optimize.alignment != 0)
  {
    std::cerr << "optimizePartial is not allowed to start at a non-aligned offset (" << from << ", alignment=" << optimize.alignment << ")" << std::endl;
    throw "non-aligned block start is forbidden";
  }
#endif
  // no recomputation in second pass of --prettygood mode if previous non-greedy search was unsuccessful
  if (optimize.greedy                    &&
      optimize.avoidNonGreedyAgain       &&
     !emitBitStream                      &&
      m_best[fromAligned].nongreedy == 0 &&
      m_best[fromAligned].length    >  0)
    return result;

  // special codes
  const unsigned int clear       = 1 << optimize.minCodeSize;
  const unsigned int endOfStream = clear + 1;

  // initialize dictionary
  std::array<int, 256> children;
  children.fill(Unknown);
  m_dictionary.resize(m_maxDictionary);
  for (unsigned int i = 0; i < m_maxDictionary; i++)
    m_dictionary[i] = children;

  if (m_isGif)
    m_dictSize = clear + 2;
  else
    m_dictSize = clear + 1; // .Z format: no endOfStream

  // initialize counters
  unsigned int  numBits   = 0;
  unsigned int  numTokens = 0;
  unsigned int  numNonGreedyMatches = 0;

  // total length of the current match, initially no match
  unsigned int  matchLength = 0;
  // bits per code
  unsigned char codeSize = getMinBits(m_dictSize);

  // process input
  unsigned int lastPos = from + length - 1;
  for (unsigned int i = from; i <= lastPos; i++)
  {
    // number of already processed bytes
    unsigned int numBytes = i - from + 1;

    // ----- match finding -----

    // need new match ?
    if (matchLength == 0)
    {
      // if blocks become too large then it's quite unlikely to find a better compression
      if (optimize.maxDictionary > 0 && m_dictSize >= optimize.maxDictionary)
        break; // some broken decoders can't handle a full dictionary, too
      if (optimize.maxTokens     > 0 && numTokens  >= optimize.maxTokens)
        break;

      // ----- the next 50+ lines to try optimize matchLength (greedy/non-greedy) -----

      // total number of bytes left in the current block
      unsigned int remaining = length + from - i;
      // find longest match (greedy), must not exceed number of available bytes
      matchLength = findMatch(i, remaining);

      // non-greedy lookahead
      bool tryNonGreedy = !optimize.greedy;
      if (matchLength == 1 || matchLength < optimize.minNonGreedyMatch)
        tryNonGreedy = false;
      // non-greedy search doesn't make sense when we're very close to the end
      if (i + matchLength + 4 >= m_data.size()) // the number 4 was chosen randomly ...
        tryNonGreedy = false;

      // flexible parsing
      if (tryNonGreedy)
      {
        // don't split long runs of the same pixels
        if (!optimize.splitRuns)// && matchLength >= 2)
        {
          // cheap check of first and last byte
          unsigned int lastMatchByte = matchLength - 1;
          bool allTheSame = (m_data[i] == m_data[i + lastMatchByte]);

          // and the rest of them until a mismatch is found
          for (unsigned int scan = 1; scan + 1 < lastMatchByte && allTheSame; scan++)
            allTheSame = (m_data[i] == m_data[i + scan]);
          // all pixels are identical ?
          if (allTheSame)
            tryNonGreedy = false;
        }

        // greedy matching after the current match
        unsigned int second  = findMatch(i + matchLength, remaining - matchLength);
        // sum of these two greedy matches
        unsigned int best    = matchLength + second;
        // look for an improvement
        unsigned int atLeast = best + optimize.minImprovement;

        // store length of currently best greedy/non-greedy match
        unsigned int choice = matchLength;
        // try all non-greedy lengths
        for (unsigned int shorter = matchLength - 1; shorter > 0; shorter--)
        {
          // greedy match of everything that follows
          unsigned int next = findMatch(i + shorter, remaining - shorter);
          unsigned int sum  = shorter + next;
          // longer ?
          if (sum >= atLeast && sum > best)
          {
            best   = shorter + next;
            choice = shorter;
          }
        }

        // found a better sequence than greedy match ?
        if (choice < matchLength)
        {
#ifdef ALLOW_VERBOSE
          /*if (emitBitStream && optimize.verbose)
            std::cout << "use non-greedy @ " << i << " \t before " << matchLength << "+" << second << "=" << matchLength+second
                                                  << "\tnow " << choice << "+" << (best - choice) << "=" << best
                                                  << " \t => +" << (best - (matchLength + second))
                                                  << std::endl;
          */
#endif
          matchLength = choice;
          numNonGreedyMatches++;
        }
      } // end of flexible parsing

      // ----- LZW code generation -----

      // need one more bit per code ?
      if (m_dictSize < m_maxDictionary)
      {
        unsigned int threshold = m_dictSize - 1;
        // detect powerOfTwo (see https://bits.stephan-brumme.com/isPowerOfTwo.html )
        if ((threshold & (threshold - 1)) == 0 && codeSize < m_maxCodeLength) // but never use more than 12 (or 16) bits
        {
          codeSize++;
          // undo if first token (.Z format)
          if (!m_isGif && threshold == 256)
            codeSize--;
        }
      }

      // update dictionary
      unsigned int code = addCode(i, matchLength);
      // append code to LZW stream
      if (emitBitStream)
        add(result, code, codeSize);

      // counters
      numBits += codeSize;
      numTokens++;
    }

    // "eat" next byte of the current match (until it's 0 and we look for the next match)
    matchLength--;

    // ----- cost evaluation -----

    // cost already determined ?
    if (optimize.readOnlyBest)
      continue;

    // don't update m_best if no block optimization started at the current slot
    bool isLastByte   = (i + 1 == m_data.size());
    // look at compression result of the remaining bytes
    unsigned int next =  i + 1;
    unsigned int nextAligned = next;
    if (optimize.alignment > 1)
      nextAligned = (next + optimize.alignment - 1) / optimize.alignment; // find current m_best index
    if (!isLastByte && m_best[nextAligned].totalBits == 0)
      continue;

    // save cost information only on aligned addresses (except for the last bytes)
    if (optimize.alignment > 1 && numBytes % optimize.alignment != 0)
    {
      //if (!isFinal)
        //continue;
      if (!isLastByte)
        continue;
    }

    // assuming the block would end here, a few extra bits are needed
    unsigned int add = codeSize; // clear / end-of-stream
    // increase code size just for the clear / end-of-stream code ?
    size_t threshold = m_dictSize - 1;
    if ((threshold & (threshold - 1)) == 0 && codeSize < m_maxCodeLength)
      add++;

    if (!m_isGif)
    {
      // TODO: only allow restart if clear code can be encoded in 16 bits
      if (!isLastByte && codeSize < 16)
        continue;

      // no endOfStream token in .Z file format
      if (isLastByte)
        add = 0;

      // fill last byte
      if (numBits % 8 != 0)
        add += 8 - (numBits % 8);

      // dictionary resets are followed by a bunch of zeros
      if (!isLastByte)
      {
        unsigned int tokensPlusClear = numTokens + 1;
        // compress' LZW must be aligned to 8 tokens
        unsigned int mod8 = tokensPlusClear & 7;
        unsigned int gap  = mod8 == 0 ? 0 : 8 - mod8;
        add += codeSize * gap;
      }
    }

    // true if the we are currently "inside" a match
    bool isPartial = (matchLength > 0);

    // compute final cost
    unsigned int       trueBits  = numBits  + add;
    unsigned long long totalBits = trueBits + m_best[nextAligned].totalBits;

    // better path ? (or no path found at all so far)
    BestBlock& best = m_best[fromAligned];
    if (best.totalBits == 0 || best.totalBits >= totalBits)
    {
      best.bits      = trueBits;
      best.totalBits = totalBits;
      best.length    = numBytes;
      best.tokens    = numTokens;
      best.partial   = isPartial;
      best.nongreedy = numNonGreedyMatches;

      // note: if there are multiple paths with the same cost, then longer blocks are preferred
      //       to change this, replace "best.totalBits >  totalBits"
      //                            by "best.totalBits >= totalBits"
      //       but keep in mind that a dictionary restart costs CPU cycles during decoding
    }
  }

  // done
  if (emitBitStream)
  {
    // end of block: emit either clear or endOfStream code
    codeSize = getMinBits(m_dictSize - 1);
    if (m_isGif)
    {
      add(result, isFinal ? endOfStream : clear, codeSize);
    }
    else
    {
      // only clear code, no endOfStream
      if (!isFinal)
      {
        add(result, clear, codeSize);
        numTokens++;
      }

      // fill current byte
      while (result.size() % 8 != 0)
      {
        result.push_back(0);
        numBits++;
      }

      // a block's number of token has to be a multiple of 8
      if (!isFinal)
      {
        if (codeSize != 16)
        {
#ifdef ALLOW_VERBOSE
          std::cerr << "LZW code size is " << (int)codeSize << std::endl;
          throw "flexiGIF currently only supports restarts with code size = 16";
#endif
          //throw "experimental code: currently only 16 bit clear codes supported";
          // some experimental values for numZeros if codeSize < 16:
          // 20/9 => 3
          // 21/9 => 2
          // 22/9 => 1
          // 30/9 => 2
          // 31/9 => 1
          // 50/9 => 3
        }

        // same formulas as in LzwDecoder
        unsigned int mod8 = numTokens & 7;
        unsigned int gap  = mod8 == 0 ? 0 : 8 - mod8;
        // add zeros
        unsigned int numZeros = codeSize * gap / 8;

#ifdef ALLOW_VERBOSE
        //std::cout << "pad " << numZeros << " zeros" << std::endl;
#endif
        for (unsigned int bit = 0; bit < 8*numZeros; bit++)
          result.push_back(0);
      }
    }
  }

  // error checking
  if (m_best[fromAligned].length == 0)
  {
#ifdef ALLOW_VERBOSE
    //std::cerr <<  << std::endl;
    //throw "current block didn't yield any optimization infos";
#endif
  }

  return result;
}


/// determine best block boundaries based on results of optimizePartial() and call merge()
LzwEncoder::BitStream LzwEncoder::optimize(OptimizationSettings optimize)
{
  // find shortest path
  unsigned int pos     = 0;
  unsigned int aligned = 0;
  std::vector<unsigned int> restarts;
  while (pos < m_data.size())
  {
    unsigned int length = m_best[aligned].length;
    // no continuation ?
    if (length == 0)
    {
#ifdef ALLOW_VERBOSE
      if (optimize.verbose)
        std::cerr << "gap @ " << pos << " (aligned=" << aligned << ")" << std::endl;
#endif
      throw "you need to choose a smaller alignment value or increase the token limit because there is a gap between two blocks";
    }

    pos    += length;
    aligned = pos / optimize.alignment;
    restarts.push_back(pos);
  }

  // LZW compress along the shortest path
  return merge(restarts, optimize);
}


/// optimize if block boundaries are known
LzwEncoder::BitStream LzwEncoder::merge(std::vector<unsigned int> restarts, OptimizationSettings optimize)
{
  // final result
  BitStream result;
  result.reserve(m_data.size() * 3); // basic heuristic to estimate memory consumption

  // optional: prepend clear code
  if (optimize.startWithClearCode && m_isGif)
  {
    // it's 2^minCodeSize which is a bunch of zeros followed by a one
    for (unsigned int i = 0; i < optimize.minCodeSize; i++)
      result.push_back(false);
    result.push_back(true);
  }

  // check number of restarts
  if (restarts.empty())
    return result;
  if (restarts.back() < m_data.size())
    restarts.push_back((unsigned int)m_data.size());

  // statistics
  std::vector<unsigned int> sizes;

  // verbose mode displays garbage if using predefined block sizes
  bool verbose = optimize.verbose;
  if (m_best.empty() || m_best[0].bits == 0)
    verbose = false;

  unsigned int pos = 0;
  for (size_t i = 0; i < restarts.size(); i++)
  {
    // dummy entry for new block at pos 0
    if (restarts[i] == 0)
      continue;

    bool isFinal = (i == restarts.size() - 1);
    unsigned int length = restarts[i] - pos;

    // switch to faster settings
    if (!m_best.empty())
    {
      optimize.greedy = (m_best[pos / optimize.alignment].nongreedy == 0); // speed-up if non-greedy search was enabled but not successful in this block
      if (optimize.greedy)
        optimize.avoidNonGreedyAgain = true;
    }
    optimize.readOnlyBest = true;

    // compute current block
    BitStream block = optimizePartial(pos, length, true, isFinal, optimize);
    if (block.empty() && length > 0)
    {
      std::cerr << "internal error, block @ " << pos << " with length " << length << " is empty" << std::endl;
      throw "optimization failed due to an internal error";
    }
    // add to previous stuff
    result.insert(result.end(), block.begin(), block.end());

    unsigned int current = (unsigned int)block.size();
    sizes.push_back(current);

#ifdef ALLOW_VERBOSE
    if (verbose)
    {
      unsigned int aligned = (pos + optimize.alignment - 1) / optimize.alignment;
      BestBlock    block   = m_best[aligned];
      const char*  pixel   = m_isGif ? "pixel" : "byte";
      std::cout << "cost @ " << pos << " \t=> bits=" << sizes[i] << " \t" << pixel << "s=" << block.length << "\ttokens=" << block.tokens
                << "\tbits/" << pixel << "=" << float(sizes[i]) / block.length
                //<< "\testimated_bits=" << block.bits << " "
                << (block.bits == sizes[i] ? "" : "?")
                << "\tnon-greedy=" << block.nongreedy
                << (block.partial       ? ", last match is partial" : "")
                << std::endl;
    }
#endif

    pos = restarts[i];
  }

#ifdef ALLOW_VERBOSE
  if (optimize.verbose)
    std::cout << "finished, now " << result.size() << " LZW bits = " << (result.size() + 7) / 8 << " bytes" << std::endl;
#endif

  return result;
}


/// add bits to BitStream
void LzwEncoder::add(BitStream& stream, unsigned int token, unsigned char numBits)
{
  while (numBits-- > 0)
  {
    stream.push_back((token & 1) != 0);
    token >>= 1;
  }
}


/// get minimum number of bits to represent token
unsigned char LzwEncoder::getMinBits(unsigned int token)
{
  unsigned char result = 0;
  do
  {
    token >>= 1;
    result++;
  } while (token != 0);

  return result;
}
