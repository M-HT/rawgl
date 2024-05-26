
/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include "sfxplayer.h"
#include "mixer.h"
#include "mixer_platform.h"
#include "resource.h"
#include "systemstub.h"
#include "util.h"
#include <math.h>

struct SfxPlayer_impl {
	virtual void setSyncVar(int16_t *syncVar) = 0;
	virtual void setEventsDelay(uint16_t delay) = 0;
	virtual void loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos) = 0;
	virtual void play(int rate) = 0;
	virtual void readSamples(int16_t *buf, int len) = 0;
	virtual void start() = 0;
	virtual void stop() = 0;

	static SfxPlayer_impl *create(Resource *res);
};

SfxPlayer::SfxPlayer()
	: _impl(0) {
}

void SfxPlayer::init(Resource *res) {
	_impl = SfxPlayer_impl::create(res);
}

void SfxPlayer::setSyncVar(int16_t *syncVar) {
	if (_impl) {
		return _impl->setSyncVar(syncVar);
	}
}

void SfxPlayer::setEventsDelay(uint16_t delay) {
	debug(DBG_SND, "SfxPlayer::setEventsDelay(%d)", delay);
	if (_impl) {
		return _impl->setEventsDelay(delay);
	}
}

void SfxPlayer::loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos) {
	debug(DBG_SND, "SfxPlayer::loadSfxModule(0x%X, %d, %d)", resNum, delay, pos);
	if (_impl) {
		return _impl->loadSfxModule(resNum, delay, pos);
	}
}

void SfxPlayer::play(int rate) {
	if (_impl) {
		return _impl->play(rate);
	}
}

void SfxPlayer::readSamples(int16_t *buf, int len) {
	if (_impl) {
		return _impl->readSamples(buf, len);
	}
}

void SfxPlayer::start() {
	debug(DBG_SND, "SfxPlayer::start()");
	if (_impl) {
		return _impl->start();
	}
}

void SfxPlayer::stop() {
	debug(DBG_SND, "SfxPlayer::stop()");
	if (_impl) {
		return _impl->stop();
	}
}

struct SfxInstrument {
	uint8_t *data;
	uint16_t volume;
};

struct SfxModule {
	const uint8_t *data;
	uint16_t curPos;
	uint8_t curOrder;
	uint8_t numOrder;
	uint8_t *orderTable;
	SfxInstrument samples[15];
};

struct SfxPattern {
	uint16_t note_1;
	uint16_t note_2;
	uint16_t sampleStart;
	uint8_t *sampleBuffer;
	uint16_t sampleLen;
	uint16_t loopPos;
	uint16_t loopLen;
	uint16_t sampleVolume;
};

struct SfxChannel {
	uint8_t *sampleData;
	uint16_t sampleLen;
	uint16_t sampleLoopPos;
	uint16_t sampleLoopLen;
	uint16_t volume;
	Frac pos;
};

struct ModulePlayer: SfxPlayer_impl {
	enum {
		NUM_CHANNELS = 4
	};

	Resource *_res;

	uint16_t _delay;
	uint16_t _resNum;
	SfxModule _sfxMod;
	int16_t *_syncVar;
	bool _playing;
	int _rate;
	int _samplesLeft;
	SfxChannel _channels[NUM_CHANNELS];

	ModulePlayer(Resource *res);

	virtual void setSyncVar(int16_t *syncVar);
	virtual void setEventsDelay(uint16_t delay);
	virtual void loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos);
	void prepareInstruments(const uint8_t *p);
	virtual void play(int rate);
	void mixSamples(int16_t *buf, int len);
	virtual void readSamples(int16_t *buf, int len);
	virtual void start();
	virtual void stop();
	void handleEvents();
	void handlePattern(uint8_t channel, const uint8_t *patternData);
};

ModulePlayer::ModulePlayer(Resource *res)
	: _res(res), _delay(0) {
	_playing = false;
}

