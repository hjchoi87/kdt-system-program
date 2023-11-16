TARGET = toy_system

SYSTEM = ./system
UI = ./ui
WEB_SERVER = ./web_server
HAL = ./hal

INCLUDES = -I$(SYSTEM) -I$(UI) -I$(WEB_SERVER) -I$(HAL) -I./

CC = gcc
CXXLIBS = -lpthread -lm -lrt
CXXFLAGS = $(INCLUDEDIRS) -g -O0 -std=c++14
CXX = g++

objects = main.o system_server.o web_server.o input.o gui.o
cxx_objects = camera_HAL.o ControlThread.o

$(TARGET): $(objects) $(cxx_objects)
	$(CXX) -o $(TARGET) $(objects) $(cxx_objects) $(CXXLIBS)

main.o:  main.c
	$(CC) -g $(INCLUDES) -c main.c

system_server.o: $(SYSTEM)/system_server.h $(SYSTEM)/system_server.c
	$(CC) -g $(INCLUDES) -c ./system/system_server.c

gui.o: $(UI)/gui.h $(UI)/gui.c
	$(CC) -g $(INCLUDES) -c $(UI)/gui.c

input.o: $(UI)/input.h $(UI)/input.c
	$(CC) -g $(INCLUDES) -c $(UI)/input.c

web_server.o: $(WEB_SERVER)/web_server.h $(WEB_SERVER)/web_server.c
	$(CC) -g $(INCLUDES) -c $(WEB_SERVER)/web_server.c

camera_HAL.o: $(HAL)/camera_HAL.cpp
	$(CXX) -g $(INCLUDES) $(CXXFLAGS) -c  $(HAL)/camera_HAL.cpp

ControlThread.o: $(HAL)/ControlThread.cpp
	$(CXX) -g $(INCLUDES) $(CXXFLAGS) -c  $(HAL)/ControlThread.cpp

.PHONY: clean
clean:
	rm -rf *.o
	rm -rf $(TARGET)
