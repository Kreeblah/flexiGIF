// //////////////////////////////////////////////////////////
// Compress.cpp
// Copyright (c) 2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/smallzwflexigif-lossless-gif-lzw-optimization/

// I couldn't find a proper spec of the .Z file format
// my code is loosely based on GZIP's LZW implementation
// https://research.cs.wisc.edu/wpis/examples/pcca/gzip/gzip.c
// other resources:
// https://wiki.wxwidgets.org/Development:_Z_File_Format
// https://www.ioccc.org/2015/mills2/hint.html

#include "Compress.h"
#include "LzwDecoder.h"

#include <fstream>

#define ALLOW_VERBOSE
#ifdef  ALLOW_VERBOSE
#include <iostream>
#include <iomanip>
#endif

// statics
bool Compress::verbose = false;

/// load file
Compress::Compress(const std::string& filename, bool loadAsUncompressedIfWrongMagicBytes)
: m_settings(0),
  m_input(filename),
  m_data()
{
  try
  {
    if (m_input.empty())
      throw "file not found or empty";

    // check magic bytes
    bool isZ = (m_input.getByte() == MagicByte1);
    isZ     |= (m_input.getByte() == MagicByte2);

    // has proper magic bytes ?
    if (isZ)
    {
      // compression settings
      m_settings = m_input.getByte();
      // default format is "block mode", where the highest bit is set
      if ((m_settings & 0x80) == 0)
        throw "only .Z block mode supported";
      // unused bits, must be zero
      if ((m_settings & 0x60) != 0)
        throw "unknown .Z format flag found";

      // maximum bits per LZW code, almost always 16
      unsigned char maxBits = m_settings & 0x1F;

      // get filesize
      std::fstream in(filename.c_str(), std::ios::in | std::ios::binary);
      in.seekg(0, in.end);
      unsigned int filesize = (unsigned int)in.tellg();
      unsigned int expected = 3 * filesize; // crude heuristic for size of uncompressed data
      in.close();

      // and decompress !
      LzwDecoder::verbose = Compress::verbose;
      LzwDecoder lzw(m_input, false, 8, maxBits, expected);
      m_data = lzw.getBytes();
    }
    else
    {
      // should it have a .Z file ?
      if (!loadAsUncompressedIfWrongMagicBytes)
        throw "file is not a .Z compressed file (magic bytes don't match)";

      // just read from disk
      std::fstream in(filename.c_str(), std::ios::in | std::ios::binary);
      // get filesize
      in.seekg(0, in.end);
      size_t numBytes = (size_t)in.tellg();
      // allocate memory and read everything at once
      m_data.resize(numBytes);
      in.seekg(0, in.beg);
      in.read((char*)&m_data[0], numBytes);
    }
  }
  catch (const char* e)
  {
#ifdef ALLOW_VERBOSE
    std::cout << "ERROR: " << (e ? e : "(unknown exception)") << std::endl;
#endif
  }
}


/// replace LZW data with optimized data and write to disk
unsigned int Compress::writeOptimized(const std::string& filename, const std::vector<bool>& bits)
{
  std::ofstream file(filename.c_str(), std::ios::out | std::ios::binary);
  // write magic bytes
  file.put((char)MagicByte1);
  file.put((char)MagicByte2);
  // and settings
  file.put((char)m_settings);

  unsigned int pos = 0;
  // merge single bits, write to disk
  while (pos < bits.size())
  {
    // create one byte
    unsigned char oneByte = 0;
    for (unsigned char bit = 0; bit < 8; bit++, pos++)
      if (pos < bits.size() && bits[pos])
        oneByte |= 1 << bit;

    // and write to disk
    file << oneByte;
  }

  // and we're done
  unsigned int filesize = (unsigned int)file.tellp();
  file.close();
  return filesize;
}


/// get uncompressed contents
const Compress::Bytes& Compress::getData() const
{
  return m_data;
}


/// for debugging only: save uncompressed data
bool Compress::dump(const std::string& filename) const
{
  std::ofstream file(filename.c_str(), std::ios::out | std::ios::binary);
  file.write((char*)&m_data[0], m_data.size());
  return file.good();
}
