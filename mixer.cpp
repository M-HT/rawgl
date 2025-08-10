
/*
 * Another World engine rewrite
 * Copyright (C) 2004-2005 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include <SDL.h>
#include <SDL_mixer.h>
#include <map>
#include "aifcplayer.h"
#include "mixer.h"
#include "mixer_platform.h"
#include "sfxplayer.h"
#include "util.h"

#ifdef USE_MT32EMU
	#define MT32EMU_API_TYPE 1
	#include <mt32emu.h>
#endif

#ifdef USE_LIBADLMIDI
	#include "adlmidi.h"
	#ifdef ADLMIDI_VERSION_MAJOR
		#define ADLMIDI_VERSION_ATLEAST(major,minor,patchlevel) (ADLMIDI_VERSION_MAJOR > (major) || (ADLMIDI_VERSION_MAJOR == (major) && (ADLMIDI_VERSION_MINOR > (minor) || (ADLMIDI_VERSION_MINOR == (minor) && ADLMIDI_VERSION_PATCHLEVEL >= (patchlevel)))))
	#else
		#define ADLMIDI_VERSION_ATLEAST(major,minor,patchlevel) 0
	#endif
#endif

enum {
	TAG_RIFF = 0x46464952,
	TAG_WAVE = 0x45564157,
	TAG_fmt  = 0x20746D66,
	TAG_data = 0x61746164
};

static const bool kAmigaStereoChannels = false; // 0,3:left 1,2:right

static int16_t toS16(int a) {
	return ((a << 8) | a) - 32768;
}

struct MixerChannel {
	const uint8_t *_data;
	Frac _pos;
	uint32_t _len;
	uint32_t _loopLen, _loopPos;
	int _volume;
	void (MixerChannel::*_mixWav)(int16_t *sample, int count);

	void initRaw(const uint8_t *data, int freq, int volume, int mixingFreq) {
		_data = data + 8;
		_pos.reset(freq, mixingFreq);

		const int len = READ_BE_UINT16(data) * 2;
		_loopLen = READ_BE_UINT16(data + 2) * 2;
		_loopPos = _loopLen ? len : 0;
		_len = len;

		_volume = volume;
	}

	void initMac(const uint8_t *data, int freq, int volume, int mixingFreq) {
		_pos.reset(freq, mixingFreq);

		const int len = READ_BE_UINT32(data + 24);
		const int loopStart = (int32_t)READ_BE_UINT32(data + 32);
		const int loopEnd = (int32_t)READ_BE_UINT32(data + 36);
		_data = data + ((loopStart < 0) ? 44 : 42);

		_loopLen = (loopStart >= 0 && loopEnd <= len && loopStart + 1 != loopEnd) ? loopEnd - loopStart : 0;
		_loopPos = loopStart;
		_len = len;

		_volume = volume;
	}

	void initWav(const uint8_t *data, int freq, int volume, int mixingFreq, int len, bool bits16, bool stereo, bool loop) {
		_data = data;
		_pos.reset(freq, mixingFreq);

		_len = len;
		_loopLen = loop ? len : 0;
		_loopPos = 0;
		_volume = volume;
		_mixWav = bits16 ? (stereo ? &MixerChannel::mixWav<16, true> : &MixerChannel::mixWav<16, false>) : (stereo ? &MixerChannel::mixWav<8, true> : &MixerChannel::mixWav<8, false>);
	}
	void mixRaw(int16_t &sample) {
		if (_data) {
			uint32_t pos = _pos.getInt();
			_pos.offset += _pos.inc;
			if (_loopLen != 0) {
				if (pos >= _loopPos + _loopLen) {
					pos = _loopPos;
					_pos.offset = (_loopPos << Frac::BITS) + _pos.inc;
				}
			} else {
				if (pos >= _len) {
					_data = 0;
					return;
				}
			}
			sample = mixS16(sample, toS16(_data[pos] ^ 0x80) * _volume / 64);
		}
	}

	void mixMac(int16_t &sample) {
		if (_data) {
			uint32_t pos = _pos.getInt();
			_pos.offset += _pos.inc;
			if (_loopLen != 0) {
				if (pos >= _loopPos + _loopLen) {
					pos = _loopPos;
					_pos.offset = (_loopPos << Frac::BITS) + _pos.inc;
				}
			} else {
				if (pos >= _len) {
					_data = 0;
					return;
				}
			}
			sample = mixS16(sample, toS16(_data[pos]) * _volume / 64);
		}
	}

	template<int bits, bool stereo>
	void mixWav(int16_t *samples, int count) {
		for (int i = 0; i < count; i += 2) {
			uint32_t pos = _pos.getInt();
			_pos.offset += _pos.inc;
			if (pos >= _len) {
				if (_loopLen != 0) {
					pos = 0;
					_pos.offset = _pos.inc;
				} else {
					_data = 0;
					break;
				}
			}
			if (stereo) {
				pos *= 2;
			}
			int valueL;
			if (bits == 8) { // U8
				valueL = toS16(_data[pos]) * _volume / 64;
			} else { // S16
				valueL = ((int16_t)READ_LE_UINT16(&_data[pos * sizeof(int16_t)])) * _volume / 64;
			}
			*samples = mixS16(*samples, valueL);
			++samples;

			int valueR;
			if (!stereo) {
				valueR = valueL;
			} else {
				if (bits == 8) { // U8
					valueR = toS16(_data[pos + 1]) * _volume / 64;
				} else { // S16
					valueR = ((int16_t)READ_LE_UINT16(&_data[(pos + 1) * sizeof(int16_t)])) * _volume / 64;
				}
			}
			*samples = mixS16(*samples, valueR);
			++samples;
		}
	}
};

static const uint8_t *loadWav(const uint8_t *data, int &freq, int &len, bool &bits16, bool &stereo) {
	uint32_t riffMagic = READ_LE_UINT32(data);
	if (riffMagic != TAG_RIFF) return 0;
	uint32_t riffLength = READ_LE_UINT32(data + 4);
	uint32_t waveMagic = READ_LE_UINT32(data + 8);
	if (waveMagic != TAG_WAVE) return 0;
	uint32_t offset = 12;
	uint32_t chunkMagic, chunkLength = 0;
	// find fmt chunk
	do {
		offset += chunkLength + (chunkLength & 1);
		if (offset >= riffLength) return 0;
		chunkMagic = READ_LE_UINT32(data + offset);
		chunkLength = READ_LE_UINT32(data + offset + 4);
		offset += 8;
	} while (chunkMagic != TAG_fmt);

	if (chunkLength < 14) return 0;
	if (offset + chunkLength >= riffLength) return 0;

	// read format
	int formatTag = READ_LE_UINT16(data + offset);
	int channels = READ_LE_UINT16(data + offset + 2);
	int samplesPerSec = READ_LE_UINT32(data + offset + 4);
	int bitsPerSample = 0;
	if (chunkLength >= 16) {
		bitsPerSample = READ_LE_UINT16(data + offset + 14);
	} else if (formatTag == 1 && channels != 0) {
		int blockAlign = READ_LE_UINT16(data + offset + 12);
		bitsPerSample = (blockAlign * 8) / channels;
	}
	// check supported format
	if ((formatTag != 1) || // PCM
		(channels != 1 && channels != 2) || // mono or stereo
		(bitsPerSample != 8 && bitsPerSample != 16)) { // 8bit or 16bit
		warning("Unsupported wave file");
		return 0;
	}

	// find data chunk
	do {
		offset += chunkLength + (chunkLength & 1);
		if (offset >= riffLength) return 0;
		chunkMagic = READ_LE_UINT32(data + offset);
		chunkLength = READ_LE_UINT32(data + offset + 4);
		offset += 8;
	} while (chunkMagic != TAG_data);

	uint32_t lengthSamples = chunkLength;
	if (offset + lengthSamples - 4 > riffLength) {
		lengthSamples = riffLength + 4 - offset;
	}
	if (channels == 2) lengthSamples >>= 1;
	if (bitsPerSample == 16) lengthSamples >>= 1;

	freq = samplesPerSec;
	len = lengthSamples;
	bits16 = (bitsPerSample == 16);
	stereo = (channels == 2);

	return data + offset;
}

struct Mixer_impl {

#if SDL_VERSIONNUM(SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL) < SDL_VERSIONNUM(2,0,2)
	static const
#endif
	int kMixFreq = 44100;
	static const SDL_AudioFormat kMixFormat = AUDIO_S16SYS;
	static const int kMixSoundChannels = 2;
	static const int kMixBufSize = 4096;
	static const int kMixChannels = 4;

	Mix_Chunk *_sounds[kMixChannels];
	Mix_Music *_music;
	MixerChannel _channels[kMixChannels];
	SfxPlayer *_sfx;
	std::map<int, Mix_Chunk *> _preloads; // AIFF preloads (3DO)
	MixerType _mixerType;
	SDL_AudioDeviceID _audioDevice;

#ifdef USE_MT32EMU
	mt32emu_context _mt32;
#endif

#ifdef USE_LIBADLMIDI
	struct ADL_MIDIPlayer *_adlHandle;
	bool _adlPlaying;
#endif

	void init(MixerType mixerType) {
		memset(_sounds, 0, sizeof(_sounds));
		_music = 0;
		memset(_channels, 0, sizeof(_channels));
		for (int i = 0; i < kMixChannels; ++i) {
			_channels[i]._mixWav = &MixerChannel::mixWav<8, false>;
		}
		_sfx = 0;
#ifdef USE_MT32EMU
		_mt32 = 0;
#endif
		_mixerType = mixerType;

		int flags = 0;
		switch (mixerType) {
#ifndef USE_LIBADLMIDI
		case kMixerTypeWavMidi:
	#if SDL_VERSIONNUM(SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL) >= SDL_VERSIONNUM(2,0,2)
			flags |= MIX_INIT_MID; // renamed with SDL2_mixer >= 2.0.2
	#else
			flags |= MIX_INIT_FLUIDSYNTH;
	#endif
			break;
#endif
		case kMixerTypeWavOgg:
			flags |= MIX_INIT_OGG;
			break;
		default:
			break;
		}
		Mix_Init(flags);
#if SDL_VERSIONNUM(SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL) >= SDL_VERSIONNUM(2,0,2)
		const SDL_version *link_version = Mix_Linked_Version();
		if (SDL_VERSIONNUM(link_version->major, link_version->minor, link_version->patch) >= SDL_VERSIONNUM(2,0,2)) {
			if (Mix_OpenAudioDevice(kMixFreq, kMixFormat, kMixSoundChannels, kMixBufSize, NULL, SDL_AUDIO_ALLOW_ANY_CHANGE & ~(SDL_AUDIO_ALLOW_FORMAT_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE)) < 0) {
				warning("Mix_OpenAudio failed: %s", Mix_GetError());
			} else {
				Mix_QuerySpec(&kMixFreq, NULL, NULL);
			}
		}
		else
#endif
		if (Mix_OpenAudio(kMixFreq, kMixFormat, kMixSoundChannels, kMixBufSize) < 0) {
			warning("Mix_OpenAudio failed: %s", Mix_GetError());
		}
		_audioDevice = 0;
		for (int i = 16; i >= 1; --i) {
			if (SDL_GetAudioDeviceStatus(i) != SDL_AUDIO_STOPPED) {
				_audioDevice = i;
				break;
			}
		}
		switch (mixerType) {
		case kMixerTypeRaw:
			Mix_HookMusic(mixAudio, this);
			break;
		case kMixerTypeMac:
			Mix_HookMusic(mixAudioMac, this);
			break;
		case kMixerTypeWavMidi:
#ifdef USE_LIBADLMIDI
			initAdlMidi();
			Mix_HookMusic(mixAudioAdlMidi, this);
#endif
			/* fall-through */
		case kMixerTypeWav:
		case kMixerTypeWavOgg:
			Mix_SetPostMix(mixAudioWav, this);
			break;
		case kMixerTypeAiff:
			Mix_AllocateChannels(kMixChannels);
			break;
		case kMixerTypeMt32:
