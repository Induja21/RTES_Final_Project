# Makefile for building calibration.cpp

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDFLAGS = $(shell pkg-config --libs opencv4)

# Target executable
TARGET = calibration

# Source file
SRC = calibration.cpp

# Object file
OBJ = $(SRC:.cpp=.o)

# Default target
all: $(TARGET)

# Link object file to create executable
$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $(TARGET) $(LDFLAGS)

# Compile source file to object file
$(OBJ): $(SRC)
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags opencv4) -c $(SRC) -o $(OBJ)

# Clean up
clean:
	rm -f $(OBJ) $(TARGET)

# Phony targets
.PHONY: all clean