// //////////////////////////////////////////////////////////
// flexigif.cpp
// Copyright (c) 2018 Stephan Brumme. All rights reserved.
// see https://create.stephan-brumme.com/flexigif-lossless-gif-lzw-optimization/

#include "GifImage.h"
#include "Compress.h"
#include "LzwDecoder.h"
#include "LzwEncoder.h"

#include <vector>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <ctime>

namespace
{
  // ----- constants -----
  /// program version
  const char* Version = "2018.11b";
  /// return codes
  enum ReturnCode
  {
    NoError                 =  0,
    GenericException        =  1,
    NotImplemented          =  2,
    ParameterOutOfRange     =  3,
    InvalidParameter        =  4,
    MissingParameter        =  5,
    UnknownParameter        =  6,
    ContradictingParameters =  7,
    MoreThanTwoFilenames    =  8,
    SameFile                =  9,
    DontOverwrite           = 10,
    NoFrameFound            = 11,
    OnlyForGifs             = 12,
    DebuggingMode           = 99
  };

  /// default values of the optimizer
  enum DefaultValues
  {
    GifMaxToken      =  20000,
    LzwMaxToken      = 100000,
    GifMaxDictionary =   4096,
    LzwMaxDictionary =  65536,
    GifMaxDictionaryCompatible = GifMaxDictionary - 3, // 4093
    Alignment        =      1,
    MinImprovement   =      1,
    MinNonGreedy     =      2
  };
}


/// terminate program, show help
void help(const std::string& errorMsg = "", int errorCode = NoError, bool showHelp = true)
{
  // error
  if (!errorMsg.empty())
    std::cout << "ERROR: " << errorMsg << std::endl;

  // default help
  if (showHelp)
    std::cout << std::endl
              << "flexiGIF " << Version << ", written by Stephan Brumme" << std::endl
              << "Usage: flexigif [options] INPUTFILE OUTPUTFILE" << std::endl
              << std::endl
              << "Options:" << std::endl
              << " -p    --prettygood         try greedy search plus non-greedy with '-a=" << Alignment << " -n=" << MinNonGreedy << " -m=" << MinImprovement << " -d=" << GifMaxDictionary << "' => typically the best results" << std::endl
              << " -a=x  --alignment=x        blocks starts at multiples of x (default is -a=1 => best compression but may be slow)" << std::endl
              << " -d=x  --dictionary=x       maximum size of the LZW dictionary (default is -d=" << GifMaxDictionary << ", 0 means \"maximum\")" << std::endl
              << " -t=x  --maxtokens=x        maximum number of tokens per block (default is -t=" << GifMaxToken << ")" << std::endl
              << " -c    --compatible         create files that should be more compatible to faulty decoders" << std::endl
              << " -l    --deinterlace        ensure that output is not interlaced" << std::endl
              << " -g    --greedy             enable     greedy match search (default)" << std::endl
              << " -n=x  --nongreedy=x        enable non-greedy match search, x is the minimum match length (default is -n=" << MinNonGreedy << ")" << std::endl
              << " -m=x  --minimprovement=x   minimum number of bytes saved by a non-greedy match (requires parameter -n, default is -m=" << MinImprovement << ")" << std::endl
              << " -i    --info               analyze internal structure of INPUTFILE" << std::endl
              << " -f    --force              overwrite OUTPUTFILE if it already exists" << std::endl
              << " -r    --splitruns          allow partial matching of long runs of the same byte (requires parameter -n)" << std::endl
              << " -u=x  --userdefined=x      don't search but set custom block boundaries, x is an ascendingly sorted list, e.g. -u=500,2000,9000" << std::endl
              << " -s    --summary            when finished, compare filesize of INPUTFILE and OUTPUTFILE" << std::endl
              << " -v    --verbose            show debug messages" << std::endl
              << " -q    --quiet              no output during compression" << std::endl
              << " -Z                         INPUTFILE and OUTPUTFILE are stored in .Z file format instead of .gif" << std::endl
              << " -b=x  --benchmark=x        benchmark GIF decoder, x stands for the number of iterations (default: x=100)" << std::endl
              << " -y    --immediately        avoid initial clear code and start immediately with compressed data" << std::endl
              //<< "      --ppm=x               store x-th frame in PPM format in OUTPUTFILE" << std::endl
              //<< "      --indices=x           store x-th frame's indices in OUTPUTFILE" << std::endl
              //<< "      --compress            INPUTFILE isn't compressed yet and OUTPUTFILE will be a .Z file" << std::endl
              //<< "      --decompress          store decompressed contents of INPUTFILE, which must be a .Z file, in OUTPUTFILE" << std::endl
              << " -h    --help               display this help screen" << std::endl
              << std::endl
              << "See https://create.stephan-brumme.com/flexigif-lossless-gif-lzw-optimization/ for more infos" << std::endl;

  exit(errorCode);
}


