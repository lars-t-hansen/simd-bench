JS=~/m-c/js/src/build-release/dist/bin/js --wasm-simd

.PHONY: all sumcols.bench

all:
	@echo "Pick a target"

sumcols.bench: sumcols.wat
	$(JS) sumcols.js

sumcols.wasm: sumcols.wat
	wat2wasm --enable-simd sumcols.wat