#ifdef USE_MT32EMU
			{
				mt32emu_report_handler_i report = { 0 };
				_mt32 = mt32emu_create_context(report, this);
				mt32emu_add_rom_file(_mt32, "CM32L_CONTROL.ROM");
				mt32emu_add_rom_file(_mt32, "CM32L_PCM.ROM");
				mt32emu_set_stereo_output_samplerate(_mt32, kMixFreq);
				mt32emu_open_synth(_mt32);
				mt32emu_set_midi_delay_mode(_mt32, MT32EMU_MDM_IMMEDIATE);
			}
			Mix_HookMusic(mixAudioMt32, this);
#else
			Mix_HookMusic(mixAudio, this);
#endif
			break;
		}
	}
	void quit() {
		stopAll();
#ifdef USE_MT32EMU
		if (_mixerType == kMixerTypeMt32) {
			mt32emu_close_synth(_mt32);
			mt32emu_free_context(_mt32);
		}
#endif
		Mix_CloseAudio();
		Mix_Quit();
#ifdef USE_LIBADLMIDI
		if (_adlHandle) {
			adl_close(_adlHandle);
			_adlHandle = 0;
		}
#endif
	}

#ifdef USE_LIBADLMIDI
	void initAdlMidi() {
		_adlPlaying = false;
		_adlHandle = adl_init(kMixFreq);
		if (!_adlHandle) return;

		if (adl_setNumChips(_adlHandle, 1)) {
			adl_close(_adlHandle);
			_adlHandle = 0;
			return;
		}

		adl_setVolumeRangeModel(_adlHandle, ADLMIDI_VolumeModel_Generic);

	#if ADLMIDI_VERSION_ATLEAST(1,3,2)
		adl_switchEmulator(_adlHandle, ADLMIDI_EMU_DOSBOX);
	#endif

		if (adl_setBank(_adlHandle, 77)) {
			adl_close(_adlHandle);
			_adlHandle = 0;
			return;
		}

		adl_reset(_adlHandle);

		adl_setLoopEnabled(_adlHandle, 0);
	}

	static void mixAudioAdlMidi(void *data, uint8_t *s16buf, int len) {
		Mixer_impl *mixer = (Mixer_impl *)data;
		if (mixer->_adlPlaying) {
			int numSamples = adl_play(mixer->_adlHandle, len / sizeof(int16_t), (short *) s16buf);
			len -= numSamples * sizeof(int16_t);
			s16buf += numSamples * sizeof(int16_t);
		}
		if (len) {
			memset(s16buf, 0, len);
		}
	}
