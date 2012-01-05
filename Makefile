test.out: main.o
	g++ -Wall -Werror -lboost_thread -lboost_system -lpthread -g -o http-hats.out main.o
main.o: main.cpp tunnel.hpp
	g++ -Wall -Werror -lboost_thread -lboost_system -lpthread -c main.cpp
clean:
	rm -f main.o http-hats.out
