JS=~/m-c/js/src/build-release/dist/bin/js --wasm-compiler=ion

.PHONY: all sumcols.bench const.bench

all:
	@echo "Pick a target"

const.bench: const.wasm
	$(JS) const.js

sumcols.bench: sumcols.wasm
	$(JS) sumcols.js

const.wasm: const.wat Makefile
	wat2wasm --enable-simd const.wat

sumcols.wasm: sumcols.wat Makefile
	wat2wasm --enable-simd sumcols.wat

mandel.wasm: mandel.cpp Makefile
	emcc -s WASM=1 -std=c++11 -O3 -msimd128 -o mandel.html mandel.cpp