#endif

	void update() {
		for (int i = 0; i < kMixChannels; ++i) {
			if (_sounds[i] && !Mix_Playing(i)) {
				freeSound(i);
			}
		}
	}

	void lockAudio() {
		if (_audioDevice) {
			SDL_LockAudioDevice(_audioDevice);
		}
	}

	void unlockAudio() {
		if (_audioDevice) {
			SDL_UnlockAudioDevice(_audioDevice);
		}
	}

	void playSoundRaw(uint8_t channel, const uint8_t *data, int freq, uint8_t volume) {
		lockAudio();
		_channels[channel].initRaw(data, freq, volume, kMixFreq);
		unlockAudio();
	}
	void playSoundMac(uint8_t channel, const uint8_t *data, int freq, uint8_t volume) {
		lockAudio();
		_channels[channel].initMac(data, freq, volume, kMixFreq);
		unlockAudio();
	}
	void playSoundWav(uint8_t channel, const uint8_t *data, int freq, uint8_t volume, bool loop) {
		int wavFreq, len;
		bool bits16, stereo;
		const uint8_t *wavData = loadWav(data, wavFreq, len, bits16, stereo);
		if (!wavData) return;

		if (wavFreq == 22050 || wavFreq == 44100 || wavFreq == 48000) {
			freq = (int)(freq * (wavFreq / 9943.0f));
		}

		lockAudio();
		_channels[channel].initWav(wavData, freq, volume, kMixFreq, len, bits16, stereo, loop);
		unlockAudio();
	}
	void playSound(uint8_t channel, int volume, Mix_Chunk *chunk, int loops = 0) {
		stopSound(channel);
		if (chunk) {
			Mix_PlayChannelTimed(channel, chunk, loops, -1);
		}
		setChannelVolume(channel, volume);
		_sounds[channel] = chunk;
	}
	void stopSound(uint8_t channel) {
		if (_mixerType == kMixerTypeMt32) {
			stopSoundMt32();
		}
		lockAudio();
		_channels[channel]._data = 0;
		unlockAudio();
		Mix_HaltChannel(channel);
		freeSound(channel);
	}
	static const uint8_t *findMt32Sound(int num) {
		for (const uint8_t *data = Mixer::_mt32SoundsTable; data[0]; data += 4) {
			if (data[0] == num) {
				return data;
			}
		}
		return 0;
	}
	void playSoundMt32(int num) {
#ifdef USE_MT32EMU
		const uint8_t *data = findMt32Sound(num);
		if (data) {
			for (; data[0] == num; data += 4) {
				int8_t note = data[1];

				uint32_t noteOn = 0x99;
				noteOn |= ABS(note) << 8;
				noteOn |= 0x7f << 16;
				mt32emu_play_msg(_mt32, noteOn);

				uint32_t pitchBend = 0xe9;
				pitchBend |= (READ_LE_UINT16(data) & 0x7f) << 8;
				pitchBend |= (0x3f80 >> 7) << 16;
				mt32emu_play_msg(_mt32, pitchBend);

				if (note < 0) {
					uint32_t noteVel = 0x99;
					noteVel |= ABS(note) << 8;
					noteVel |= 0 << 16;
					mt32emu_play_msg(_mt32, noteVel);
				}
			}
		}
#endif
	}
	void stopSoundMt32() {
#ifdef USE_MT32EMU
		uint32_t controlChange = 0xb9;
		controlChange |= 0x7b << 8;
		controlChange |= 0 << 16;
		mt32emu_play_msg(_mt32, controlChange);
#endif
	}
	void freeSound(int channel) {
		Mix_FreeChunk(_sounds[channel]);
		_sounds[channel] = 0;
	}
	void setChannelVolume(uint8_t channel, uint8_t volume) {
		lockAudio();
		_channels[channel]._volume = volume;
		unlockAudio();
		Mix_Volume(channel, volume * MIX_MAX_VOLUME / 63);
	}

	void playMusic(const char *path, int loops = 0) {
		stopMusic();
#ifdef USE_LIBADLMIDI
		if (_mixerType == kMixerTypeWavMidi) {
			if (_adlHandle) {
				lockAudio();
				if (adl_openFile(_adlHandle, path)) {
					warning("Failed to load music '%s', %s", path, adl_errorInfo(_adlHandle));
				} else {
					_adlPlaying = true;
				}
				unlockAudio();
			}
			return;
		}
#endif
		_music = Mix_LoadMUS(path);
		if (_music) {
			if (_mixerType == kMixerTypeWavMidi) {
				Mix_VolumeMusic(MIX_MAX_VOLUME/2);
			} else {
				Mix_VolumeMusic(MIX_MAX_VOLUME);
			}
			Mix_PlayMusic(_music, loops);
		} else {
			warning("Failed to load music '%s', %s", path, Mix_GetError());
		}
	}
	void stopMusic() {
#ifdef USE_LIBADLMIDI
		if (_mixerType == kMixerTypeWavMidi) {
			if (_adlHandle) {
				lockAudio();
				adl_reset(_adlHandle);
				_adlPlaying = false;
				unlockAudio();
			}
			return;
		}
#endif
		Mix_HaltMusic();
		Mix_FreeMusic(_music);
		_music = 0;
	}

	static void mixAifcPlayer(void *data, uint8_t *s16buf, int len) {
		((AifcPlayer *)data)->readSamples((int16_t *)s16buf, len / 2);
	}
	void playAifcMusic(AifcPlayer *aifc) {
		Mix_HookMusic(mixAifcPlayer, aifc);
	}
	void stopAifcMusic() {
		Mix_HookMusic(0, 0);
	}

	void playSfxMusic(SfxPlayer *sfx) {
		lockAudio();
		_sfx = sfx;
		_sfx->play(kMixFreq);
		unlockAudio();
	}
	void stopSfxMusic() {
		lockAudio();
		if (_sfx) {
			_sfx->stop();
			_sfx = 0;
		}
		unlockAudio();
	}

	void mixChannels(int16_t *samples, int count) {
		if (kAmigaStereoChannels) {
			for (int i = 0; i < count; i += 2) {
				_channels[0].mixRaw(*samples);
				_channels[3].mixRaw(*samples);
				++samples;
				_channels[1].mixRaw(*samples);
				_channels[2].mixRaw(*samples);
				++samples;
			}
		}  else {
			for (int i = 0; i < count; i += 2) {
				for (int j = 0; j < kMixChannels; ++j) {
					_channels[j].mixRaw(samples[i]);
				}
				samples[i + 1] = samples[i];
			}
		}
	}

	static void mixAudio(void *data, uint8_t *s16buf, int len) {
		memset(s16buf, 0, len);
		Mixer_impl *mixer = (Mixer_impl *)data;
		mixer->mixChannels((int16_t *)s16buf, len / sizeof(int16_t));
		if (mixer->_sfx) {
			mixer->_sfx->readSamples((int16_t *)s16buf, len / sizeof(int16_t));
		}
	}

	void mixChannelsMac(int16_t *samples, int count) {
		for (int i = 0; i < count; i += 2) {
			for (int j = 0; j < kMixChannels; ++j) {
				_channels[j].mixMac(samples[i]);
			}
			samples[i + 1] = samples[i];
		}
	}

	static void mixAudioMac(void *data, uint8_t *s16buf, int len) {
		memset(s16buf, 0, len);
		Mixer_impl *mixer = (Mixer_impl *)data;
		mixer->mixChannelsMac((int16_t *)s16buf, len / sizeof(int16_t));
		if (mixer->_sfx) {
			mixer->_sfx->readSamples((int16_t *)s16buf, len / sizeof(int16_t));
		}
	}

	void mixChannelsWav(int16_t *samples, int count) {
		for (int i = 0; i < kMixChannels; ++i) {
			if (_channels[i]._data) {
				(_channels[i].*_channels[i]._mixWav)(samples, count);
			}
		}
	}

	static void mixAudioWav(void *data, uint8_t *s16buf, int len) {
		Mixer_impl *mixer = (Mixer_impl *)data;
		mixer->mixChannelsWav((int16_t *)s16buf, len / sizeof(int16_t));
	}

