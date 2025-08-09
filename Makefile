
SRCS = aifcplayer.cpp bitmap.cpp file.cpp engine.cpp graphics_soft.cpp \
	script.cpp mixer.cpp pak.cpp resource.cpp resource_mac.cpp resource_nth.cpp \
	resource_win31.cpp resource_3do.cpp scaler.cpp screenshot.cpp systemstub_sdl.cpp sfxplayer.cpp \
	staticres.cpp unpack.cpp util.cpp video.cpp main.cpp

SDL_CFLAGS = `sdl2-config --cflags`
SDL_LIBS = `sdl2-config --libs` -lSDL2_mixer

DEFINES = -DBYPASS_PROTECTION

ifndef NO_GL
	SRCS += graphics_gl.cpp
	SDL_LIBS += -lGL
	DEFINES += -DUSE_GL
endif

CXXFLAGS := -g -O -MMD -Wall -Wpedantic $(SDL_CFLAGS) $(DEFINES)
LIBS := -lz -lmt32emu
ifdef USE_LIBADLMIDI
	CXXFLAGS += -DUSE_LIBADLMIDI
	LIBS += -lADLMIDI
endif
ifeq "$(TARGET)" "pyra"
	CXXFLAGS += -O3 -DPYRA -march=armv7ve+simd -mcpu=cortex-a15 -mtune=cortex-a15 -mfpu=neon-vfpv4 -mfloat-abi=hard -mthumb
endif

OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)

rawgl: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(SDL_LIBS) $(LIBS)

clean:
	rm -f $(OBJS) $(DEPS)

-include $(DEPS)
