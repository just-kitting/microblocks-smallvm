#!/bin/sh
set -e
#
# Note (old?): Setting ALLOW_MEMORY_GROWTH=1 causes a 2x to 4x slowdown in Firefox on MacBook Pro!
# Note, March 2018: -O3 faster for Firefox & WASM; -Os faster for Safari & Chrome asm.js
#
# The following options to emcc can help with debugging:
#-s ASSERTIONS=2 \
#-s SAFE_HEAP=1 \
#
# Increased memory from 136314880 (130 MB) to 209715200 (~200 MB)
# Increased memory to 268435456 (256 MB) (June 2022)

# copy folders to be included in embedded file system
rm -rf Examples Libraries precompiled esp32 runtime translations
cp -r ../../gp/Examples .
cp -r ../../gp/Libraries .
mkdir -p precompiled
find ../../precompiled -maxdepth 1 -type f \( -name '*.hex' -o -name '*.uf2' -o -name '*.bin' \) -exec cp -t precompiled {} +
mkdir -p esp32
find ../../esp32 -maxdepth 1 -type f -name '*.bin' -exec cp -t esp32 {} +
cp -r ../../gp/runtime .
cp ../../ide/* runtime/lib
mv runtime/lib/MicroBlocksPatches.gp runtime/lib/zzzMicroBlocksPatches.gp # makes patches load last
cp -r ../../translations .

emcc -std=gnu99 -Wall -O3 \
-D EMSCRIPTEN \
-D NO_JPEG \
-D NO_SDL \
-D NO_SOCKETS \
-D SHA2_USE_INTTYPES_H \
-s USE_ZLIB=1 \
-s FETCH=1 \
-s TOTAL_MEMORY=268435456 \
-s ALLOW_MEMORY_GROWTH=0 \
-s WASM=1 \
browserPrims.c cache.c dict.c embeddedFS.c events.c gp.c httpPrims.c interp.c mem.c memGC.c oop.c parse.c \
pathPrims.c prims.c serialPortPrims.c sha1.c sha2.c soundPrims.c textAndFontPrims.c vectorPrims.c \
--preload-file Examples \
--preload-file Libraries \
--preload-file precompiled \
--preload-file esp32 \
--preload-file runtime \
--preload-file translations \
-o gp_wasm.js

# copy the compiler output files to the webapp folder
cp gp_wasm.js ../webapp
cp gp_wasm.wasm ../webapp
cp gp_wasm.data ../webapp

# move the compiler output files into the MicroBlocks folder
mv gp_wasm.js ../MicroBlocks
mv gp_wasm.wasm ../MicroBlocks
mv gp_wasm.data ../MicroBlocks

# remove copied folders after build
rm -r Examples
rm -r Libraries
rm -r precompiled
rm -r esp32
rm -r runtime
rm -r translations
