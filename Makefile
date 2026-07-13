.PHONY: all build run clean rebuild 
all: rebuild 

rebuild: clean build 

clean:
	rm -rf build 

build:
	cmake -S . -B build 
	cmake --build build 
run:
	./build/lume_server config/server.conf

