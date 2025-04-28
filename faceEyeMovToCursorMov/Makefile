# Compiler to use
CXX = g++

# Compiler flags
CXXFLAGS = --std=c++23 -Wall -pedantic -Iinc $(shell pkg-config --cflags opencv4)

# Linker flags (e.g., for pthreads)
LDFLAGS = -lpthread $(shell pkg-config --libs opencv4) -lzmq -lX11 -ludev

# Target executable name
TARGET = faceDetection

# Directories
SRC_DIR = src
INC_DIR = inc

# Source files (all .cpp files in src/)
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)

# Object files (replace .cpp with .o, place in current directory)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=%.o)

# Header files (all .hpp files in inc/, for dependency tracking)
HEADERS = $(wildcard $(INC_DIR)/*.hpp)

# Default target
all: $(TARGET)

# Link object files to create the executable
$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Compile source files to object files
%.o: $(SRC_DIR)/%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Phony targets (not actual files)
.PHONY: all clean