void ModulePlayer::setSyncVar(int16_t *syncVar) {
	_syncVar = syncVar;
}

void ModulePlayer::setEventsDelay(uint16_t delay) {
	_delay = delay;
}

void ModulePlayer::loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos) {
	MemEntry *me = &_res->_memList[resNum];
	if (me->status == Resource::STATUS_LOADED && me->type == Resource::RT_MUSIC) {
		memset(&_sfxMod, 0, sizeof(SfxModule));
		_sfxMod.curOrder = pos;
		_sfxMod.numOrder = me->bufPtr[0x3F];
		debug(DBG_SND, "ModulePlayer::loadSfxModule() curOrder = 0x%X numOrder = 0x%X", _sfxMod.curOrder, _sfxMod.numOrder);
		_sfxMod.orderTable = me->bufPtr + 0x40;
		if (delay == 0) {
			_delay = READ_BE_UINT16(me->bufPtr);
		} else {
			_delay = delay;
		}
		_sfxMod.data = me->bufPtr + 0xC0;
		debug(DBG_SND, "ModulePlayer::loadSfxModule() eventDelay = %d ms", _delay);
		prepareInstruments(me->bufPtr + 2);
	} else {
		warning("ModulePlayer::loadSfxModule() ec=0x%X", 0xF8);
	}
}

void ModulePlayer::prepareInstruments(const uint8_t *p) {
	memset(_sfxMod.samples, 0, sizeof(_sfxMod.samples));
	for (int i = 0; i < 15; ++i) {
		SfxInstrument *ins = &_sfxMod.samples[i];
		uint16_t resNum = READ_BE_UINT16(p); p += 2;
		if (resNum != 0) {
			ins->volume = READ_BE_UINT16(p);
			MemEntry *me = &_res->_memList[resNum];
			if (me->status == Resource::STATUS_LOADED && me->type == Resource::RT_SOUND) {
				ins->data = me->bufPtr;
				debug(DBG_SND, "Loaded instrument 0x%X n=%d volume=%d", resNum, i, ins->volume);
			} else {
				error("Error loading instrument 0x%X", resNum);
			}
		}
		p += 2; // skip volume
	}
}

void ModulePlayer::play(int rate) {
	_playing = true;
	_rate = rate;
	_samplesLeft = 0;
	memset(_channels, 0, sizeof(_channels));
}

static int16_t toS16(int a) {
	if (a <= -128) {
		return -32768;
	} else if (a >= 127) {
		return 32767;
	} else {
		const uint8_t u8 = (a ^ 0x80);
		return ((u8 << 8) | u8) - 32768;
	}
}

static void mixChannel(int16_t &s, SfxChannel *ch) {
	if (ch->sampleLen == 0) {
		return;
	}
	int pos1 = ch->pos.offset >> Frac::BITS;
	ch->pos.offset += ch->pos.inc;
	int pos2 = pos1 + 1;
	if (ch->sampleLoopLen != 0) {
		if (pos1 >= ch->sampleLoopPos + ch->sampleLoopLen - 1) {
			pos2 = ch->sampleLoopPos;
			ch->pos.offset = pos2 << Frac::BITS;
		}
	} else {
		if (pos1 >= ch->sampleLen - 1) {
			ch->sampleLen = 0;
			return;
		}
	}
	int sample = ch->pos.interpolate((int8_t)ch->sampleData[pos1], (int8_t)ch->sampleData[pos2]);
	s = mixS16(s, toS16(sample * ch->volume / 64));
}

void ModulePlayer::mixSamples(int16_t *buf, int len) {
	while (len != 0) {
		if (_samplesLeft == 0) {
			handleEvents();
			const int samplesPerTick = _rate * (_delay * 60 * 1000 / kPaulaFreq) / 1000;
			_samplesLeft = samplesPerTick;
		}
		int count = _samplesLeft;
		if (count > len) {
			count = len;
		}
		_samplesLeft -= count;
		len -= count;
		for (int i = 0; i < count; ++i) {
			mixChannel(*buf, &_channels[0]);
			mixChannel(*buf, &_channels[3]);
			++buf;
			mixChannel(*buf, &_channels[1]);
			mixChannel(*buf, &_channels[2]);
			++buf;
		}
	}
}

