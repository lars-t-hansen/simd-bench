JS=~/m-u/js/src/build-release/dist/bin/js --wasm-compiler=ion

.PHONY: all sumcols.bench const.bench mandel.bench

all:
	@echo "Pick a target"

const.bench: const.wasm
	$(JS) const.js

sumcols.bench: sumcols.wasm
	$(JS) sumcols.js

mandel.bench: mandel.js
	$(JS) mandel.js

const.wasm: const.wat const.js Makefile
	wat2wasm --enable-simd const.wat

sumcols.wasm: sumcols.wat sumcols.js Makefile
	wat2wasm --enable-simd sumcols.wat

# -munimplemented-simd128 will get us v128.const
MANDEL_OPT= -s WASM=1 -DUSE_SIMD -std=c++11 -O3 -msimd128 -munimplemented-simd128 

mandel.html: mandel.cpp Makefile
	emcc $(MANDEL_OPT) -DRUNTIME -DSDL_OUTPUT -o mandel.html mandel.cpp

# Here, remove -DRUNTIME and add -DPPMX_OUTPUT and capture the output
# in a .ppmx file (be sure not to run via make or you also get the
# command lines), and then run ppmx2ppm to generate a ppm to check the
# output.
mandel.js: mandel.cpp Makefile
	emcc $(MANDEL_OPT) -DRUNTIME -o mandel.js mandel.cpp

mandel.wasm: mandel.cpp Makefile
	emcc $(MANDEL_OPT) -DRUNTIME -c -o mandel.wasm mandel.cpp
