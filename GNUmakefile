# Compiler settings - using clang which is default on macOS
CXX = clang++
CXXFLAGS = -Wall -std=c++11 
LDFLAGS =  # Linker flag
O = -O2 # Optimization level

all: shell

%.o: %.cc shell.hh
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(O) -o $@ -c $<

shell: shell.o helpers.o
	$(CXX) $(CXXFLAGS) $(O) -o $@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -f shell *.o *~ *.bak core *.core
	rm -rf out *.dSYM

.PHONY: all clean
