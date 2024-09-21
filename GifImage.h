// //////////////////////////////////////////////////////////
// GifImage.h
// Copyright (c) 2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/flexigif-lossless-gif-lzw-optimization/

#pragma once

#include "BinaryInputBuffer.h"

#include <vector>
#include <string>
#include <utility> // for std::pair

/// decode a GIF file, support for writing the same file with optimized LZW bitstream
/** errors throw an exception (const char*) **/
class GifImage
{
public:
  // -------------------- data types --------------------
  /// a continuous block of bytes
  typedef std::vector<unsigned char> Bytes;

  /// RGB color
  struct Color
  {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
  };

  /// extension IDs
  enum ExtensionType
  {
    PlainText      = 0x01,
    GraphicControl = 0xF9,
    Comment        = 0xFE,
    Application    = 0xFF
  };

  /// a single frame
  struct Frame
  {
    /// simple copy of frame's header
    Bytes         rawHeader;

    /// extensions
    std::vector<std::pair<ExtensionType, Bytes> > extensions;

    /// each frame's bits per token
    unsigned char codeSize;
    /// pixels / indices
    Bytes         pixels;

    /// frame's upper left corner (relative to the global image)
    unsigned int  offsetLeft;
    unsigned int  offsetTop;
    /// frame size
    unsigned int  width;
    unsigned int  height;

    /// true if colors are sorted
    bool          isSorted;
    /// true if interlaced
    bool          isInterlaced;
    /// file position of interlaced flag (TODO: my code's broken for animated GIFs)
    unsigned int  posInterlaced;
    /// local color map, stored
    std::vector<Color> localColorMap;

    /// original LZW size (in bits)
    unsigned int  numLzwBits;
  };

  // -------------------- methods --------------------
  /// load file
  explicit GifImage(const std::string& filename);

  /// number of frames (or 1 if not animated)
  unsigned int  getNumFrames() const;
  /// return decompressed data (indices for local/global color map)
  const GifImage::Frame& getFrame(unsigned int frame = 0) const;

  /// color depth (bits per pixel)
  unsigned char getColorDepth() const;

  /// replace LZW data with optimized data and write to disk (bitDepth = 0 means "take value from m_colorDepth")
  unsigned int  writeOptimized(const std::string& filename, const std::vector<std::vector<bool> >& bits, unsigned char bitDepth = 0);

  /// convert from non-interlaced to interlaced (and vice versa)
  void setInterlacing(bool makeInterlaced);

  /// for debugging only: store image data in PPM format
  bool dumpPpm(const std::string& filename, unsigned int frame = 0) const;
  /// for debugging only: store indices
  bool dumpIndices(const std::string& filename, unsigned int frame = 0) const;

  /// show debug output
  static bool verbose;

private:
  /// read signature GIF 87a/89a
  void parseSignature();
  /// global image parameters (constant for all frame)
  void parseGlobalDescriptor();
  /// GIF extensions (e.g. animation settings)
  void parseExtensions     (Frame& frame);
  /// local image parameters
  void parseLocalDescriptor(Frame& frame);
  /// final bytes of the image
  void parseTerminator();

  /// read 16 bits from m_input, little endian
  unsigned short getWord();

  /// the header will remain untouched
  Bytes         m_rawHeader;
  /// the last byte will remain untouched, too
  Bytes         m_rawTrailer; // contains just one byte, it's always 0x3B

  /// file version ("GIF87a" or "GIF89a")
  std::string   m_version;
  /// width  (in pixels)
  unsigned int  m_width;
  /// height (in pixels)
  unsigned int  m_height;
  /// bits per color
  unsigned char m_colorDepth;
  /// true if colors are sorted
  bool          m_isSorted;
  /// number of colors
  unsigned int  m_sizeGlobalColorMap;
  /// palette index of background color
  unsigned char m_backgroundColor;
  /// aspect ratio
  unsigned char m_aspectRatio;

  /// true, if animated
  bool          m_isAnimated;

  /// global color map
  std::vector<Color> m_globalColorMap;

  /// simple wrapper to read file bit-wise
  BinaryInputBuffer  m_input;

  /// decompressed frames (indices for local/global color map)
  std::vector<Frame> m_frames;
};
