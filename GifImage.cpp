// //////////////////////////////////////////////////////////
// GifImage.cpp
// Copyright (c) 2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/smallzwflexigif-lossless-gif-lzw-optimization/

// official GIF spec: https://www.w3.org/Graphics/GIF/spec-gif89a.txt

#include "GifImage.h"
#include "LzwDecoder.h"

#include <fstream>

#define ALLOW_VERBOSE
#ifdef  ALLOW_VERBOSE
#include <iostream>
#include <iomanip>
#endif

// statics
bool GifImage::verbose = false;

/// load file
GifImage::GifImage(const std::string& filename)
: m_rawHeader(),
  m_rawTrailer(),
  m_version(),
  m_width(0),
  m_height(0),
  m_colorDepth(0),
  m_isSorted(false),
  m_sizeGlobalColorMap(0),
  m_backgroundColor(0),
  m_aspectRatio(0),
  m_isAnimated(false),
  m_globalColorMap(),
  m_input(filename),
  m_frames()
{
  try
  {
    if (m_input.empty())
      throw "file not found or empty";

    // parse header
    parseSignature();
    parseGlobalDescriptor();

#ifdef ALLOW_VERBOSE
    if (verbose)
      std::cout << "'" << filename << "' image size " << m_width << "x" << m_height << ", " << (1 << m_colorDepth) << " colors" << std::endl;
#endif

    // copy global header
    size_t numBytesHeader = m_input.getNumBytesRead();
    std::ifstream rawFile(filename.c_str(), std::ios::in | std::ios::binary);
    m_rawHeader.resize(numBytesHeader);
    rawFile.read((char*)&m_rawHeader[0], numBytesHeader);

    unsigned int totalLzwBits = 0;

    // decompress LZW
    while (true)
    {
      unsigned int bytesReadSoFar = m_input.getNumBytesRead();

      // technically it's impossible to encounter the end-of-file marker before the first frame
      unsigned char marker = m_input.peekBits(8);
      if (marker == 0x3B)
        break;

#ifdef ALLOW_VERBOSE
      if (verbose)
        std::cout << "decompress frame " << (m_frames.size() + 1) << ": ";
#endif

      Frame frame;

      // parse frame header
      parseExtensions     (frame);
      parseLocalDescriptor(frame);

      // and copy frame header
      unsigned int frameHeaderSize = m_input.getNumBytesRead() - bytesReadSoFar;
      frame.rawHeader.resize(frameHeaderSize);
      rawFile.seekg(bytesReadSoFar);
      rawFile.read((char*)&frame.rawHeader[0], frameHeaderSize);

      // decode LZW stream
      unsigned char minCodeSize = m_input.getByte();
      unsigned char maxCodeSize = 12; // constant value according to spec
      LzwDecoder::verbose = GifImage::verbose;
      LzwDecoder lzw(m_input, true, minCodeSize, maxCodeSize, m_width * m_height);
      frame.pixels     = lzw.getBytes();
      frame.codeSize   = lzw.getCodeSize();
      totalLzwBits    += lzw.getNumCompressedBits();
      frame.numLzwBits = lzw.getNumCompressedBits();

      // yeah, finished another frame ...
      m_frames.push_back(frame);
    }

    // read/copy the last bytes of the file, too
    m_rawTrailer = { (unsigned char)m_input.peekBits(8) };
    parseTerminator();

    if (!m_input.empty() != 0)
      throw "there is still some data left ...";

#ifdef ALLOW_VERBOSE
    if (verbose)
    {
      const char* frames = (m_frames.size() == 1) ? "frame" : "frames";
      std::cout << m_frames.size() << " " << frames << ", " << totalLzwBits << " bits, " << m_frames.front().pixels.size() << " pixels plus " << numBytesHeader << " header bytes" << std::endl;
    }
#endif
  }
  catch (const char* e)
  {
#ifdef ALLOW_VERBOSE
    std::cout << "ERROR: " << (e ? e : "(unknown exception)") << std::endl;
#else
    throw;
#endif
  }
}


/// number of frames (or 1 if not animated)
unsigned int GifImage::getNumFrames() const
{
  return (unsigned int)m_frames.size();
}