/// let's go !
int main(int argc, char** argv)
{
  // no parameters at all ?
  if (argc == 1)
    help();

  // filenames
  std::string input;
  std::string output;

  // other parameters
  bool isGif       = true;  // if false then .Z file format
  bool inputInfo   = false; // just one filename: show some compression details of input file
  bool showSummary = false; // after finishing recompression: display how many bytes were saved
  bool overwrite   = false; // overwrite an existing OUTPUTFILE
  bool quiet       = false; // no console output
  bool verbose     = false; // lots of console output
  bool deinterlace = false; // deinterlace a GIF image

  bool smartGreedy = false;
  bool benchmark   = false; // decompress INPUTFILE several times and measure throughput in MB/sec
  unsigned int iterations = 10;  // benchmark only: repeat x times
  bool showDecompressed = false; // dump a frame in PPM format to OUTPUTFILE
  bool showIndices      = false; // dump a frame's indices to OUTPUTFILE
  unsigned int ppmFrame = 0;     // only relevant if showDecompressed is true
  bool compressZ   = false; // decompress INPUTFILE to OUTPUTFILE (.Z format only)
  bool decompressZ = false; // compress INPUTFILE to OUTPUTFILE (.Z format)

  std::vector<unsigned int> predefinedBlocks; // insert clear codes at these user-defined positions

  // optimizer
  LzwEncoder::OptimizationSettings optimize;
  optimize.alignment           = Alignment;
  optimize.verbose             = false;
  optimize.greedy              = true;
  optimize.minImprovement      = MinImprovement;
  optimize.minNonGreedyMatch   = MinNonGreedy;
  optimize.splitRuns           = false;
  optimize.maxDictionary       = 0;
  optimize.maxTokens           = GifMaxToken;
  optimize.startWithClearCode  = true;
  optimize.readOnlyBest        = false;
  optimize.avoidNonGreedyAgain = false;

  // parse parameters
  std::string current;
  size_t      currentPos = 0; // if several short arguments are merged into one, e.g. "-v -s -f" => "-vsf"

  // arguments 1 ... argc-1
  int parameter = 0;
  while (parameter < argc)
  {
    // long parameters cannot be merged (such as --force)
    if ((current.size() > 2 && current[0] == '-' && current[1] == '-') ||
        (currentPos + 1 >= current.size()))
      current.clear();

    // next parameter ?
    if (current.empty() || currentPos + 1 >= current.size())
    {
      parameter++;
      if (parameter == argc)
        break;
    }

    // read argument
    if (current.empty())
    {
      current = argv[parameter];
      if (current.size() >= 2 && current[0] == '-' && current[1] != '-')
        currentPos = 1;
      else
        currentPos = 0;
    }
    else
    {
      // several short parameters were merged
      currentPos++;
    }

    // if single-letter argument
    char currentShort = current[currentPos];

    // no option => must be a filename
    if (current[0] != '-')
    {
      // first comes the input file
      if (input.empty())
      {
        input = current;
        current.clear();
        continue;
      }
      // then the output file
      if (output.empty())
      {
        output = current;
        current.clear();
        continue;
      }

      // but at most two filenames ...
      help("more than two filenames specified", MoreThanTwoFilenames);
    }

    // help
    if (currentShort == 'h' || current == "--help")
      help();

    // info
    if (currentShort == 'i' || current == "--info")
    {
      inputInfo = true;
      continue;
    }

    // summary
    if (currentShort == 's' || current == "--summary")
    {
      if (quiet)
        help("flag -s (show summary) contradicts -q (quiet)", ContradictingParameters, false);
      showSummary = true;
      continue;
    }

    // overwrite
    if (currentShort == 'f' || current == "--force")
    {
      overwrite = true;
      continue;
    }

    // verbose
    if (currentShort == 'v' || current == "--verbose")
    {
      if (quiet)
        help("flag -v (verbose) contradicts -q (quiet)", ContradictingParameters, false);

      verbose             = true;
      optimize.verbose    = true;
      GifImage::verbose   = true;
      Compress::verbose   = true;
      LzwDecoder::verbose = true;
      continue;
    }

    // quiet
    if (currentShort == 'q' || current == "--quiet")
    {
      if (verbose)
        help("flag -q (quiet) contradicts -v (verbose)",      ContradictingParameters, false);
      if (showSummary)
        help("flag -q (quiet) contradicts -s (show summary)", ContradictingParameters, false);

      quiet = true;
      continue;
    }

    // greedy
    if (currentShort == 'g' || current == "--greedy")
    {
      optimize.greedy = true;
      continue;
    }

    // split runs
    if (currentShort == 'r' || current == "--splitruns")
    {
      optimize.splitRuns = true;
      continue;
    }

    // deinterlace
    if (currentShort == 'l' || current == "--deinterlace")
    {
      deinterlace = true;
      continue;
    }

    // good settings
    if (currentShort == 'p' || current == "--prettygood")
    {
      smartGreedy             = true;
      optimize.greedy         = false;
      optimize.minImprovement = MinImprovement;
      optimize.maxDictionary  = GifMaxDictionary;
      optimize.maxTokens      = GifMaxToken;
      optimize.avoidNonGreedyAgain = true;
      continue;
    }

    // enhance compatibility
    if (currentShort == 'c' || current == "--compatible")
    {
      optimize.maxDictionary = GifMaxDictionaryCompatible; // 4093
      optimize.greedy        = true;
      optimize.startWithClearCode = true;
      continue;
    }

    // avoid initial clear code (relevant for GIFs only)
    if (currentShort == 'y' || current == "--immediately")
    {
      optimize.startWithClearCode = false;
      continue;
    }

    // compress' .Z file format
    if (currentShort == 'Z')
    {
      isGif = false;
      continue;
    }

    // decompress .Z file, nothing else
    if (current == "--decompress")
    {
      decompressZ = true;
      isGif       = false; // implicit -Z flag
      continue;
    }

    // INPUTFILE isn't compressed (applies to .Z files only)
    if (current == "--compress")
    {
      compressZ = true;
      isGif     = false; // implicit -Z flag
      continue;
    }

    // adjustable parameters
    bool hasValue = false;
    int  value = 0;
    std::string strValue;
    size_t splitAt = current.find('=');
    if (splitAt != std::string::npos)
    {
      // get right-hand side and store number/string in variables value/strValue
      strValue = current.substr(splitAt + 1);
      value    = atoi(strValue.c_str());
      // keep only left-hand side of parameter
      current.resize(splitAt);
      // no other argument may be merged with this one
      currentPos = current.size();

      hasValue = !strValue.empty();
    }

    // alignment/blockstart
    if (current == "-a" || current == "--alignment")
    {
      if (value <= 0)
        help("parameter -a/--alignment cannot be zero", ParameterOutOfRange, false);

      optimize.alignment = (unsigned int)value;
      continue;
    }

    // maximum size of dictionary
    if (current == "-d" || current == "--dictionary")
    {
      if (value <= 0)
        help("parameter -d/--dictionary cannot be zero", ParameterOutOfRange, false);

      optimize.maxDictionary = (unsigned int)value;
      continue;
    }

    // maximum number of tokens per block
    if (current == "-t" || current == "--maxtokens")
    {
      if (value < 0)
        value = 0; // no limit

      optimize.maxTokens = (unsigned int)value;
      continue;
    }

    // non-greedy minimum improvement
    if (current == "-m" || current == "--minimprovement")
    {
      if (value <= 0)
        help("parameter -m/--minimprovement cannot be zero", ParameterOutOfRange, false);

      optimize.minImprovement = (unsigned int)value;
      continue;
    }

    // non-greedy match length
    if (currentShort == 'n' || current == "--nongreedy")
    {
      optimize.greedy = false;
      optimize.minNonGreedyMatch = hasValue ? (unsigned int)value : MinNonGreedy;

      if (value < 2)
        help("parameter -n/--nongreedy cannot be less than 2", ParameterOutOfRange, false);

      continue;
    }

    // predefined block boundaries
    if (current == "-u" || current == "--userdefined")
    {
      // syntax: a,b,c where a/b/c are decimal numbers
      predefinedBlocks.push_back(0);
      for (size_t i = 0; i < strValue.size(); i++)
      {
        char oneByte = strValue[i];
        // next number follows ?
        if (oneByte == ',')
        {
          predefinedBlocks.push_back(0);
          continue;
        }

        if (oneByte < '0' || oneByte > '9')
          help("invalid syntax for parameter -u/--userdefined: it must be a sorted list of numbers", InvalidParameter, false);

        // process digit
        predefinedBlocks.back() *= 10;
        predefinedBlocks.back() += oneByte - '0';
      }

      // check whether the list is in ascending order (duplicates are disallowed, too)
      for (size_t i = 1; i < predefinedBlocks.size(); i++)
        if (predefinedBlocks[i - 1] >= predefinedBlocks[i])
          help("invalid syntax for parameter -u/--userdefined: it must be a sorted list of numbers", InvalidParameter, false);

      continue;
    }

    // benchmark, user-defined number of iterations
    if (current == "-b" || current == "--benchmark")
    {
      benchmark  = true;
      iterations = hasValue ? value : 100; // default: decode 100x
      if (value < 1)
        help("parameter -b/--benchmark cannot be zero", ParameterOutOfRange, false);
      continue;
    }

    // PPM output of a GIF frame of LZW decompression of Z file (TODO: jsut debugging code, not in public interface yet)
    if (current == "--ppm")
    {
      showDecompressed = true;
      ppmFrame = hasValue ? value : 1; // first frame by default
      continue;
    }

    // PPM output of a GIF frame of LZW decompression of Z file (TODO: jsut debugging code, not in public interface yet)
    if (current == "--indices")
    {
      showIndices = true;
      ppmFrame = hasValue ? value : 1; // first frame by default
      continue;
    }

    // whoopsie ...
    help("unknown parameter " + current, 1);
  }

  try
  {
    // auto-detect .Z files
    if (input.size() > 2 &&
        input[input.size() - 2] == '.' &&
        input[input.size() - 1] == 'Z')
      isGif = false;

    if (!isGif)
    {
      // increase token limit
      if (optimize.maxTokens == GifMaxToken)
        optimize.maxTokens = LzwMaxToken;
    }

    // check parameter combinations
    if (optimize.splitRuns && optimize.greedy)
      help("parameter -r requires -n", MissingParameter);

    // only one parameter: a file name => automatically switch to "info mode"
    if (argc == 2 && !input.empty())
      inputInfo = true;

    // show GIF/LZW infos about input file
    if (inputInfo)
    {
      if ( input .empty())
        help("no filename provided", MissingParameter, false);
      if (!output.empty())
        help("too many filenames provided (accepting only one)", MoreThanTwoFilenames, false);

      // just load and parse
      if (isGif)
      {
        GifImage::verbose = true;
        GifImage info(input);
      }
      else
      {
        Compress::verbose = true;
        Compress info(input);
      }
      return NoError;
    }

    // benchmark
    if (benchmark)
    {
      if (input.empty())
        help("missing INPUTFILE", MissingParameter, false);

      std::cout << "benchmarking '" << input << "' ..." << std::endl
                << "decoding file, " << iterations << " iterations" << std::endl;

      clock_t start = clock();

      unsigned int numDecodedFrames = 0;
      unsigned long long numPixels  = 0;

      for (unsigned int i = 0; i < iterations; i++)
      {
        if (isGif)
        {
          // parse file
          GifImage gif(input);

          // error during decoding
          if (gif.getNumFrames() == 0)
            help("no frames found in " + input, NoFrameFound, false);

          // statistics
          numDecodedFrames += gif.getNumFrames();
          for (unsigned int frame = 0; frame < gif.getNumFrames(); frame++)
            numPixels += gif.getFrame(frame).pixels.size();

          // disable verbose output for the 2..n iteration
          GifImage::verbose = false;
        }
        else
        {
          // parse file
          Compress lzw(input, compressZ);
          numDecodedFrames++;
          numPixels += lzw.getData().size();
          // disable verbose output for the 2..n iteration
          Compress::verbose = false;
        }
      }

      clock_t finish   = clock();
      float seconds    = (finish - start) / float(CLOCKS_PER_SEC);
      float perFile    = seconds / iterations;
      float perFrame   = seconds / numDecodedFrames;
      float throughput = numPixels / seconds;
      std::cout   << std::fixed
                  << "elapsed:    " << std::setw(8) << std::setprecision(6) << seconds  << " seconds" << std::endl
                  << "per file:   " << std::setw(8) << std::setprecision(6) << perFile  << " seconds" << std::endl;
      if (iterations != numDecodedFrames)
        std::cout << "per frame:  " << std::setw(8) << std::setprecision(6) << perFrame << " seconds" << std::endl;
      std::cout   << "throughput: " << std::setw(8) << std::setprecision(3) << throughput / 1000000 << " megapixel/second" << std::endl;
      return NoError;
    }

    if (input .empty())
      help("missing INPUTFILE",  MissingParameter);
    if (output.empty())
      help("missing OUTPUTFILE", MissingParameter);

    // same name ?
    if (input == output)
      help("INPUTFILE and OUTPUTFILE cannot be the same filename", SameFile, false);

    // don't overwrite by default
    if (!overwrite)
    {
      std::fstream checkOutput(output.c_str());
      if (checkOutput)
        help("OUTPUTFILE already exists, please use -f to overwrite an existing file", DontOverwrite, false);
    }

    // store a single frame in PPM format
    if (showDecompressed || showIndices)
    {
      GifImage gif(input);

      // use 0-index internally instead of 1-index
      ppmFrame--;
      if (ppmFrame >= gif.getNumFrames()) // note: if ppmFrame was 0 => -1 => 0xFFFFFF... => a very large number
        help("please specify a valid frame number", ParameterOutOfRange);

      // write PPM (or plain indices)
      bool ok = false;
      if (showDecompressed)
        ok = gif.dumpPpm(output, ppmFrame);
      else
        ok = gif.dumpIndices(output, ppmFrame);

      return ok ? NoError : DontOverwrite;
    }

    // decompress .Z file
    if (decompressZ)
    {
      Compress lzw(input, compressZ);
      if (lzw.dump(output))
        return NoError;
      else
        return DontOverwrite;
    }

    // -------------------- process input --------------------

    clock_t start = clock();

    if (!quiet)
      std::cout << "flexiGIF " << Version << ", written by Stephan Brumme" << std::endl;
    if (verbose)
    {
      std::cout << "used options:";

      std::cout << " -a=" << optimize.alignment;
      if (optimize.startWithClearCode)
        std::cout << " -c";
      if (optimize.maxDictionary > 0)
        std::cout << " -d=" << optimize.maxDictionary;
      if (overwrite)
        std::cout << " -f";
      if (deinterlace)
        std::cout << " -l";
      if (!optimize.greedy)
        std::cout << " -m=" << optimize.minImprovement;
      if (!optimize.greedy)
        std::cout << " -n=" << optimize.minNonGreedyMatch;
      if (smartGreedy)
        std::cout << " -p";
      if (quiet)
        std::cout << " -q";
      if (optimize.splitRuns && !optimize.greedy)
        std::cout << " -r";
      if (showSummary)
        std::cout << " -s";
      std::cout << " -t=" << optimize.maxTokens;
      if (verbose)
        std::cout << " -v";
      if (!isGif)
        std::cout << " -Z";
      if (compressZ)
        std::cout << " --compress";
      if (decompressZ)
        std::cout << " --decompress";
      if (showIndices)
        std::cout << " --indices=" << ppmFrame;
      if (showDecompressed)
        std::cout << " --ppm=" << ppmFrame;

      std::cout << std::endl;
    }

    if (verbose)
      std::cout << std::endl << "===== decompress '" << input << "' =====" << std::endl;

    if (isGif) // ---------- recompress GIF frames ----------
    {
      // load GIF
      GifImage gif(input);

      // error during decoding ?
      if (gif.getNumFrames() == 0)
        help("no frames found in " + input, NoFrameFound, false);

      // determine minCodeSize, often 8 (up to 256 colors)
      optimize.minCodeSize = 1;
      // look for largest byte in each frame
      unsigned char maxValue = 0;
      const unsigned char EightBits = 0x80;
      for (unsigned int frame = 0; frame < gif.getNumFrames() && maxValue < EightBits; frame++)
      {
        // get bytes / pixels
        const GifImage::Bytes& indices = gif.getFrame(frame).pixels;
        for (size_t i = 0; i < indices.size(); i++)
          // larger ?
          if (maxValue < indices[i])
          {
            maxValue = indices[i];
            // at least 8 bits ? => maximum
            if (maxValue >= EightBits)
              break;
          }
      }
      // compute number of bits
      while (maxValue >= (1 << optimize.minCodeSize))
        optimize.minCodeSize++;
      // codeSize = 1 is not allowed by spec, even b/w images have codeSize = 2
      if (optimize.minCodeSize == 1)
        optimize.minCodeSize = 2;

      // de-interlace non-animated GIFs
      if (deinterlace)
      {
        if (gif.getNumFrames() > 1)
          help("de-interlacing is not supported yet for animated GIFs", NotImplemented, false);
        gif.setInterlacing(false);
      }

      if (gif.getNumFrames() > 1 && !predefinedBlocks.empty())
        help("user-defined block boundaries are not allowed for animated GIFs", NotImplemented, false);

      // -------------------- generate output --------------------

      if (verbose)
        std::cout << std::endl << "===== compression in progress ... =====" << std::endl;

      // optimize all frames
      unsigned int numFrames = gif.getNumFrames();
      std::vector<std::vector<bool> > optimizedFrames;
      for (unsigned int frame = 0; frame < numFrames; frame++)
      {
        // get original LZW bytes
        const GifImage::Frame& current = gif.getFrame(frame);
        const std::vector<unsigned char>& indices = current.pixels;
        LzwEncoder encoded(indices, isGif);
        optimize.minCodeSize = current.codeSize;

        // store optimized LZW bytes
        LzwEncoder::BitStream optimized;

        // look for optimal block boundaries
        if (predefinedBlocks.empty())
        {
          unsigned int lastDisplay = 0;
          int pos = (int)indices.size() - 1;
          for (int i = pos; i >= 0; i--)
          {
            // only if block start is aligned
            if (i % optimize.alignment != 0)
              continue;

            // show progress
            if (!quiet && (i / optimize.alignment) % 8 == 0 && clock() != lastDisplay)
            {
              unsigned int percentage = 100 - (100 * i / indices.size());
              std::cout << "    \rframe " << frame+1 << "/" << numFrames << " (" << indices.size() << " pixels): "
                        << percentage << "% done";

              // ETA
              clock_t now     = clock();
              float elapsed   = (now - start) / float(CLOCKS_PER_SEC);
              float estimated = elapsed * 100 / (percentage + 0.000001f) - elapsed;

              if (elapsed > 3 && numFrames == 1 && estimated >= 1)
                std::cout << " (after " << (int)elapsed << "s, about " << (int)estimated << "s left)";
              std::cout << std::flush;

              lastDisplay = now;
            }

            // estimate cost
            encoded.optimizePartial(i, 0, false, true, optimize);

            // repeat estimation, this time with non-greedy search
            if (smartGreedy && !optimize.greedy)
            {
              optimize.greedy = true;
              encoded.optimizePartial(i, 0, false, true, optimize);
              optimize.greedy = false;
            }
          }

          if (!quiet)
            std::cout << "                            " << std::endl;

          // final bitstream for current image
          optimized = encoded.optimize(optimize);
        }
        else
        {
          // remove invalid block boundaries (or should it be an ERROR ?)
          while (predefinedBlocks.back() > indices.size())
            predefinedBlocks.pop_back();

          // to simplify code, include start and end of file as boundaries, too
          if (predefinedBlocks.empty() || predefinedBlocks.front() != 0)
            predefinedBlocks.insert(predefinedBlocks.begin(), 0);
          if (predefinedBlocks.back() != indices.size())
            predefinedBlocks.push_back(indices.size());

          // avoid certain optimizer settings that might cause incomplete images
          optimize.maxTokens     = 0;
          optimize.maxDictionary = 0;

          optimized = encoded.merge(predefinedBlocks, optimize);
        }

        optimizedFrames.push_back(optimized);
      }

      // write to disk
      gif.writeOptimized(output, optimizedFrames, optimize.minCodeSize);
    }
    else // ---------- .Z file format ----------
    {
      if (!predefinedBlocks.empty())
        help("predefined blocks not implemented yet for .Z files", NotImplemented);

      // disable GIF-only optimizations
      optimize.startWithClearCode = false;

      Compress lzw(input, compressZ);

      // get LZW bytes
      const std::vector<unsigned char>& bytes = lzw.getData();
      LzwEncoder encoded(bytes, isGif);

      // adjust optimizer:
      // always the full ASCII alphabet
      optimize.minCodeSize = 8;
      // dictionary limit is 2^16 instead of 2^12
      if (optimize.maxDictionary == GifMaxDictionary || optimize.maxDictionary == GifMaxDictionaryCompatible)
        optimize.maxDictionary = LzwMaxDictionary; // 0 is an option, too, ... it disables the limit check

      if (verbose)
        std::cout << std::endl << "===== compression in progress ... =====" << std::endl;

      // store optimized LZW bytes
      LzwEncoder::BitStream optimized;

      // look for optimal block boundaries
      unsigned int percentageDone = 0;
      int pos = (int)bytes.size() - 1;
      for (int i = pos; i >= 0; i--)
      {
        // only if block start is aligned
        if (i % optimize.alignment != 0)
          continue;

        // show progress
        float percentage = 100 - (100 * float(i) / bytes.size());
        if (percentage != percentageDone && !quiet)
        {
          // ETA
          clock_t now     = clock();
          float elapsed   = (now - start) / float(CLOCKS_PER_SEC);
          float estimated = elapsed * 100 / (percentage + 0.000001f) - elapsed;

          std::cout << "    \r" << (int)percentage << "% done";
          if (elapsed > 3 && estimated >= 1)
            std::cout << " (after " << (int)elapsed << "s, about " << (int)estimated << "s left)";
          std::cout << std::flush;

          percentageDone = (unsigned int)percentage;
        }

        // estimate cost
        encoded.optimizePartial(i, 0, false, true, optimize);
      }

      if (!quiet)
        std::cout << "                            " << std::endl;

      // write to disk
      optimized = encoded.optimize(optimize);
      lzw.writeOptimized(output, optimized);
    }

    // -------------------- bonus output :-) --------------------
    if (showSummary)
    {
      // measure duration
      clock_t finish  = clock();
      float   seconds = (finish - start) / float(CLOCKS_PER_SEC);

      // get filesizes
      std::fstream in (input .c_str(), std::ios::in | std::ios::binary);
      std::fstream out(output.c_str(), std::ios::in | std::ios::binary);
      in .seekg(0, in .end);
      out.seekg(0, out.end);
      int before = (int)in .tellg();
      int now    = (int)out.tellg();

      if (verbose)
        std::cout << std::endl << "===== done ! =====" << std::endl;

      // smaller, larger ?
      int diff   = before - now;
      if (diff == 0)
        std::cout << "no optimization found for '" << input << "', same size as before (" << now << " bytes).";
      if (diff >  0)
        std::cout << "'" << output << "' is " <<  diff << " bytes smaller than '" << input << "' (" << now << " vs " << before << " bytes) => "
                  << "you saved " << std::fixed << std::setprecision(3) << (diff * 100.0f / before) << "%.";
      if (diff <  0)
      {
        std::cout << "'" << output << "' is " << -diff << " bytes larger than '" << input << "' (" << now << " vs " << before << " bytes).";
        if (optimize.alignment > 1 || optimize.greedy)
          std::cout << " Please use more aggressive optimization settings.";
      }

      std::cout << " Finished after " << std::fixed << std::setprecision(2) << seconds << " seconds." << std::endl;
    }
  }
  catch (const char* e)
  {
    std::cerr << "ERROR: " << (e ? e : "(no message)") << std::endl;
    return GenericException;
  }
  catch (std::exception& e)
  {
    std::cerr << "ERROR: " << (e.what() ? e.what() : "(no message)") << std::endl;
    return GenericException;
  }
  catch (...)
  {
    std::cerr << "ERROR: unknown exception caught" << std::endl;
    return GenericException;
  }

  return NoError;
}
