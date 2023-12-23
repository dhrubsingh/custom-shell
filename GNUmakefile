# Compiler settings - using clang which is default on macOS
CXX = clang++
CPPFLAGS = # Add any C preprocessor flags here
CXXFLAGS = -Wall -std=c++11 # Add other C++ flags as needed
LDFLAGS =  # Linker flag
LIBS =  # Any libraries you need to link
O = -O2 # Optimization level

all: shell

%.o: %.cc shell.hh
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(O) -o $@ -c $<

shell: shell.o helpers.o
	$(CXX) $(CXXFLAGS) $(O) -o $@ $^ $(LDFLAGS) $(LIBS)

sleep61: sleep61.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(O) -o $@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -f shell *.o *~ *.bak core *.core
	rm -rf out *.dSYM

.PHONY: all clean