/// return decompressed data (indices for local/global color map)
const GifImage::Frame& GifImage::getFrame(unsigned int frame) const
{
  if (frame >= m_frames.size())
    throw "invalid frame number";

  return m_frames.at(frame);
}


/// color depth (bits per pixel)
unsigned char GifImage::getColorDepth() const
{
  return m_colorDepth;
}


/// replace LZW data with optimized data and write to disk (bitDepth = 0 means "take value from m_colorDepth")
unsigned int GifImage::writeOptimized(const std::string& filename, const std::vector<std::vector<bool> >& bits, unsigned char bitDepth)
{
  std::ofstream file(filename.c_str(), std::ios::out | std::ios::binary);
  // write original header
  file.write((char*)&m_rawHeader[0], m_rawHeader.size());

  // bits per pixel/code
  if (bitDepth == 0)
    bitDepth = m_colorDepth;

  for (unsigned int frame = 0; frame < bits.size(); frame++)
  {
    // frame header
    file.write((char*)&m_frames[frame].rawHeader[0], m_frames[frame].rawHeader.size());

    // minCodeSize
    file << m_frames[frame].codeSize;

    // iterate over bits
    const std::vector<bool>& current = bits[frame];
    unsigned int pos = 0;
    while (pos < current.size())
    {
      // each block contains at most 255 bytes (=255*8 bits)
      unsigned int bitsCurrentBlock = (unsigned int)current.size() - pos;
      const unsigned int MaxBitsPerBlock = 255*8;
      if (bitsCurrentBlock > MaxBitsPerBlock)
        bitsCurrentBlock = MaxBitsPerBlock;

      // size in bytes, round up
      unsigned char blockSize = (bitsCurrentBlock + 7) / 8;
      file << blockSize;

      // merge single bits, write to disk
      for (unsigned int i = 0; i < blockSize; i++)
      {
        // create one byte
        unsigned char oneByte = 0;
        for (unsigned char bit = 0; bit < 8 && pos < current.size(); bit++, pos++)
          if (current[pos])
            oneByte |= 1 << bit;

        // and write to disk
        file << oneByte;
      }
    }

    // add an empty block after each image
    file << (unsigned char) 0;
  }

  // write terminator
  file.write((char*)&m_rawTrailer[0], m_rawTrailer.size());

  // and we're done
  unsigned int filesize = (unsigned int)file.tellp();
  file.close();
  return filesize;
}


/// for debugging only: store image data in PPM format
bool GifImage::dumpPpm(const std::string& filename, unsigned int frame) const
{
  const Frame& current = m_frames.at(frame);
  if (current.width  != m_width ||
      current.height != m_height)
    throw "PPM for partial frames not supported yet";

  // header
  std::ofstream file(filename.c_str(), std::ios::out | std::ios::binary);
  if (!file)
    return false;

  file << "P6\n"
       << m_width << " " << m_height << "\n"
       << "255\n";

  // color mapping for current frame
  std::vector<Color> colorMap = m_globalColorMap;
  for (unsigned int i = 0; i < current.localColorMap.size(); i++)
    colorMap[i] = current.localColorMap[i];

  // convert indices to RGB
  const Bytes& pixels = current.pixels;
  for (unsigned int i = 0; i < pixels.size(); i++)
  {
    Color color = colorMap[pixels[i]];
    file << color.red
         << color.green
         << color.blue;
  }

  return file.good();
}


/// for debugging only: store indices
bool GifImage::dumpIndices(const std::string& filename, unsigned int frame) const
{
  const Frame& current = m_frames.at(frame);
  if (current.width  != m_width ||
      current.height != m_height)
    throw "dumping indices of partial frames not supported yet";

  // current.pixels are a consecutive memory block, just dump to disk
  std::ofstream file(filename.c_str(), std::ios::out | std::ios::binary);
  file.write((const char*)&current.pixels[0], current.pixels.size());
  return file.good();
}


