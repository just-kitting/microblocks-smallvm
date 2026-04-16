/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// boardieI2CTargetSimPrims.c - Stubbed simulated I2C target primitives for Boardie.
// Linux gets the first functional backend; Boardie keeps the primitive surface stable.

#include <stdlib.h>

#include "mem.h"
#include "interp.h"

static uint32 emptyByteArray = HEADER(ByteArrayType, 0);

static OBJ primStart(int argCount, OBJ *args) { return falseObj; }
static OBJ primStop(int argCount, OBJ *args) { return falseObj; }
static OBJ primIsStarted(int argCount, OBJ *args) { return falseObj; }
static OBJ primAddress(int argCount, OBJ *args) { return int2obj(-1); }
static OBJ primWriteAvailable(int argCount, OBJ *args) { return falseObj; }
static OBJ primReceiveWrite(int argCount, OBJ *args) { return (OBJ) &emptyByteArray; }
static OBJ primReadRequested(int argCount, OBJ *args) { return falseObj; }
static OBJ primReply(int argCount, OBJ *args) { return falseObj; }

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
