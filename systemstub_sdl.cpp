
/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include <SDL.h>
#include "graphics.h"
#include "systemstub.h"
#include "util.h"

struct SystemStub_SDL : SystemStub {

	static const int kJoystickIndex = 0;
	static const int kJoystickCommitValue = 16384;
	static const float kAspectRatio;

	int _w, _h;
	float _aspectRatio[4];
	SDL_Window *_window;
	SDL_Renderer *_renderer;
	SDL_GLContext _glcontext;
	int _texW, _texH, _texRedLow;
	SDL_Texture *_texture;
	SDL_Joystick *_joystick;
	SDL_GameController *_controller;
	int _screenshot;

	SystemStub_SDL();
	virtual ~SystemStub_SDL() {}

	virtual void init(const char *title, const DisplayMode *dm);
	virtual void fini();

	virtual void prepareScreen(int &w, int &h, float ar[4]);
	virtual void updateScreen();
	virtual bool createTexture(int w, int h);
	virtual void setScreenPixelsCLUT(const uint8_t *data, const uint8_t *pal, int w, int h);
	virtual void setScreenPixels555(const uint16_t *data, int w, int h);

	virtual void processEvents();
	virtual void sleep(uint32_t duration);
	virtual uint32_t getTimeStamp();

	void setAspectRatio(int w, int h);
};

const float SystemStub_SDL::kAspectRatio = 16.f / 10.f;

SystemStub_SDL::SystemStub_SDL()
	: _w(0), _h(0), _window(0), _renderer(0), _texW(0), _texH(0), _texture(0) {
}

void SystemStub_SDL::init(const char *title, const DisplayMode *dm) {
	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
	SDL_ShowCursor(SDL_DISABLE);
	// SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

	int windowW = 0;
	int windowH = 0;
	int flags = dm->opengl ? SDL_WINDOW_OPENGL : 0;
	if (dm->mode == DisplayMode::WINDOWED) {
		flags |= SDL_WINDOW_RESIZABLE;
		windowW = dm->width;
		windowH = dm->height;
	} else {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}
	_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowW, windowH, flags | SDL_WINDOW_HIDDEN);

	if (dm->opengl) {
		_glcontext = SDL_GL_CreateContext(_window);
	} else {
		_glcontext = 0;
#ifdef PYRA
		_renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_SOFTWARE);
#else
		_renderer = SDL_CreateRenderer(_window, -1, SDL_RENDERER_ACCELERATED);
#endif
		SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 255);
		SDL_RenderClear(_renderer);
	}
	SDL_ShowWindow(_window);
	SDL_GetWindowSize(_window, &_w, &_h);
	_aspectRatio[0] = _aspectRatio[1] = 0.;
	_aspectRatio[2] = _aspectRatio[3] = 1.;
	if (dm->mode == DisplayMode::FULLSCREEN_AR) {
		if (dm->opengl) {
			setAspectRatio(_w, _h);
		} else {
			SDL_RenderSetLogicalSize(_renderer, 320, 200);
		}
	}
	_joystick = 0;
	_controller = 0;
	if (SDL_NumJoysticks() > 0) {

#if SDL_COMPILEDVERSION >= SDL_VERSIONNUM(2,0,2)
		SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
#endif

#ifndef PYRA
		if (SDL_IsGameController(kJoystickIndex)) {
			_controller = SDL_GameControllerOpen(kJoystickIndex);
		}
#endif
		if (!_controller) {
			_joystick = SDL_JoystickOpen(kJoystickIndex);
		}
	}
	_screenshot = 1;
	_dm = *dm;
}

void SystemStub_SDL::fini() {
	if (_texture) {
		SDL_DestroyTexture(_texture);
		_texture = 0;
	}
	if (_joystick) {
		SDL_JoystickClose(_joystick);
		_joystick = 0;
	}
	if (_controller) {
		SDL_GameControllerClose(_controller);
		_controller = 0;
	}
	if (_renderer) {
		SDL_DestroyRenderer(_renderer);
		_renderer = 0;
	}
	if (_glcontext) {
		SDL_GL_DeleteContext(_glcontext);
		_glcontext = 0;
	}
	SDL_DestroyWindow(_window);
	SDL_Quit();
}

void SystemStub_SDL::prepareScreen(int &w, int &h, float ar[4]) {
	w = _w;
	h = _h;
	ar[0] = _aspectRatio[0];
	ar[1] = _aspectRatio[1];
	ar[2] = _aspectRatio[2];
	ar[3] = _aspectRatio[3];
	if (_renderer) {
		SDL_RenderClear(_renderer);
	}
}

