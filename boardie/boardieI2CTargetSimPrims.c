/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// boardieI2CTargetSimPrims.c - Simulated I2C target primitives for Boardie.
// The browser runtime uses a JavaScript queue shared with the parent page.

#include <stdlib.h>
#include <string.h>

#include <emscripten.h>

#include "mem.h"
#include "interp.h"

static int configuredAddress = -1;
static int activeReadID = -1;
static uint32 emptyByteArray = HEADER(ByteArrayType, 0);

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
	configuredAddress = address;
	activeReadID = -1;
	EM_ASM({
		if (window.BadgeSnakeI2CTarget) {
			window.BadgeSnakeI2CTarget.clearAddress($0);
		}
	}, address);
	return trueObj;
}

static OBJ primStop(int argCount, OBJ *args) {
	configuredAddress = -1;
	activeReadID = -1;
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
	return EM_ASM_INT({
		return (window.BadgeSnakeI2CTarget &&
			window.BadgeSnakeI2CTarget.peekWrite($0)) ? 1 : 0;
	}, configuredAddress) ? trueObj : falseObj;
}

static OBJ primReceiveWrite(int argCount, OBJ *args) {
	if (configuredAddress < 0) return (OBJ) &emptyByteArray;

	int byteCount = EM_ASM_INT({
		if (!window.BadgeSnakeI2CTarget) return -1;
		var item = window.BadgeSnakeI2CTarget.peekWrite($0);
		return item ? item.data.length : -1;
	}, configuredAddress);
	if (byteCount < 0) return (OBJ) &emptyByteArray;

	OBJ result = newObj(ByteArrayType, (byteCount + 3) / 4, falseObj);
	if (!result) return fail(insufficientMemoryError);
	if (byteCount > 0) {
		EM_ASM({
			if (!window.BadgeSnakeI2CTarget) return;
			var item = window.BadgeSnakeI2CTarget.shiftWrite($0);
			if (item) HEAPU8.set(item.data, $1);
		}, configuredAddress, (int) ((uint8 *) &FIELD(result, 0)), byteCount);
	} else {
		EM_ASM({
			if (!window.BadgeSnakeI2CTarget) return;
			window.BadgeSnakeI2CTarget.shiftWrite($0);
		}, configuredAddress);
	}
	setByteCountAdjust(result, byteCount);
	return result;
}

static OBJ primReadRequested(int argCount, OBJ *args) {
	if (configuredAddress < 0) return falseObj;
	if (activeReadID >= 0) return trueObj;

	int requestID = EM_ASM_INT({
		if (!window.BadgeSnakeI2CTarget) return -1;
		var item = window.BadgeSnakeI2CTarget.shiftRead($0);
		return item ? item.id : -1;
	}, configuredAddress);
	if (requestID < 0) return falseObj;
	activeReadID = requestID;
	return trueObj;
}

static OBJ primReply(int argCount, OBJ *args) {
	if ((configuredAddress < 0) || (activeReadID < 0) || (argCount < 1)) return falseObj;
	uint8 buffer[512];
	int byteCount = objToBytes(args[0], buffer, sizeof(buffer));
	if (byteCount < 0) return falseObj;

	EM_ASM({
		if (!window.BadgeSnakeI2CTarget) return;
		var data = HEAPU8.slice($2, $2 + $3);
		window.BadgeSnakeI2CTarget.storeResponse($0, $1, data);
	}, configuredAddress, activeReadID, (int) buffer, byteCount);
	activeReadID = -1;
	return trueObj;
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