/// convert from non-interlaced to interlaced (and vice versa)
void GifImage::setInterlacing(bool makeInterlaced)
{
  if (m_frames.size() != 1)
    throw "code doesn't work yet for animated GIFs";

  unsigned int posInterlaced = m_frames.front().posInterlaced;
  if (posInterlaced == 0)
    throw "interlaced bit not found";

  if (m_isAnimated)
    throw "interlacing in animation not supported yet";

  // interlacing doesn't matter for a single line
  if (m_height <= 1)
    return;

  const unsigned char mask = 0x40;
  bool isInterlaced = (m_rawHeader[posInterlaced] & mask) != 0;
  // keep current interlacing mode ?
  if (isInterlaced == makeInterlaced)
    return;

  // line order:
  // A) every 8th row, beginning with 0th row
  // B) every 8th row, beginning with 4th row
  // C) every 4th row, beginning with 2nd row
  // D) every 2nd row, beginning with 1st row

  for (unsigned int frame = 0; frame < m_frames.size(); frame++)
  {
    Bytes& current = m_frames[frame].pixels;

    if (makeInterlaced)
    {
      // non-interlaced => interlaced

      // set flag
      m_rawHeader[posInterlaced] |= mask;

      // re-order lines
      Bytes interlaced;
      interlaced.reserve(current.size());
      for (unsigned int y = 0; y < m_height; y += 8)
        interlaced.insert(interlaced.end(), current.begin() + y * m_width, current.begin() + y * m_width + m_width);
      for (unsigned int y = 4; y < m_height; y += 8)
        interlaced.insert(interlaced.end(), current.begin() + y * m_width, current.begin() + y * m_width + m_width);
      for (unsigned int y = 2; y < m_height; y += 4)
        interlaced.insert(interlaced.end(), current.begin() + y * m_width, current.begin() + y * m_width + m_width);
      for (unsigned int y = 1; y < m_height; y += 2)
        interlaced.insert(interlaced.end(), current.begin() + y * m_width, current.begin() + y * m_width + m_width);
      current = interlaced;
    }
    else
    {
      // interlaced => non-interlaced

      // unset flag
      m_rawHeader[posInterlaced] &= ~mask;

      // re-order lines
      Bytes interlaced = current;
      current.clear();

      // offsets of the first line of each line
      unsigned int pass0 = 0;
      unsigned int pass1 = (m_height + 8 - 1) / 8;
      // same as
      //pass1 = pass0;
      //for (unsigned int y = 0; y < m_height; y += 8)
      //  pass1++;

      unsigned int pass2 = pass1 + (m_height + 8 - 5) / 8;
      //pass2 = pass1;
      //for (unsigned int y = 4; y < m_height; y += 8)
      //  pass2++;

      unsigned int pass3 = pass2 + (m_height + 4 - 1) / 4;
      //pass3 = pass2;
      //for (unsigned int y = 2; y < m_height; y += 4)
        //pass3++;

      for (unsigned int y = 0; y < m_height; y++)
        switch (y % 8)
        {
        case 0:
          current.insert(current.end(), interlaced.begin() + pass0 * m_width, interlaced.begin() + (pass0 + 1) * m_width);
          pass0++;
          break;

        case 4:
          current.insert(current.end(), interlaced.begin() + pass1 * m_width, interlaced.begin() + (pass1 + 1) * m_width);
          pass1++;
          break;

        case 2: case 6:
          current.insert(current.end(), interlaced.begin() + pass2 * m_width, interlaced.begin() + (pass2 + 1) * m_width);
          pass2++;
          break;

        case 1: case 3: case 5: case 7:
          current.insert(current.end(), interlaced.begin() + pass3 * m_width, interlaced.begin() + (pass3 + 1) * m_width);
          pass3++;
          break;
        }
    }
  }
}


/// read signature GIF 87a/89a
void GifImage::parseSignature()
{
  const unsigned int NumBytes = 6;
  m_version.resize(NumBytes);
  for (unsigned int i = 0; i < NumBytes; i++)
    m_version[i] = m_input.getByte();

  // always starts with "GIF"
  if (m_version[0] != 'G' ||
      m_version[1] != 'I' ||
      m_version[2] != 'F')
    throw "invalid file signature";

  // version is either "87a" or "89a"
  if ( m_version[3] != '8' ||
      (m_version[4] != '7' && m_version[4] != '9') ||
       m_version[5] != 'a')
    throw "invalid GIF version, only 87a and 89a supported";
}


