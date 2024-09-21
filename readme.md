# flexiGIF - lossless GIF/LZW optimization

(written by Stephan Brumme, 2018)

## Quick Overview

flexiGIF shrinks GIF files by optimizing their compression scheme (LZW algorithm).
No visual information is changed and the output is 100% pixel-wise identical to the original file - that's why it's called "lossless optimization".
And the results are still GIF files you can open/view with any standard tool.
Animated GIFs are supported as well.

Most files can be reduced by about 2%. I'm not aware of any open-source software outperforming flexiGIF.

The only downside: it may takes several seconds or even minutes for a medium-sized GIF image.
That's several magnitudes slower than other GIF encoders.
Your GIF decoder isn't affected at all - probably it even becomes faster because it has less input to process.

flexiGIF does NOT optimize palette, strip extensions and/or reduces color information.
There are many sophisticated tools out there, such as ImageMagick, that excel at that job.
flexiGIF is designed to be a tool used _after_ you ran those image-processing programs.

Proposed toolchain:
1. create your GIF
2. run an image-optimizer, such as Gifsicle
3. let flexiGIF optimize the LZW bitstream

flexiGIF is a command-line tool and can be easily used with custom scripts, etc.
Keep in mind that flexiGIF's compression is very (!) slow, magnitudes slower than a standard GIF encoder.

## Lossless compression

Unlike the algorithms behind JPEG or PNG, there is is no adjustable compression level in LZ78/LZW compressed data.
So I had to come up with something else ... and I choose _not_ to play around with image specific optimizations.
Instead, I extract the LZW data stream from GIF files and recompress it.

flexiGIF combines two techniques invented by other people - the following ideas are not my original work (I wish they were ...).
To my knowledge, flexiGIF is the first free program to implement both.

## How does it work ?

The LZW algorithm is more than 3 decades old - in 1984 Welch improved the LZ78 by Lempel and Ziv (obviously invented in 1978).
Up to now, almost all GIF/LZW encoders work the same way (loosely based on Wikipedia article of GIF):
1. fill a dictionary with all available symbols, typically that's 0..255
2. find the longest string S in the dictionary matching the current input
3. add S plus the next symbol to the dictionary, emit the dictionary index of S
4. if the dictionary is full, then reset the dictionary (see step 1)
5. go to step 2

Often it takes just a few lines of code to implement that algorithm.
At the moment, flexiGIF has >2,500 lines of code because it tweaks the algorithms in a few subtle ways ... read on !

### Flexible Dictionary Reset

The GIF standard defines a maximum dictionary size of 2^12 = 4096 entries.
Then a so-called clear code should be inserted into the bitstream in order to reset the dictionary to its initial state.
Almost all GIF encoders execute step 4 when the dictionary contains 4096 entries, a few do it a little bit earlier (4093, e.g. IrfanView).

However, the GIF standard also allows to "skip" step 4: if the dictionary is full then you should (not "you must" !) reset the dictionary.
It's explicitly written on the very first page of the GIF standard ("deferred clear code"):
https://www.w3.org/Graphics/GIF/spec-gif89a.txt

What does it mean ? If the dictionary is full, then you are not allowed to execute step 3 (add strings).
But you can still reference old dictionary entries, it becomes static.
If they still are good matches for the current input, then they can provide a nice compression ratio.
Moreover, they most likely exceed the compression ratio of a recently resetted dictionary.
On the other hand, a full dictionary requires longer codes (GIF: 12 bits).

