
/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 */

#ifndef SFXPLAYER_H__
#define SFXPLAYER_H__

#include "intern.h"

struct Resource;
struct SfxPlayer_impl;

struct SfxPlayer {
	SfxPlayer_impl *_impl;

	SfxPlayer();
	void init(Resource *res);
	void setSyncVar(int16_t *syncVar);

	void setEventsDelay(uint16_t delay);
	void loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos);
	void play(int rate);
	void readSamples(int16_t *buf, int len);
	void start();
	void stop();
};

#endif