/// global image parameters (constant for all frame)
void GifImage::parseGlobalDescriptor()
{
  // get size (16 bits stored little endian)
  m_width  = getWord();
  m_height = getWord();

  // bits per color => 8 if 256 colors
  m_colorDepth  = m_input.getBits(3) + 1;
  m_sizeGlobalColorMap = 1 << m_colorDepth;

  // unused: true if colors are sorted descendingly (by "importance")
  m_isSorted = m_input.getBool();
  m_input.removeBits(3); // skip 3 bite

  // has palette ?
  bool hasGlobalColorMap = m_input.getBool();
  if (!hasGlobalColorMap)
    m_sizeGlobalColorMap = 0;

  m_backgroundColor = m_input.getByte();
  m_aspectRatio     = m_input.getByte();

  // read global palette
  m_globalColorMap.resize(m_sizeGlobalColorMap);
  for (unsigned int i = 0; i < m_sizeGlobalColorMap; i++)
  {
    Color color;
    color.red   = m_input.getByte();
    color.green = m_input.getByte();
    color.blue  = m_input.getByte();

    m_globalColorMap[i] = color;
  }
}


/// GIF extensions (e.g. animation settings)
void GifImage::parseExtensions(Frame& frame)
{
  while (true)
  {
    // each extension starts with 0x21
    unsigned char marker = m_input.peekBits(8);
    if (marker != 0x21)
      return;
    m_input.removeBits(8);

    // get extension type
    ExtensionType identifier = (ExtensionType)m_input.getBits(8);

    if (identifier == GraphicControl)
    {
      //std::cout << "HACK: assume animated" << std::endl;
      m_isAnimated = true;
    }

    // read all its parts (usually just one part)
    Bytes data;
    while (true)
    {
      unsigned char length = m_input.getByte();
      // last part ?
      if (length == 0)
        break;

      // skip contents
      for (unsigned char i = 0; i < length; i++)
        data.push_back(m_input.getByte());
    };

    frame.extensions.push_back(std::make_pair(identifier, data));
  };
}


/// local image parameters
void GifImage::parseLocalDescriptor(Frame& frame)
{
  unsigned char identifier = m_input.getByte();
  if (identifier != 0x2C)
    throw "expected local descriptor, but not found";

  // frame dimensions
  frame.offsetLeft = getWord();
  frame.offsetTop  = getWord();
  frame.width      = getWord();
  frame.height     = getWord();

  // HACK: doesn't work for animations
  frame.posInterlaced = m_input.getNumBytesRead();

  // color map related stuff
  unsigned int sizeLocalColorMap = 1 << (m_input.getBits(3) + 1);
  m_input.removeBits(2);
  frame.isSorted        = m_input.getBool();
  frame.isInterlaced    = m_input.getBool();
  bool hasLocalColorMap = m_input.getBool();
  if (!hasLocalColorMap)
    sizeLocalColorMap = 0;

#ifdef ALLOW_VERBOSE
  if (verbose)
  {
    std::cout << frame.width << "x" << frame.height << " located at " << frame.offsetLeft << "x" << frame.offsetTop;
    if (frame.isInterlaced)
      std::cout << ", interlaced";
    if (hasLocalColorMap)
      std::cout << ", local color map size=" << sizeLocalColorMap;
  }
#endif

  // copy RGB colors
  frame.localColorMap.resize(sizeLocalColorMap);
  for (unsigned int i = 0; i < sizeLocalColorMap; i++)
  {
    Color color;
    color.red   = m_input.getByte();
    color.green = m_input.getByte();
    color.blue  = m_input.getByte();

    frame.localColorMap[i] = color;
  }
}


/// final bytes of the image
void GifImage::parseTerminator()
{
  unsigned char identifier = m_input.getByte();
  if (identifier != 0x3B)
    throw "invalid terminator";
}


/// read 16 bits, little endian
unsigned short GifImage::getWord()
{
  unsigned short low  = m_input.getByte();
  unsigned short high = m_input.getByte();
  return low + (high << 8);
}
