CC = g++
CFLAGS = -Wall -Wextra -g -O0 -std=c++17 -I/home/yilegu/squint/pm-cc-bug-finder/deps/libb64/include
LDFLAGS = -L/home/yilegu/squint/pm-cc-bug-finder/deps/libb64/src -lb64

TARGET = generator
SOURCES = generator.cpp
OBJECTS = $(SOURCES:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)