void ModulePlayer::readSamples(int16_t *buf, int len) {
	if (_delay != 0) {
		mixSamples(buf, len / 2);
	}
}

void ModulePlayer::start() {
	_sfxMod.curPos = 0;
}

void ModulePlayer::stop() {
	_playing = false;
}

void ModulePlayer::handleEvents() {
	uint8_t order = _sfxMod.orderTable[_sfxMod.curOrder];
	const uint8_t *patternData = _sfxMod.data + _sfxMod.curPos + order * 1024;
	for (uint8_t ch = 0; ch < 4; ++ch) {
		handlePattern(ch, patternData);
		patternData += 4;
	}
	_sfxMod.curPos += 4 * 4;
	debug(DBG_SND, "ModulePlayer::handleEvents() order = 0x%X curPos = 0x%X", order, _sfxMod.curPos);
	if (_sfxMod.curPos >= 1024) {
		_sfxMod.curPos = 0;
		order = _sfxMod.curOrder + 1;
		if (order == _sfxMod.numOrder) {
			_playing = false;
		}
		_sfxMod.curOrder = order;
	}
}

void ModulePlayer::handlePattern(uint8_t channel, const uint8_t *data) {
	SfxPattern pat;
	memset(&pat, 0, sizeof(SfxPattern));
	pat.note_1 = READ_BE_UINT16(data + 0);
	pat.note_2 = READ_BE_UINT16(data + 2);
	if (pat.note_1 != 0xFFFD) {
		uint16_t sample = (pat.note_2 & 0xF000) >> 12;
		if (sample != 0) {
			uint8_t *ptr = _sfxMod.samples[sample - 1].data;
			if (ptr != 0) {
				debug(DBG_SND, "ModulePlayer::handlePattern() preparing sample %d", sample);
				pat.sampleVolume = _sfxMod.samples[sample - 1].volume;
				pat.sampleStart = 8;
				pat.sampleBuffer = ptr;
				pat.sampleLen = READ_BE_UINT16(ptr) * 2;
				uint16_t loopLen = READ_BE_UINT16(ptr + 2) * 2;
				if (loopLen != 0) {
					pat.loopPos = pat.sampleLen;
					pat.loopLen = loopLen;
				} else {
					pat.loopPos = 0;
					pat.loopLen = 0;
				}
				int16_t m = pat.sampleVolume;
				uint8_t effect = (pat.note_2 & 0x0F00) >> 8;
				if (effect == 5) { // volume up
					uint8_t volume = (pat.note_2 & 0xFF);
					m += volume;
					if (m > 0x3F) {
						m = 0x3F;
					}
				} else if (effect == 6) { // volume down
					uint8_t volume = (pat.note_2 & 0xFF);
					m -= volume;
					if (m < 0) {
						m = 0;
					}
				}
				_channels[channel].volume = m;
				pat.sampleVolume = m;
			}
		}
	}
	if (pat.note_1 == 0xFFFD) {
		debug(DBG_SND, "ModulePlayer::handlePattern() _syncVar = 0x%X", pat.note_2);
		*_syncVar = pat.note_2;
	} else if (pat.note_1 == 0xFFFE) {
		_channels[channel].sampleLen = 0;
	} else if (pat.note_1 != 0 && pat.sampleBuffer != 0) {
		assert(pat.note_1 >= 0x37 && pat.note_1 < 0x1000);
		// convert Amiga period value to hz
		const int freq = kPaulaFreq / (pat.note_1 * 2);
		debug(DBG_SND, "ModulePlayer::handlePattern() adding sample freq = 0x%X", freq);
		SfxChannel *ch = &_channels[channel];
		ch->sampleData = pat.sampleBuffer + pat.sampleStart;
		ch->sampleLen = pat.sampleLen;
		ch->sampleLoopPos = pat.loopPos;
		ch->sampleLoopLen = pat.loopLen;
		ch->volume = pat.sampleVolume;
		ch->pos.offset = 0;
		ch->pos.inc = (freq << Frac::BITS) / _rate;
	}
}