void SystemStub_SDL::updateScreen() {
	if (_renderer) {
		SDL_RenderPresent(_renderer);
	} else {
		SDL_GL_SwapWindow(_window);
	}
}

bool SystemStub_SDL::createTexture(int w, int h) {
	Uint32 format = SDL_PIXELFORMAT_ABGR8888;
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(_renderer, &info) <= 0) {
		for (int i = 0; i < (int)info.num_texture_formats; ++i) {
			if (info.texture_formats[i] == SDL_PIXELFORMAT_ARGB8888 ||
			    info.texture_formats[i] == SDL_PIXELFORMAT_ABGR8888 ||
			    info.texture_formats[i] == SDL_PIXELFORMAT_RGB888 ||
			    info.texture_formats[i] == SDL_PIXELFORMAT_BGR888) {
				format = info.texture_formats[i];
				break;
			}
		}
	}
	_texture = SDL_CreateTexture(_renderer, format, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (!_texture) {
		return false;
	}
	_texW = w;
	_texH = h;
	_texRedLow = (format == SDL_PIXELFORMAT_ABGR8888 || format == SDL_PIXELFORMAT_BGR888);
	return true;
}

void SystemStub_SDL::setScreenPixelsCLUT(const uint8_t *data, const uint8_t *pal, int w, int h) {
	if (_renderer) {
		if (!_texture) {
			if (!createTexture(w, h)) return;
		}
		assert(w <= _texW && h <= _texH);
		SDL_Rect r;
		r.w = w;
		r.h = h;
		if (w != _texW && h != _texH) {
			r.x = (_texW - w) / 2;
			r.y = (_texH - h) / 2;
		} else {
			r.x = 0;
			r.y = 0;
		}
		uint32_t clut[16];
		if (_texRedLow) {
			for (int i = 0; i < 16; ++i) {
				clut[i] = pal[3 * i] | (pal[3 * i + 1] << 8) | (pal[3 * i + 2] << 16);
			}
		} else {
			for (int i = 0; i < 16; ++i) {
				clut[i] = pal[3 * i + 2] | (pal[3 * i + 1] << 8) | (pal[3 * i] << 16);
			}
		}
		uint32_t *dst;
		int pitch;
		if (!SDL_LockTexture(_texture, &r, (void **)&dst, &pitch)) {
			for (int i = 0; i < h; ++i) {
				for (int j = 0; j < w; ++j) {
					dst[j] = clut[*data++];
				}
				dst = (uint32_t *)(pitch + (uintptr_t)dst);
			}
			SDL_UnlockTexture(_texture);
		}
		SDL_RenderCopy(_renderer, _texture, 0, 0);
	}
}

void SystemStub_SDL::setScreenPixels555(const uint16_t *data, int w, int h) {
	if (_renderer) {
		if (!_texture) {
			if (!createTexture(w, h)) return;
		}
		assert(w <= _texW && h <= _texH);
		SDL_Rect r;
		r.w = w;
		r.h = h;
		if (w != _texW && h != _texH) {
			r.x = (_texW - w) / 2;
			r.y = (_texH - h) / 2;
		} else {
			r.x = 0;
			r.y = 0;
		}
		uint32_t *dst;
		int pitch;
		if (!SDL_LockTexture(_texture, &r, (void **)&dst, &pitch)) {
			if (_texRedLow) {
				for (int i = 0; i < h; ++i) {
					for (int j = 0; j < w; ++j) {
						const uint16_t color = *data++;
						dst[j] = ((color & 0x001F) << 19) | ((color & 0x001C) << 14) |
						         ((color & 0x03E0) <<  6) | ((color & 0x0380) <<  1) |
						         ((color & 0x7C00) >>  7) | ((color & 0x7000) >> 12);
					}
					dst = (uint32_t *)(pitch + (uintptr_t)dst);
				}
			} else {
				for (int i = 0; i < h; ++i) {
					for (int j = 0; j < w; ++j) {
						const uint16_t color = *data++;
						dst[j] = ((color & 0x001F) << 3) | ((color & 0x001C) >> 2) |
						         ((color & 0x03E0) << 6) | ((color & 0x0380) << 1) |
						         ((color & 0x7C00) << 9) | ((color & 0x7000) << 4);
					}
					dst = (uint32_t *)(pitch + (uintptr_t)dst);
				}
			}
			SDL_UnlockTexture(_texture);
		}
		SDL_RenderCopy(_renderer, _texture, 0, 0);
	}
}

