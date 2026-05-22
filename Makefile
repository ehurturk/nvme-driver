CXX := g++
CC := gcc

CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -g -Iinclude
LDFLAGS :=

BUILD_DIR := build
TARGET := $(BUILD_DIR)/usnvme

SRCS := main.cpp $(wildcard src/*.cpp)
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all run clean debug release

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -x c++ -c $< -o $@

$(BUILD_DIR)/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: $(TARGET)
	./$(TARGET)

debug: CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
debug: LDFLAGS += -fsanitize=address,undefined
debug: $(TARGET)

release: CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -O2 -DNDEBUG -Iinclude
release: clean $(TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