struct MidiTrack {
	const uint8_t *data;
	unsigned int length;
	unsigned int delta;
	uint8_t status;
};

struct MidiInstrument {
	const uint8_t *data;
	uint32_t offset;
	int32_t length;
	uint32_t rate; // 16.16 fixed point
	int32_t loopStart;
	int32_t loopEnd;
	uint8_t note;
	bool loaded;
};

struct MidiFile {
	const uint8_t *data;
	unsigned int numberOfTracks;
	unsigned int timeDivision;
	unsigned int tempo;
	MidiTrack *tracks;
	MidiInstrument instruments[128];
};

struct MidiChannel {
	const uint8_t *sampleData;
	int32_t sampleLen;
	int32_t sampleLoopPos;
	int32_t sampleLoopLen;
	int32_t channelVolume;
	int32_t noteVolume;
	Frac pos;
	uint8_t instrument;
};

struct MidiPlayer: SfxPlayer_impl {
	enum {
		NUM_CHANNELS = 4
	};
	enum {
		TYPE_MThd = 0x4D546864,
		TYPE_MTrk = 0x4D54726B
	};

	Resource *_res;

	MidiFile _midiFile;
	bool _playing;
	int _rate;
	int _samplesLeft;
	MidiChannel _channels[NUM_CHANNELS];

	MidiPlayer(Resource *res);

	virtual void setSyncVar(int16_t *syncVar);
	virtual void setEventsDelay(uint16_t delay);
	virtual void loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos);
	void readMidi(const uint8_t *p, uint32_t offset);
	void prepareInstruments();
	virtual void play(int rate);
	void mixSamples(int16_t *buf, int len);
	virtual void readSamples(int16_t *buf, int len);
	virtual void start();
	virtual void stop();
	void handleEvents();
	//void handlePattern(uint8_t channel, const uint8_t *patternData);
};

MidiPlayer::MidiPlayer(Resource *res)
	: _res(res) {
	_playing = false;
	memset(&_midiFile, 0, sizeof(MidiFile));
}

void MidiPlayer::setSyncVar(int16_t *syncVar) {
}

void MidiPlayer::setEventsDelay(uint16_t delay) {
}

void MidiPlayer::loadSfxModule(uint16_t resNum, uint16_t delay, uint8_t pos) {
	stop();

	uint32_t offset;
	const char *p = _res->getMusicPath(resNum, 0, 0, &offset);
	if (p) {
		readMidi((const uint8_t *)p, offset);
		prepareInstruments();
	}
}

void MidiPlayer::readMidi(const uint8_t *p, uint32_t offset) {
	memset(&_midiFile, 0, sizeof(MidiFile));
	_midiFile.data = p;
	p += offset;

	uint32_t mthdMagic = READ_BE_UINT32(p);
	if (mthdMagic != TYPE_MThd) return;
	uint32_t mthdLength = READ_BE_UINT32(p + 4);
	if (mthdLength != 6) return;
	uint16_t formatType = READ_BE_UINT16(p + 8);
	if (formatType != 0 && formatType != 1) return;
	uint16_t numberOfTracks = READ_BE_UINT16(p + 10);
	if ((numberOfTracks == 0) || (formatType == 0 && numberOfTracks != 1)) return;
	uint16_t timeDivision = READ_BE_UINT16(p + 12);
	if (timeDivision & 0x8000) {
		if (((timeDivision & 0x7F00) == 0) || ((timeDivision & 0xFF) == 0)) return;
	} else {
		if (timeDivision == 0) return;
	}
	p += 14;

	_midiFile.tracks = (MidiTrack *)malloc(numberOfTracks * sizeof(MidiTrack));
	if (!_midiFile.tracks) return;

	for (int i = 0; i < numberOfTracks; ++i) {
		uint32_t mtrkMagic = READ_BE_UINT32(p);
		if (mtrkMagic != TYPE_MTrk) return;
		uint32_t mtrkLength = READ_BE_UINT32(p + 4);
		p += 8;
		_midiFile.tracks[i].data = p;
		_midiFile.tracks[i].length = mtrkLength;
		p += mtrkLength;
	}

	_midiFile.numberOfTracks = numberOfTracks;
	_midiFile.timeDivision = timeDivision;
	_midiFile.tempo = 500000;

	debug(DBG_SND, "MidiPlayer::readMidi() formatType = %i numberOfTracks = %i timeDivision = 0x%X", formatType, numberOfTracks, timeDivision);
}