void SystemStub_SDL::processEvents() {
	SDL_Event ev;
	while(SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			_pi.quit = true;
			break;
		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
				_w = _dm.width  = ev.window.data1;
				_h = _dm.height = ev.window.data2;
			} else if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
				_pi.quit = true;
			}
			break;
		case SDL_KEYUP:
			switch (ev.key.keysym.sym) {
			case SDLK_LEFT:
				_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				break;
			case SDLK_RIGHT:
				_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				break;
			case SDLK_UP:
				_pi.dirMask &= ~PlayerInput::DIR_UP;
				break;
			case SDLK_DOWN:
				_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				break;
			case SDLK_SPACE:
			case SDLK_RETURN:
#ifdef PYRA
			case SDLK_HOME:
			case SDLK_PAGEUP:
			case SDLK_END:
			case SDLK_PAGEDOWN:
#endif
				_pi.action = false;
				break;
			case SDLK_LSHIFT:
			case SDLK_RSHIFT:
				_pi.jump = false;
				break;
			case SDLK_s:
				_pi.screenshot = true;
				break;
			case SDLK_c:
				_pi.code = true;
				break;
			case SDLK_p:
				_pi.pause = true;
				break;
			case SDLK_ESCAPE:
			case SDLK_AC_BACK:
				_pi.back = true;
				break;
			case SDLK_AC_HOME:
				_pi.quit = true;
				break;
			default:
				break;
			}
			break;
		case SDL_KEYDOWN:
			if (ev.key.keysym.mod & KMOD_ALT) {
				if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER) {
				} else if (ev.key.keysym.sym == SDLK_x) {
					_pi.quit = true;
				}
				break;
			} else if (ev.key.keysym.mod & KMOD_CTRL) {
				if (ev.key.keysym.sym == SDLK_f) {
					_pi.fastMode = true;
				}
				break;
			}
			if (ev.key.keysym.sym < 128) {
				_pi.lastChar = ev.key.keysym.sym;
			}
			switch(ev.key.keysym.sym) {
			case SDLK_LEFT:
				_pi.dirMask |= PlayerInput::DIR_LEFT;
				break;
			case SDLK_RIGHT:
				_pi.dirMask |= PlayerInput::DIR_RIGHT;
				break;
			case SDLK_UP:
				_pi.dirMask |= PlayerInput::DIR_UP;
				break;
			case SDLK_DOWN:
				_pi.dirMask |= PlayerInput::DIR_DOWN;
				break;
			case SDLK_SPACE:
			case SDLK_RETURN:
#ifdef PYRA
			case SDLK_HOME:
			case SDLK_PAGEUP:
			case SDLK_END:
			case SDLK_PAGEDOWN:
#endif
				_pi.action = true;
				break;
			case SDLK_LSHIFT:
			case SDLK_RSHIFT:
				_pi.jump = true;
				break;
			default:
				break;
			}
			break;
#ifndef PYRA
		case SDL_JOYHATMOTION:
			if (_joystick) {
				_pi.dirMask = 0;
				if (ev.jhat.value & SDL_HAT_UP) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				}
				if (ev.jhat.value & SDL_HAT_DOWN) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				}
				if (ev.jhat.value & SDL_HAT_LEFT) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				}
				if (ev.jhat.value & SDL_HAT_RIGHT) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				}
			}
			break;
		case SDL_JOYAXISMOTION:
			if (_joystick) {
				switch (ev.jaxis.axis) {
				case 0:
					_pi.dirMask &= ~(PlayerInput::DIR_RIGHT | PlayerInput::DIR_LEFT);
					if (ev.jaxis.value > kJoystickCommitValue) {
						_pi.dirMask |= PlayerInput::DIR_RIGHT;
					} else if (ev.jaxis.value < -kJoystickCommitValue) {
						_pi.dirMask |= PlayerInput::DIR_LEFT;
					}
					break;
				case 1:
					_pi.dirMask &= ~(PlayerInput::DIR_UP | PlayerInput::DIR_DOWN);
					if (ev.jaxis.value > kJoystickCommitValue) {
						_pi.dirMask |= PlayerInput::DIR_DOWN;
					} else if (ev.jaxis.value < -kJoystickCommitValue) {
						_pi.dirMask |= PlayerInput::DIR_UP;
					}
					break;
				}
			}
			break;
