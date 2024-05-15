
#ifndef RESOURCE_MAC_H__
#define RESOURCE_MAC_H__

#include "intern.h"
#include "file.h"

struct ResourceMacMap {
	uint16_t typesOffset;
	uint16_t namesOffset;
	uint16_t typesCount;
};

struct ResourceMacType {
	unsigned char id[4];
	uint16_t count;
	uint16_t startOffset;
};

enum {
	kResourceMacEntryNameLength = 64
};

struct ResourceMacEntry {
	uint16_t id;
	uint16_t nameOffset;
	uint32_t dataOffset;
	char name[kResourceMacEntryNameLength];
};

struct ResourceMac {

	static const char *FILE017;
	static const char *DELPH1;
	static const char *OOTW1;
	static const char *INTRO2;
	static const char *ENDSONG;
	static const unsigned char TYPE_MIDI[4];
	static const unsigned char TYPE_INST[4];
	static const unsigned char TYPE_snd[4];

	File _f;
	char *_dirPath;

	uint32_t _dataOffset;
	ResourceMacMap _map;
	ResourceMacType *_types;
	ResourceMacEntry **_entries;

	ResourceMac(const char *filePath);
	~ResourceMac();

	bool load();
	void loadResourceFork(uint32_t offset, uint32_t size);
	const ResourceMacEntry *findEntry(const char *name) const;
	const ResourceMacEntry *findEntry(const unsigned char typeId[4], uint16_t entryId) const;

	uint8_t *loadFile(int num, uint8_t *dst, uint32_t *size, bool aiff = false);
	const char *getMusic(int num, uint32_t *offset);
	const uint8_t *getInstrument(int num, uint32_t *offset);
};

#endif
