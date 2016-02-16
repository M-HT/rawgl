#SDL_CFLAGS = `sdl-config --cflags`
#SDL_LIBS = `sdl-config --libs`

SDL_LIBS= -lSDL
DEFINES = -DSYS_LITTLE_ENDIAN

CXX = g++
CXXFLAGS:= -g -O -Wall -Wuninitialized -Wno-unknown-pragmas -Wshadow
CXXFLAGS+= -Wimplicit -Wundef -Wreorder -Wwrite-strings -Wnon-virtual-dtor -Wno-multichar
CXXFLAGS+= $(SDL_CFLAGS) $(DEFINES)

SRCS = bank.cpp file.cpp engine.cpp logic.cpp mixer.cpp resource.cpp sdlstub.cpp \
	serializer.cpp sfxplayer.cpp staticres.cpp util.cpp video.cpp main.cpp \

OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)

EXE = raw.exe

all: $(OBJS)
	$(CXX) $(LDFLAGS) -o $(EXE) $(OBJS) $(SDL_LIBS) -lz

.cpp.o:
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $*.o

clean:
	rm -f *.o *.d zlib/*.o zlib/*.a

-include $(DEPS)
