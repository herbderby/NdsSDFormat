# Compiler and Flags
CXX := clang++
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -Werror -O2 -I./include
AR := ar
ARFLAGS := rcs

# Directories
SRC_DIR := src
INC_DIR := include
BUILD_DIR := build
TEST_DIR := tests
TOOLS_DIR := tools

# Targets
LIB_NAME := libsdformat.a
FORMAT_IMAGE := format_image
TEST_RUNNER := test_runner

# File Lists
LIB_SRCS := $(wildcard $(SRC_DIR)/*.cpp)
LIB_OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(LIB_SRCS))

# Phony Targets
.PHONY: all clean directories

all: directories $(BUILD_DIR)/$(LIB_NAME) $(BUILD_DIR)/$(FORMAT_IMAGE) $(BUILD_DIR)/$(TEST_RUNNER)

# Create Build Directory
directories:
	@mkdir -p $(BUILD_DIR)

# Build Static Library
$(BUILD_DIR)/$(LIB_NAME): $(LIB_OBJS)
	@echo "Packaging Library $@"
	@$(AR) $(ARFLAGS) $@ $^

# Build FormatImage CLI
$(BUILD_DIR)/$(FORMAT_IMAGE): $(TOOLS_DIR)/FormatImage.cpp $(BUILD_DIR)/$(LIB_NAME)
	@echo "Building FormatImage $@"
	@$(CXX) $(CXXFLAGS) $< -L./build -lsdformat -o $@

# Build Test Runner
$(BUILD_DIR)/$(TEST_RUNNER): $(TEST_DIR)/integration_runner.cpp
	@echo "Building Test Runner $@"
	@$(CXX) $(CXXFLAGS) $< -o $@

# Compile Object Files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@echo "Compiling $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo "Cleaning build directory"
	@rm -rf $(BUILD_DIR)
