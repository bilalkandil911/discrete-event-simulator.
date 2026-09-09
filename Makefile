CXX      = g++
CXXFLAGS = -std=c++17 -Wall -O2
TARGET   = sim

all: $(TARGET)

$(TARGET): main.cpp Queue.h Models.h Stats.h Simulator.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