#ifdef USE_MT32EMU
	static void mixAudioMt32(void *data, uint8_t *s16buf, int len) {
		Mixer_impl *mixer = (Mixer_impl *)data;
		mt32emu_render_bit16s(mixer->_mt32, (int16_t *)s16buf, len / (2 * sizeof(int16_t)));
		mixer->mixChannels((int16_t *)s16buf, len / sizeof(int16_t));
		if (mixer->_sfx) {
			mixer->_sfx->readSamples((int16_t *)s16buf, len / sizeof(int16_t));
		}
	}
#endif

	void stopAll() {
		for (int i = 0; i < kMixChannels; ++i) {
			stopSound(i);
		}
		stopMusic();
		stopSfxMusic();
		if (_mixerType == kMixerTypeAiff) {
			stopAifcMusic();
		}
		for (std::map<int, Mix_Chunk *>::iterator it = _preloads.begin(); it != _preloads.end(); ++it) {
			debug(DBG_SND, "Flush preload %d", it->first);
			Mix_FreeChunk(it->second);
		}
		_preloads.clear();
	}

	void preloadSoundAiff(int num, const uint8_t *data) {
		if (_preloads.find(num) != _preloads.end()) {
			warning("AIFF sound %d is already preloaded", num);
		} else {
			const uint32_t size = READ_BE_UINT32(data + 4) + 8;
			SDL_RWops *rw = SDL_RWFromConstMem(data, size);
			Mix_Chunk *chunk = Mix_LoadWAV_RW(rw, 0);
			rw->close(rw);
			_preloads[num] = chunk;
		}
	}

	void playSoundAiff(int channel, int num, int volume) {
		if (_preloads.find(num) == _preloads.end()) {
			warning("AIFF sound %d is not preloaded", num);
		} else {
			Mix_Chunk *chunk = _preloads[num];
			Mix_PlayChannelTimed(channel, chunk, 0, -1);
			Mix_Volume(channel, volume * MIX_MAX_VOLUME / 63);
		}
	}
};

