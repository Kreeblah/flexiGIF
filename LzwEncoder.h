// //////////////////////////////////////////////////////////
// LzwEncoder.h
// Copyright (c) 2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/flexigif-lossless-gif-lzw-optimization/

#pragma once

#include <vector>
#include <array>

using std::size_t;

/// compress data with LZW algorithm and a user-defined block-splitting algorithm
class LzwEncoder
{
public:
  /// uncompressed data
  typedef std::vector<unsigned char> RawData;
  /// encoded/compressed data
  typedef std::vector<bool> BitStream;

  /// set uncompressed data
  explicit LzwEncoder(const RawData& data, bool isGif = true);

  /// optimization parameters
  struct OptimizationSettings
  {
    /// bits per LZW code
    unsigned char minCodeSize;
    /// for compatibility: first LZW code sent to output is the clear code
    bool startWithClearCode;
    /// display internal stuff while compressing data
    bool verbose;

    /// if zero => greedy search else non-greedy search
    bool greedy;
    /// minimum match length considered for non-greedy search
    unsigned int  minNonGreedyMatch;
    /// minimum number of bytes saved when a non-greedy match is used
    unsigned int  minImprovement;

    /// maximum number of dictionary entries
    unsigned int  maxDictionary;
    /// maximum number of tokens per block (huge values severely affect compression speed)
    unsigned int  maxTokens;
    /// if true then non-greedy matching is allowed to analyze a string of identical bytes
    bool splitRuns;

    /// alignment
    unsigned int alignment;

    /// read-only access to m_best (gives small speed up for final pass when emitBitStream=true)
    bool readOnlyBest;
    /// don't recompute non-greedy infos when greedy search found no greedy matches
    bool avoidNonGreedyAgain;
  };

  /// optimize a single block and update results in m_best
  BitStream optimizePartial(unsigned int from, unsigned int maxLength, bool emitBitStream, bool isFinal, OptimizationSettings optimize);
  /// determine best block boundaries based on results of optimizePartial() and call merge()
  BitStream optimize(OptimizationSettings optimize);

  /// optimize if block boundaries are known
  BitStream merge(std::vector<unsigned int> restarts, OptimizationSettings optimize);

private:
  /// add string at m_data[from...from+length] to the dictionary and return its code
  int          addCode  (unsigned int from, unsigned int length);
  /// return length of longest match, beginning at m_data[from], limited to maxLength
  unsigned int findMatch(unsigned int from, unsigned int maxLength);

  /// add bits to BitStream
  static void          add(BitStream& stream, unsigned int token, unsigned char numBits);
  /// get minimum number of bits to represent token
  static unsigned char getMinBits(unsigned int token);

  // ----- debugging code -----
  RawData debugDecode(int code) const;
  int     findCode   (unsigned int from, unsigned int maxLength) const;

  /// uncompressed data
  RawData m_data;

  // ----- temporary data structures -----
  /// gather statistics of a single compressed block of data
  struct BestBlock
  {
    /// current block: number of bytes (uncompressed input)
    unsigned int length;
    /// current block: number of bits of compressed output
    unsigned int bits;
    /// this plus all following blocks: total number of bits
    unsigned long long totalBits;
    /// current block: number of LZW codes
    unsigned int tokens;
    /// number of non-greedy matches
    unsigned int nongreedy;
    /// true if block's last match isn't greedy
    bool         partial;

    BestBlock()
    : length(0), bits(0), totalBits(0),
      tokens(0), nongreedy(0), partial(false)
    {}
  };
  /// all block of compressed data where m_best[x] is the optimum block (regarding all following blocks) which starts at input byte x
  std::vector<BestBlock> m_best;

  /// for each LZW code store the LZW codes of its children (which is the same as its parent plus one byte, -1 => undefined/no child)
  typedef std::vector<std::array<int, 256> > Dictionary;
  Dictionary    m_dictionary;
  /// number of valid entries in m_dictionary
  unsigned int  m_dictSize;
  /// maximum number of codes in the dictionary
  unsigned int  m_maxDictionary;
  /// GIF = 12, LZW = 16
  unsigned char m_maxCodeLength;

  /// true, if encode as GIF
  bool          m_isGif;
};
