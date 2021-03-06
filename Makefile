IDIR=include
CC=g++ -std=c++20 -pthread
CFLAGS=-I$(IDIR)

ODIR =obj
SRCDIR =.

compile: client-phase$(PHASE).cpp
	$(CC) -o $(ODIR)/phase$(PHASE).out $^
	$(CC) -g -o $(ODIR)/phase.out $^

run:
	./$(ODIR)/phase$(PHASE).out ../sample-data/client$(CLIENT)-config.txt ../sample-data/files/client$(CLIENT)/

debug:
	gdb -q --args $(ODIR)/phase.out ../sample-data/client$(CLIENT)-config.txt ../sample-data/files/client$(CLIENT)/

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o $(ODIR)/*.out *~ core $(INCDIR)/*~