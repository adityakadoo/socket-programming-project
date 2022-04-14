compile: client-phase1.cpp client-phase2.cpp client-phase3.cpp client-phase4.cpp client-phase5.cpp
	g++ -std=c++20 -pthread -o client-phase1 client-phase1.cpp
	g++ -std=c++20 -pthread -o client-phase2 client-phase2.cpp
	g++ -std=c++20 -pthread -o client-phase3 client-phase3.cpp
	g++ -std=c++20 -pthread -o client-phase4 client-phase4.cpp
	g++ -std=c++20 -pthread -o client-phase5 client-phase5.cpp


.PHONY: clean

clean:
	rm -f client-phase[1-5]
	rm -rf output