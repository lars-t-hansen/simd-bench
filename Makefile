JS=~/m-u/js/src/build-release/dist/bin/js --wasm-compiler=ion

.PHONY: all sumcols.bench const.bench mandel.bench raybench.bench

all:
	@echo "Pick a target"

const.bench: const.wasm
	$(JS) const.js

sumcols.bench: sumcols.wasm
	$(JS) sumcols.js

mandel.bench: mandel.js
	$(JS) mandel.js

raybench.bench: raybench.js
	$(JS) raybench.js

const.wasm: const.wat const.js Makefile
	wat2wasm --enable-simd const.wat

sumcols.wasm: sumcols.wat sumcols.js Makefile
	wat2wasm --enable-simd sumcols.wat

# Mandelbrot benchmark
#
# Processing options
#   USE_SIMD    = use SIMD primitives
#   (nothing)   = use scalar computation
#
# Output options (can be combined)
#
#   RUNTIME     = just print timing info and status values on stdout
#   PPMX_STDOUT = dump a ppmx file on stdout
#   SDL_BROWSER = render to a browser canvas using SDL
#
# A ppmx file is a text version of a ppm file.  Convert it to ppm by
# running ppmx2ppm on it.  A ppm can be viewed in Emacs.

# -munimplemented-simd128 will get us v128.const with emcc in June 2020
# -O2 is fine, -O3 generates sort of weird code, hard to understand.
MANDEL_OPT= -s WASM=1 -DUSE_SIMD -std=c++11 -O2 -msimd128 -munimplemented-simd128 

mandel.html: mandel.cpp Makefile
	emcc $(MANDEL_OPT) -DRUNTIME -DSDL_OUTPUT -o mandel.html mandel.cpp

mandel.js: mandel.cpp Makefile
	emcc $(MANDEL_OPT) -DRUNTIME -o mandel.js mandel.cpp

# For the specially interested.
mandel.wasm: mandel.cpp Makefile
	emcc $(MANDEL_OPT) -DRUNTIME -c -o mandel.wasm mandel.cpp

# Ray tracer benchmark
#
# Processing and output options are as for Mandelbrot.

# With SIMD
RAYBENCH_OPT=-s WASM=1 -DUSE_SIMD -DPARTITIONING=true -DSHADOWS=true -DANTIALIAS=true -DREFLECTION=2 -std=c++11 -O2 -msimd128 -munimplemented-simd128 

raybench.html: raybench.cpp Makefile
	emcc $(RAYBENCH_OPT) -DRUNTIME -DSDL_OUTPUT -o raybench.html raybench.cpp

raybench.js: raybench.cpp Makefile
	emcc $(RAYBENCH_OPT) -DRUNTIME -o raybench.js raybench.cpp
