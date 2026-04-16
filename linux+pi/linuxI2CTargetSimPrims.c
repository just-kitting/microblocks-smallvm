/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// linuxI2CTargetSimPrims.c - Simulated I2C target primitives for the Linux VM.
// These primitives use a simple request/response spool directory so host-side
// tools can emulate an I2C controller talking to MicroBlocks code.

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mem.h"
#include "interp.h"

#define DEFAULT_SIM_DIR "/tmp/microblocks_i2c_target_sim"
#define MAX_REQUEST_ID_LEN 32
#ifndef PATH_MAX
#define PATH_MAX 512
#endif

static int configuredAddress = -1;
static char activeReadID[MAX_REQUEST_ID_LEN];
static uint32 emptyByteArray = HEADER(ByteArrayType, 0);

static const char *simDir() {
	const char *dir = getenv("MICROBLOCKS_I2C_SIM_DIR");
	if (dir && dir[0]) return dir;
	return DEFAULT_SIM_DIR;
}

static int ensureDirectory(const char *path) {
	struct stat info;
	if (0 == stat(path, &info)) return S_ISDIR(info.st_mode);
	return (0 == mkdir(path, 0777)) || (EEXIST == errno);
}

static void clearActiveRequest() {
	activeReadID[0] = 0;
}

static void buildWritePath(char *dst, size_t dstSize, int address, const char *requestID) {
	snprintf(dst, dstSize, "%s/write-%02d-%s.bin", simDir(), address, requestID);
}

static void buildResponsePath(char *dst, size_t dstSize, int address, const char *requestID) {
	snprintf(dst, dstSize, "%s/response-%02d-%s.bin", simDir(), address, requestID);
}

static void buildReadPath(char *dst, size_t dstSize, int address, const char *requestID) {
	snprintf(dst, dstSize, "%s/read-%02d-%s.bin", simDir(), address, requestID);
}

static int readFileBytes(const char *path, uint8 **dataOut, int *byteCountOut) {
	FILE *file = fopen(path, "rb");
	if (!file) return false;
	if (0 != fseek(file, 0, SEEK_END)) {
		fclose(file);
		return false;
	}
	long byteCount = ftell(file);
	if (byteCount < 0) {
		fclose(file);
		return false;
	}
	if (0 != fseek(file, 0, SEEK_SET)) {
		fclose(file);
		return false;
	}

	uint8 *data = NULL;
	if (byteCount > 0) {
		data = (uint8 *) malloc(byteCount);
		if (!data) {
			fclose(file);
			return false;
		}
		if (byteCount != fread(data, 1, byteCount, file)) {
			free(data);
			fclose(file);
			return false;
		}
	}
	fclose(file);
	*dataOut = data;
	*byteCountOut = (int) byteCount;
	return true;
}

static int writeFileBytes(const char *path, const uint8 *data, int byteCount) {
	FILE *file = fopen(path, "wb");
	if (!file) return false;
	if (byteCount > 0) {
		int bytesWritten = fwrite(data, 1, byteCount, file);
		if (bytesWritten != byteCount) {
			fclose(file);
			return false;
		}
	}
	fclose(file);
	return true;
}

static int parseRequestFileName(const char *name, const char *kind, int *addressOut, char *requestIDOut, size_t requestIDSize) {
	int address = -1;
	char requestID[MAX_REQUEST_ID_LEN];
	char format[64];
	snprintf(format, sizeof(format), "%s-%%02d-%%31[^.].bin", kind);
	if (2 != sscanf(name, format, &address, requestID)) return false;
	if ((address < 0) || (address > 127)) return false;
	snprintf(requestIDOut, requestIDSize, "%s", requestID);
	*addressOut = address;
	return true;
}

static int findOldestRequest(const char *kind, int address, char *requestIDOut, size_t requestIDSize) {
	DIR *dir = opendir(simDir());
	if (!dir) return false;

	struct dirent *entry = NULL;
	unsigned long long bestID = 0;
	int found = false;
	char bestRequestID[MAX_REQUEST_ID_LEN];

	while ((entry = readdir(dir)) != NULL) {
		int entryAddress = -1;
		char requestID[MAX_REQUEST_ID_LEN];
		if (!parseRequestFileName(entry->d_name, kind, &entryAddress, requestID, sizeof(requestID))) continue;
		if (entryAddress != address) continue;
		unsigned long long parsedID = strtoull(requestID, NULL, 10);
		if (!found || (parsedID < bestID)) {
			bestID = parsedID;
			snprintf(bestRequestID, sizeof(bestRequestID), "%s", requestID);
			found = true;
		}
	}
	closedir(dir);
	if (!found) return false;
	snprintf(requestIDOut, requestIDSize, "%s", bestRequestID);
	return true;
}