static unsigned int readVariableLength(const uint8_t *&p, unsigned int &length) {
	unsigned int varLength = 0;
	uint8_t data;

	if (length) {
		do {
			data = *p++;
			--length;
			varLength = (varLength << 7) | (data & 0x7f);
		} while (data & 0x80 && length);
	}

	return (length) ? varLength : 0;
}

void MidiPlayer::prepareInstruments() {
	for (unsigned int i = 0; i < _midiFile.numberOfTracks; ++i) {
		const uint8_t *p = _midiFile.tracks[i].data;
		unsigned int length = _midiFile.tracks[i].length, varLength;
		uint8_t status = 0;
		while (length) {
			readVariableLength(p, length);
			if (*p & 0x80 && length) {
				status = *p++;
				--length;
			}
			switch (status >> 4) {
			case 0x08: // Note off
			case 0x09: // Note on
			case 0x0A: // Pressure change
			case 0x0B: // Controller change
			case 0x0E: // Pitch bend
				if (length >= 2) {
					p += 2;
					length -= 2;
				} else length = 0;
				break;
			case 0x0C: // Program change
				if (length) {
					uint8_t num = *p++;
					--length;
					if (!_midiFile.instruments[num].loaded) {
						_midiFile.instruments[num].loaded = true;
						uint32_t offset;
						const uint8_t *p2 = _res->getInstrument(num, &offset);
						if (p2) {
							MidiInstrument *instrument = &_midiFile.instruments[num];
							instrument->data = p2;
							instrument->offset = offset + 42;
							instrument->length = READ_BE_UINT32(p2 + offset + 24);
							instrument->rate = 0x56EE8BA3 /*READ_BE_UINT32(p2 + offset + 28)*/; // 16.16 fixed point
							instrument->loopStart = READ_BE_UINT32(p2 + offset + 32);
							instrument->loopEnd = READ_BE_UINT32(p2 + offset + 36);
							instrument->note = p2[offset + 41];
							debug(DBG_SND, "Loaded instrument 0x%X", num);
						} else {
							error("Error loading instrument 0x%X", num);
						}
					}
				}
				break;
			case 0x0D: // Channel press
				if (length) {
					++p;
					--length;
				}
				break;
			case 0x0F: // System Common / RealTime messages / Meta events
				switch (status & 0x0F) {
				case 0x00: // Sysex start
				case 0x07: // Sysex end
					varLength = readVariableLength(p, length);
					if (varLength <= length) {
						p += varLength;
						length -= varLength;
					} else length = 0;
					break;
				case 0x01: // MTC Quarter Frame package
				case 0x03: // Song Select
				case 0x04: // Undefined/Unknown
				case 0x05: // Port Select (non-standard)
					if (length) {
						++p;
						--length;
					}
					break;
				case 0x2: // Song Position
					if (length >= 2) {
						p += 2;
						length -= 2;
					} else length = 0;
					break;
				case 0x0F: // Meta event
					if (length) {
						if (*p == 0x2F) { // End of track
							length = 0;
						} else {
							++p;
							--length;
							varLength = readVariableLength(p, length);
							if (varLength <= length) {
								p += varLength;
								length -= varLength;
							} else length = 0;
						}
					}
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		}
	}
}

void MidiPlayer::play(int rate) {
	_playing = (_midiFile.numberOfTracks != 0);
	_rate = rate;
	_samplesLeft = 0;
	memset(_channels, 0, sizeof(_channels));
	for (unsigned int i = 0; i < NUM_CHANNELS; ++i) {
		_channels[i].channelVolume = 100;
	}
}

static int16_t U8toS16(uint8_t a) {
	return ((a << 8) | a) - 32768;
}

static int getChannelSample(MidiChannel *ch) {
	if (ch->noteVolume == 0 || ch->sampleLen == 0) {
		return 0;
	}
	int pos1 = ch->pos.offset >> Frac::BITS;
	ch->pos.offset += ch->pos.inc;
	int pos2 = pos1 + 1;
	if (ch->sampleLoopLen != 0) {
		if (pos1 >= ch->sampleLoopPos + ch->sampleLoopLen - 1) {
			pos2 = ch->sampleLoopPos;
			ch->pos.offset = pos2 << Frac::BITS;
		}
	} else {
		if (pos1 >= ch->sampleLen - 1) {
			ch->sampleLen = 0;
			return 0;
		}
	}
	int sample = ch->pos.interpolate(U8toS16(ch->sampleData[pos1]), U8toS16(ch->sampleData[pos2]));
	return (sample * ch->noteVolume / 128) * ch->channelVolume / 128;
}

void MidiPlayer::mixSamples(int16_t *buf, int len) {
	while (len != 0) {
		if (_samplesLeft == 0) {
			handleEvents();
			const int samplesPerTick = (_rate * (int64_t)_midiFile.tempo) / (1000000 * (int64_t)_midiFile.timeDivision);
			_samplesLeft = samplesPerTick;
		}
		int count = _samplesLeft;
		if (count > len) {
			count = len;
		}
		_samplesLeft -= count;
		len -= count;
		for (int i = 0; i < count; ++i) {
			int sample = 0;
			for (int j = 0; j < NUM_CHANNELS; ++j) {
				sample += getChannelSample(&_channels[j]);
			}
			buf[0] = mixS16(buf[0], sample);
			buf[1] = mixS16(buf[1], sample);
			buf += 2;
		}
	}
}

void MidiPlayer::readSamples(int16_t *buf, int len) {
	if (_playing) {
		mixSamples(buf, len / 2);
	}
}

void MidiPlayer::start() {
	for (unsigned int i = 0; i < _midiFile.numberOfTracks; ++i) {
		_midiFile.tracks[i].delta = readVariableLength(_midiFile.tracks[i].data, _midiFile.tracks[i].length);
		_midiFile.tracks[i].status = 0;
	}
}

void MidiPlayer::stop() {
	_playing = false;
	_midiFile.numberOfTracks = 0;
	if (_midiFile.data) {
		free((void *)_midiFile.data);
		_midiFile.data = 0;
	}
	if (_midiFile.tracks) {
		free(_midiFile.tracks);
		_midiFile.tracks = 0;
	}
	for (int i = 0; i < 128; ++i) {
		if (_midiFile.instruments[i].data) {
			free((void *)_midiFile.instruments[i].data);
			_midiFile.instruments[i].data = 0;
		}
	}
}

void MidiPlayer::handleEvents() {
	for (unsigned int i = 0; i < _midiFile.numberOfTracks; ++i) {
		MidiTrack *track = &_midiFile.tracks[i];
		while (track->length && !track->delta) {
			unsigned int varLength;
			if (*track->data & 0x80) {
				track->status = *track->data++;
				--track->length;
			}
			switch (track->status >> 4) {
			case 0x08: // Note off
				_channels[(track->status & 0x0F) - 1].noteVolume = 0;
				if (track->length >= 2) {
					track->data += 2;
					track->length -= 2;
				} else track->length = 0;
				break;
			case 0x09: // Note on
				if (track->length >= 2) {
					MidiChannel *channel = &_channels[(track->status & 0x0F) - 1];
					if (channel->sampleData) {
						channel->noteVolume = track->data[1];
						if (channel->noteVolume) {
							MidiInstrument *instrument = &_midiFile.instruments[channel->instrument];
							if (track->data[0] == instrument->note) {
								channel->pos.reset(instrument->rate >> 8, _rate << 8);
							} else {
								static const double twelfthRootTwo = 1.05946309434;
								double newRate = pow(twelfthRootTwo, track->data[0] - instrument->note) * (instrument->rate / 65536.0);
								channel->pos.reset((int)(newRate * 256.0), _rate << 8);
							}
						}
					}
					track->data += 2;
					track->length -= 2;
				} else track->length = 0;
				break;
			case 0x0A: // Pressure change
			case 0x0B: // Controller change
			case 0x0E: // Pitch bend
				if (track->length >= 2) {
					track->data += 2;
					track->length -= 2;
				} else track->length = 0;
				break;
			case 0x0C: // Program change
				if (track->length) {
					uint8_t num = *track->data++;
					--track->length;
					MidiChannel *channel = &_channels[(track->status & 0x0F) - 1];
					if (channel->instrument != num) {
						MidiInstrument *instrument = &_midiFile.instruments[num];
						channel->sampleData = instrument->data + instrument->offset;
						channel->sampleLen = instrument->length;
						channel->sampleLoopPos = instrument->loopStart;
						channel->sampleLoopLen = (instrument->loopStart >= 0 && instrument->loopEnd <= instrument->length && instrument->loopStart + 1 != instrument->loopEnd) ? instrument->loopEnd - instrument->loopStart : 0;
						channel->noteVolume = 0;
						channel->instrument = num;
					}
				}
				break;
			case 0x0D: // Channel press
				if (track->length) {
					++track->data;
					--track->length;
				}
				break;
			case 0x0F: // System Common / RealTime messages / Meta events
				switch (track->status & 0x0F) {
				case 0x00: // Sysex start
				case 0x07: // Sysex end
					varLength = readVariableLength(track->data, track->length);
					if (varLength <= track->length) {
						track->data += varLength;
						track->length -= varLength;
					} else track->length = 0;
					break;
				case 0x01: // MTC Quarter Frame package
				case 0x03: // Song Select
				case 0x04: // Undefined/Unknown
				case 0x05: // Port Select (non-standard)
					if (track->length) {
						++track->data;
						--track->length;
					}
					break;
				case 0x2: // Song Position
					if (track->length >= 2) {
						track->data += 2;
						track->length -= 2;
					} else track->length = 0;
					break;
				case 0x0F: // Meta event
					if (track->length) {
						if (*track->data == 0x2F) { // End of track
							track->length = 0;
						} else {
							if ((track->data[0] == 0x51) && (track->data[1] == 3) && (track->length >= 5)) { // Set tempo
								_midiFile.tempo = (track->data[2] << 16) | (track->data[3] << 8) | track->data[4];
							}
							++track->data;
							--track->length;
							varLength = readVariableLength(track->data, track->length);
							if (varLength <= track->length) {
								track->data += varLength;
								track->length -= varLength;
							} else track->length = 0;
						}
					}
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
			track->delta = readVariableLength(track->data, track->length);
		}
		if (track->length) {
			track->delta--;
		}
	}
}

SfxPlayer_impl *SfxPlayer_impl::create(Resource *res) {
	switch (res->getDataType()) {
	case Resource::DT_DOS:
	case Resource::DT_AMIGA:
	case Resource::DT_ATARI:
	case Resource::DT_ATARI_DEMO:
		return new ModulePlayer(res);
	case Resource::DT_MAC:
		return new MidiPlayer(res);
	default:
		break;
	}
	return 0;
}