Mixer::Mixer(SfxPlayer *sfx)
	: _aifc(0), _sfx(sfx) {
}

void Mixer::init(MixerType mixerType) {
	_impl = new Mixer_impl();
	_impl->init(mixerType);
}

void Mixer::quit() {
	stopAll();
	if (_impl) {
		_impl->quit();
		delete _impl;
	}
	delete _aifc;
}

void Mixer::update() {
	if (_impl) {
		_impl->update();
	}
}

bool Mixer::hasMt32() const {
	return _impl && (_impl->_mixerType == kMixerTypeMt32);
}

bool Mixer::hasMt32SoundMapping(int num) {
	return Mixer_impl::findMt32Sound(num) != 0;
}

void Mixer::playSoundRaw(uint8_t channel, const uint8_t *data, uint16_t freq, uint8_t volume) {
	debug(DBG_SND, "Mixer::playSoundRaw(%d, %d, %d)", channel, freq, volume);
	if (_impl) {
		return _impl->playSoundRaw(channel, data, freq, volume);
	}
}

void Mixer::playSoundMac(uint8_t channel, const uint8_t *data, uint16_t freq, uint8_t volume) {
	debug(DBG_SND, "Mixer::playSoundMac(%d, %d, %d)", channel, freq, volume);
	if (_impl) {
		return _impl->playSoundMac(channel, data, freq, volume);
	}
}