flexiGIF runs a brute-force search to find the best spot to reset the dictionary:
- sometimes it makes sense to reset the dictionary after a few pixels (if pixels' colors change rapidly)
- sometimes the old dictionary is great for several thousand pixels/bytes

### Flexible Matching

In the aforementioned algorithm, step 2 reads: "find the longest string".
This isn't the whole truth, though. Almost all GIF encoders have so-called "greedy" match finders and actually look for the longest match.
But there are certain situations where the _longest_ match is not the _best_ match.
Let's consider a situation where the dictionary contains the strings `a`, `b`, `aa`, `ab`, `aaa`, `abb`.
If the input is `aaabb`, then a greedy matcher splits it into three groups (`aaa`)(`b`)(`b`).
A flexible, non-greedy matcher chooses (`aa`)(`abb`) saving one token.
It accepts a sub-optimal match for the first two bytes because in that case the next match will be much longer.

Since each match creates a new dictionary entry, too, flexible matching causes the LZW dictionary to contain duplicates:
if (`aa`) was chosen then (`aa`)+`a` = `aaa` must be added to the dictionary even though it's already there.
Quite often flexible matching saves a few bytes but unfortunately in a substantial number of cases the opposite is true:
the sub-optimal dictionary worsens the compression ratio.

The encoder's flexible matching is always slower than greedy matching. It doesn't make a difference during decoding, though.

Note: my flexible matcher is limited to two matches (one-step lookahead). It's not optimal parsing such as my smalLZ4 tool.

## Previous Work

There are a few scientific papers about the described algorithms.
In my eyes the most understandable document was written by Nayuki ( https://www.nayuki.io/page/gif-optimizer-java ).
It comes with full Java source code but it's extremely memory-hungry and quite slow. Only flexible dictionary reset is implemented.

Flexible matching is part of pretty much every practical LZ77 compression program, such as GZIP.
It's often called one-step lookahead.

## Code

Everything was written in C++ from scratch. No external libraries are required.
The code can be compiled with GCC, CLang and Visual C++.
I haven't tested flexiGIF on big-endian systems.

## Command-line options

Usage: `flexigif [options] INPUTFILE OUTPUTFILE`

flexiGIF actually produces good results if you use no options at all (just the two filenames).

Most parameters have a short version (such as `-h`) and a long version (`--help`) which is more readable.
Multiple short versions can be merged if they have no numeric value, e.g. `-f -s -v` is the same as `-fsv`.

`-h    --help`
Display help screen.

`-p    --prettygood`
Uses a set of parameters that typically give the best results. Recommended for most inputs but may be REALLY slow.

`-a=x  --alignment=x`
My brute-force search starts at every pixel which is a multiple of this value.
If `-a=1` then every possible block splitting is analyzed.
Higher values speed up the program and reduce memory consumption (`-a=10` is often 10x faster).

`-d=x  --dictionary=x`
Maximum size of the LZW dictionary before a restart is forced.
Typically left unchanged unless you encounter compatibility issues with certain GIF decoders, then `-d=4093` may help (see `-c`, too).

`-t=x  --maxtokens=x`
In order to speed up the brute-force search, a maximum number of matches per block can be defined.
It's extremely rare to find GIF images with more than 20000 matches before resetting the dictionary (=default value).
Often `-t=10000` is enough, too, and about twice as fast.

`-c    --compatible`
Try to emit output that respects bugs/limitations of certain GIF decoders.
At the moment, `-c` is equivalent to `-d=4093`.

`-l    --deinterlace`
Deinterlace GIF images.
Note: not implemented for animated GIFs yet.

`-g    --greedy`
Enable greedy match search (which is default behavior anyway)

`-n=x  --nongreedy=x`
Enable non-greedy match search where x is the minimum match length to be considered.
Default is `-n=2` which means every match with at least two pixels/bytes are checked whether a non-greedy search gives better results.
Higher values cause less duplicates in the dictionary which sometimes improves compression ratio.

`-m=x  --minimprovement=x`
Minimum number of bytes saved by a non-greedy match (requires parameter `-n`, too).
Default is `-m=1`

`-i    --info`
Analyze internal structure of `INPUTFILE`. No need provide `OUTPUTFILE` because everything is written to STDOUT.
It's more or less a debugging switch and the format of the displayed information may change at any time.

`-f    --force`
Overwrite `OUTPUTFILE` if it already exists.

`-r    --splitruns`
Allow partial matching of long runs of the same byte (requires parameter `-n`).
Many GIF files have large areas with the same color.
Non-greedy search often leads to very bad dictionaries when those "long runs" of the same color are split.
Enabling this option is useful only for very few images.

`-u=x  --userdefined=x`
flexiGIF doesn't look for the best dictionary reset position - instead, you provide them !
`x` is an ascendingly sorted list, e.g. `-u=500,2000,9000`
It's considered a debugging option.

`-s    --summary`
When finished, compare filesize of `INPUTFILE` and `OUTPUTFILE`.

`-v    --verbose`
Show debug messages. It's more or less a debugging switch and the format of the displayed information may change at any time.

`-q    --quiet`
No output at all, except when an error occurred.

`-Z`
`INPUTFILE` and `OUTPUTFILE` are stored in .Z file format instead of GIF

`-y    --immediately`
Avoid initial dictionary reset (clear code) and start immediately with compressed data.
This options typically saves a byte.
However, a few popular GIF decoders can't properly handle GIF without an initial dictionary reset.

## Limitations

My .Z encoder and decoder support only dictionary restarts if the current LZW code size is 16 bits.