static int objToBytes(OBJ obj, uint8 *dst, int maxCount) {
	if (isInt(obj)) {
		int value = obj2int(obj);
		if (((uint32) value) > 255) {
			fail(byteOutOfRange);
			return -1;
		}
		if (maxCount < 1) return 0;
		dst[0] = value & 255;
		return 1;
	}

	if (IS_TYPE(obj, ByteArrayType)) {
		int byteCount = BYTES(obj);
		if (byteCount > maxCount) byteCount = maxCount;
		memcpy(dst, (uint8 *) &FIELD(obj, 0), byteCount);
		return byteCount;
	}

	if (IS_TYPE(obj, ListType)) {
		int count = obj2int(FIELD(obj, 0));
		if (count > maxCount) count = maxCount;
		for (int i = 0; i < count; i++) {
			OBJ item = FIELD(obj, i + 1);
			if (!isInt(item)) {
				fail(needsListOfIntegers);
				return -1;
			}
			int value = obj2int(item);
			if (((uint32) value) > 255) {
				fail(byteOutOfRange);
				return -1;
			}
			dst[i] = value & 255;
		}
		return count;
	}

	fail(needsByteArray);
	return -1;
}

static OBJ primStart(int argCount, OBJ *args) {
	if ((argCount < 1) || !isInt(args[0])) return fail(needsIntegerError);
	int address = obj2int(args[0]);
	if ((address < 0) || (address > 127)) return fail(i2cDeviceIDOutOfRange);
	if (!ensureDirectory(simDir())) return falseObj;
	configuredAddress = address;
	clearActiveRequest();
	return trueObj;
}

static OBJ primStop(int argCount, OBJ *args) {
	configuredAddress = -1;
	clearActiveRequest();
	return falseObj;
}

static OBJ primIsStarted(int argCount, OBJ *args) {
	return (configuredAddress >= 0) ? trueObj : falseObj;
}

static OBJ primAddress(int argCount, OBJ *args) {
	return int2obj((configuredAddress >= 0) ? configuredAddress : -1);
}

static OBJ primWriteAvailable(int argCount, OBJ *args) {
	if (configuredAddress < 0) return falseObj;
	char requestID[MAX_REQUEST_ID_LEN];
	return findOldestRequest("write", configuredAddress, requestID, sizeof(requestID)) ? trueObj : falseObj;
}

static OBJ primReceiveWrite(int argCount, OBJ *args) {
	if (configuredAddress < 0) return (OBJ) &emptyByteArray;

	char requestID[MAX_REQUEST_ID_LEN];
	if (!findOldestRequest("write", configuredAddress, requestID, sizeof(requestID))) {
		return (OBJ) &emptyByteArray;
	}

	char requestPath[PATH_MAX];
	buildWritePath(requestPath, sizeof(requestPath), configuredAddress, requestID);

	uint8 *data = NULL;
	int byteCount = 0;
	if (!readFileBytes(requestPath, &data, &byteCount)) return (OBJ) &emptyByteArray;
	if (0 != unlink(requestPath)) {
		free(data);
		return (OBJ) &emptyByteArray;
	}

	OBJ result = newObj(ByteArrayType, (byteCount + 3) / 4, falseObj);
	if (!result) {
		free(data);
		return fail(insufficientMemoryError);
	}
	if (byteCount > 0) memcpy((uint8 *) &FIELD(result, 0), data, byteCount);
	setByteCountAdjust(result, byteCount);
	free(data);
	return result;
}

static OBJ primReadRequested(int argCount, OBJ *args) {
	if (configuredAddress < 0) return falseObj;
	if (activeReadID[0]) return trueObj;

	char requestID[MAX_REQUEST_ID_LEN];
	if (!findOldestRequest("read", configuredAddress, requestID, sizeof(requestID))) {
		return falseObj;
	}

	char readPath[PATH_MAX];
	buildReadPath(readPath, sizeof(readPath), configuredAddress, requestID);
	if (0 != unlink(readPath)) return falseObj;
	snprintf(activeReadID, sizeof(activeReadID), "%s", requestID);
	return trueObj;
}

static OBJ primReply(int argCount, OBJ *args) {
	if ((configuredAddress < 0) || !activeReadID[0] || (argCount < 1)) return falseObj;

	uint8 buffer[512];
	int byteCount = objToBytes(args[0], buffer, sizeof(buffer));
	if (byteCount < 0) return falseObj;

	char responsePath[PATH_MAX];
	buildResponsePath(responsePath, sizeof(responsePath), configuredAddress, activeReadID);
	int ok = writeFileBytes(responsePath, buffer, byteCount);
	clearActiveRequest();
	return ok ? trueObj : falseObj;
}

static PrimEntry entries[] = {
	{"start", primStart},
	{"stop", primStop},
	{"isStarted", primIsStarted},
	{"address", primAddress},
	{"writeAvailable", primWriteAvailable},
	{"receiveWrite", primReceiveWrite},
	{"readRequested", primReadRequested},
	{"reply", primReply},
};

void addI2CTargetSimPrims() {
	addPrimitiveSet("i2ctarget", sizeof(entries) / sizeof(PrimEntry), entries);
}