void Mixer::playSoundWav(uint8_t channel, const uint8_t *data, uint16_t freq, uint8_t volume, uint8_t loop) {
	debug(DBG_SND, "Mixer::playSoundWav(%d, %d, %d)", channel, volume, loop);
	if (_impl) {
		return _impl->playSoundWav(channel, data, freq, volume, loop);
	}
}

void Mixer::stopSound(uint8_t channel) {
	debug(DBG_SND, "Mixer::stopSound(%d)", channel);
	if (_impl) {
		return _impl->stopSound(channel);
	}
}

void Mixer::playSoundMt32(int num) {
	debug(DBG_SND, "Mixer::playSoundMt32(%d)", num);
	if (_impl) {
		return _impl->playSoundMt32(num);
	}
}

void Mixer::setChannelVolume(uint8_t channel, uint8_t volume) {
	debug(DBG_SND, "Mixer::setChannelVolume(%d, %d)", channel, volume);
	if (_impl) {
		return _impl->setChannelVolume(channel, volume);
	}
}

void Mixer::playMusic(const char *path, uint8_t loop) {
	debug(DBG_SND, "Mixer::playMusic(%s, %d)", path, loop);
	if (_impl) {
		return _impl->playMusic(path, (loop != 0) ? -1 : 0);
	}
}

