# simd-bench

Simplistic wasm SIMD benchmarks.

Mandelbrot shows significant speedups with SIMD (3.5x).

Raybench has very modest speedups, possibly because it is not well
converted to SIMD yet - it seems that a number of routines could
better use SIMD, and it may also be that the algorithm could be
restructured to fit SIMD better.  The intersection tests in particular
are mostly scalar.  And we could perhaps use a dot product
instruction.
