# simple Makefile
#MAKEFLAGS += --silent

# compiler settings
CXX = clang++
CXXFLAGS  = -O3 -std=c++17 -Wall -Wextra -pedantic -pedantic-errors -Werror
#CXXFLAGS +=-static
#CXXFLAGS += -ffunction-sections -fdata-sections -Wl,--gc-sections

# input/output
INCLUDES = BinaryInputBuffer.h   GifImage.h   LzwEncoder.h   LzwDecoder.h   Compress.h
SRC      = BinaryInputBuffer.cpp GifImage.cpp LzwEncoder.cpp LzwDecoder.cpp Compress.cpp flexiGIF.cpp
LIBS     =
TARGET   = flexiGIF

# rules
.PHONY: default clean rebuild

default: $(TARGET)

$(TARGET): $(SRC) $(INCLUDES)
	$(CXX) $(CXXFLAGS) $(SRC) $(LIBS) -o $@

clean:
	-rm -f $(TARGET)

rebuild: clean $(TARGET)