#endif
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			if (_joystick) {
#ifdef PYRA
				if (ev.jbutton.button == 8) {
					_pi.jump = (ev.jbutton.state == SDL_PRESSED);
				}
#else
				_pi.action = (ev.jbutton.state == SDL_PRESSED);
#endif
			}
			break;
#ifndef PYRA
		case SDL_CONTROLLERAXISMOTION:
			if (_controller) {
				switch (ev.caxis.axis) {
				case SDL_CONTROLLER_AXIS_LEFTX:
				case SDL_CONTROLLER_AXIS_RIGHTX:
					if (ev.caxis.value < -kJoystickCommitValue) {
						_pi.dirMask |= PlayerInput::DIR_LEFT;
					} else {
						_pi.dirMask &= ~PlayerInput::DIR_LEFT;
					}
					if (ev.caxis.value > kJoystickCommitValue) {
						_pi.dirMask |= PlayerInput::DIR_RIGHT;
					} else {
						_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
					}
					break;
				case SDL_CONTROLLER_AXIS_LEFTY:
				case SDL_CONTROLLER_AXIS_RIGHTY:
					if (ev.caxis.value < -kJoystickCommitValue) {
						_pi.dirMask |= PlayerInput::DIR_UP;
					} else {
						_pi.dirMask &= ~PlayerInput::DIR_UP;
					}
					if (ev.caxis.value > kJoystickCommitValue) {
						_pi.dirMask |= PlayerInput::DIR_DOWN;
					} else {
						_pi.dirMask &= ~PlayerInput::DIR_DOWN;
					}
					break;
				}
			}
			break;
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
			if (_controller) {
				const bool pressed = (ev.cbutton.state == SDL_PRESSED);
				switch (ev.cbutton.button) {
				case SDL_CONTROLLER_BUTTON_BACK:
					_pi.back = pressed;
					break;
				case SDL_CONTROLLER_BUTTON_GUIDE:
					_pi.code = pressed;
					break;
				case SDL_CONTROLLER_BUTTON_START:
					_pi.pause = pressed;
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_UP:
					if (pressed) {
						_pi.dirMask |= PlayerInput::DIR_UP;
					} else {
						_pi.dirMask &= ~PlayerInput::DIR_UP;
					}
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
					if (pressed) {
						_pi.dirMask |= PlayerInput::DIR_DOWN;
					} else {
						_pi.dirMask &= ~PlayerInput::DIR_DOWN;
					}
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
					if (pressed) {
						_pi.dirMask |= PlayerInput::DIR_LEFT;
					} else {
						_pi.dirMask &= ~PlayerInput::DIR_LEFT;
					}
					break;
				case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
					if (pressed) {
						_pi.dirMask |= PlayerInput::DIR_RIGHT;
					} else {
						_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
					}
					break;
				case SDL_CONTROLLER_BUTTON_A:
					_pi.action = pressed;
					break;
				case SDL_CONTROLLER_BUTTON_B:
					_pi.jump = pressed;
					break;
				}
			}
			break;
#endif
		default:
			break;
		}
	}
}

void SystemStub_SDL::sleep(uint32_t duration) {
	SDL_Delay(duration);
}

uint32_t SystemStub_SDL::getTimeStamp() {
	return SDL_GetTicks();
}

void SystemStub_SDL::setAspectRatio(int w, int h) {
	const float currentAspectRatio = w / (float)h;
	if (int(currentAspectRatio * 100) == int(kAspectRatio * 100)) {
		_aspectRatio[0] = 0.f;
		_aspectRatio[1] = 0.f;
		_aspectRatio[2] = 1.f;
		_aspectRatio[3] = 1.f;
		return;
	}
	// pillar box
	if (currentAspectRatio > kAspectRatio) {
		const float inset = 1.f - kAspectRatio / currentAspectRatio;
		_aspectRatio[0] = inset / 2;
		_aspectRatio[1] = 0.f;
		_aspectRatio[2] = 1.f - inset;
		_aspectRatio[3] = 1.f;
		return;
	}
	// letter box
	if (currentAspectRatio < kAspectRatio) {
		const float inset = 1.f - currentAspectRatio / kAspectRatio;
		_aspectRatio[0] = 0.f;
		_aspectRatio[1] = inset / 2;
		_aspectRatio[2] = 1.f;
		_aspectRatio[3] = 1.f - inset;
		return;
	}
}

SystemStub *SystemStub_SDL_create() {
	return new SystemStub_SDL();
}
