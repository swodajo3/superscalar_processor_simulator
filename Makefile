SUBMIT = procsim.h procsim.c Makefile
CXXFLAGS := -g -Wall -std=c99 -lm
CXX=gcc

all: procsim

procsim: procsim.o 
	$(CXX) -o procsim procsim.o $(CXXFLAGS)

clean:
	rm -f procsim *.o

submit: clean
	tar zcvf swodajo3.tar.gz $(SUBMIT)