void Mixer::stopMusic() {
	debug(DBG_SND, "Mixer::stopMusic()");
	if (_impl) {
		return _impl->stopMusic();
	}
}

void Mixer::playAifcMusic(const char *path, uint32_t offset) {
	debug(DBG_SND, "Mixer::playAifcMusic(%s)", path);
	if (!_aifc) {
		_aifc = new AifcPlayer();
	}
	if (_impl) {
		_impl->stopAifcMusic();
		if (_aifc->play(_impl->kMixFreq, path, offset)) {
			_impl->playAifcMusic(_aifc);
		}
	}
}

void Mixer::stopAifcMusic() {
	debug(DBG_SND, "Mixer::stopAifcMusic()");
	if (_impl && _aifc) {
		_aifc->stop();
		_impl->stopAifcMusic();
	}
}

void Mixer::playSfxMusic(int num) {
	debug(DBG_SND, "Mixer::playSfxMusic(%d)", num);
	if (_impl && _sfx) {
		return _impl->playSfxMusic(_sfx);
	}
}

void Mixer::stopSfxMusic() {
	debug(DBG_SND, "Mixer::stopSfxMusic()");
	if (_impl && _sfx) {
		return _impl->stopSfxMusic();
	}
}

void Mixer::stopAll() {
	debug(DBG_SND, "Mixer::stopAll()");
	if (_impl) {
		return _impl->stopAll();
	}
}

void Mixer::preloadSoundAiff(uint8_t num, const uint8_t *data) {
	debug(DBG_SND, "Mixer::preloadSoundAiff(num:%d, data:%p)", num, data);
	if (_impl) {
		return _impl->preloadSoundAiff(num, data);
	}
}

void Mixer::playSoundAiff(uint8_t channel, uint8_t num, uint8_t volume) {
	debug(DBG_SND, "Mixer::playSoundAiff()");
	if (_impl) {
		return _impl->playSoundAiff(channel, num, volume);
	}
}
