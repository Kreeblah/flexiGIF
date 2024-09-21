// //////////////////////////////////////////////////////////
// Compress.h
// Copyright (c) 2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/flexigif-lossless-gif-lzw-optimization/

#pragma once

#include "BinaryInputBuffer.h"

#include <vector>
#include <string>

/// decode a .Z compressed file (created by the old compress unix tool)
/** errors throw an exception (const char*) **/
class Compress
{
public:
  // -------------------- data types --------------------
  /// a continuous block of bytes
  typedef std::vector<unsigned char> Bytes;

  // -------------------- methods --------------------
  /// load file
  explicit Compress(const std::string& filename, bool loadAsUncompressedIfWrongMagicBytes = false);

  /// replace LZW data with optimized data and write to disk
  unsigned int writeOptimized(const std::string& filename, const std::vector<bool>& bits);

  /// get uncompressed contents
  const Bytes& getData() const;

  /// for debugging only: save uncompressed data
  bool dump(const std::string& filename) const;

  /// show debug output
  static bool verbose;

private:
  /// first two bytes of every .Z file
  enum
  {
    MagicByte1 = 0x1F,
    MagicByte2 = 0x9D
  };

  /// settings of the original file (third byte of that file)
  unsigned char     m_settings;

  /// simple wrapper to read file bit-wise
  BinaryInputBuffer m_input;
  /// uncompressed bytes
  Bytes             m_data;
};
