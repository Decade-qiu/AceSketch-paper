CXX = c++
CXXFLAGS = -O3 -std=c++17 -Wall -Wextra -Wpedantic
HEADERS := $(wildcard algorithms/*.h utils/*.h)

.PHONY: all clean
all: acesketch

acesketch: main.cpp $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) main.cpp $(LDFLAGS) -o $@

clean:
	$(RM) acesketch
