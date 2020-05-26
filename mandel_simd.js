// SIMD computation of the Mandelbrot set, for the SpiderMonkey shell.
// Hand-translated to JS because of an apparent Emscripten bug.  See mandel.cpp for the C++ code.

// OUTPUT CONTROL
//
// If "ppm", print an ugly but ASCII-safe PPM representation of the set on stdout (3.5MB).
// You can open this in Emacs, or use some other viewer.
//
// If "timing", print timing information as a string.
//
// If "disassembly", print the disassembly of the mandelbrot function (in a SpiderMonkey debug build)
const OUTPUT = "timing";

function ROUNDUP4(x) {
    return (x+3)&~3;
}

function SCALE(v, range, min, max) {
    return `(f32.add (f32.const ${min}) (f32.mul (local.get ${v}) (f32.const ${(max - min) / range})))`;
}

const WIDTH = ROUNDUP4(Math.floor(400*3.5));
const HEIGHT = 400*2;
const CUTOFF = 1000;

const PAGES = Math.ceil((HEIGHT * WIDTH * 4) / 65536);

const TXT = `
(module
  (memory (export "mem") ${PAGES})

  (func (export "mandel_simd")
    (local $Py f32)
    (local $Px f32)
    (local $addr i32)
    (local $y0 v128)
    (local $x0 v128)
    (local $x v128)
    (local $y v128)
    (local $active v128)
    (local $counter v128)
    (local $x_sq v128)
    (local $y_sq v128)
    (local $sum_sq v128)
    (local $tmp v128)
    (local $xy v128)

    (block $YBLOCK
      (loop $YLOOP
        (br_if $YBLOCK (f32.ge (local.get $Py) (f32.const ${HEIGHT})))
        (local.set $y0 (f32x4.splat ${SCALE('$Py', HEIGHT, -1, 1)}))

        (local.set $Px (f32.const 0))
        (block $XBLOCK
          (loop $XLOOP
            (br_if $XBLOCK (f32.ge (local.get $Px) (f32.const ${WIDTH})))
            (local.set $x0 (f32x4.splat ${SCALE('$Px', WIDTH, -2.5, 1)}))
            (local.set $x (v128.const f32x4 0 0 0 0))
            (local.set $y (v128.const f32x4 0 0 0 0))
            (local.set $active (v128.const i32x4 -1 -1 -1 -1))
            (local.set $counter (v128.const i32x4 ${CUTOFF} ${CUTOFF} ${CUTOFF} ${CUTOFF}))

            (block $IBLOCK
              (loop $ILOOP
                (local.set $x_sq (f32x4.mul (local.get $x) (local.get $x)))
                (local.set $y_sq (f32x4.mul (local.get $y) (local.get $y)))
                (local.set $sum_sq (f32x4.add (local.get $x_sq) (local.get $y_sq)))
                (local.set $active (v128.and (local.get $active)
                                             (f32x4.le (local.get $sum_sq) (v128.const f32x4 4 4 4 4))))
                (local.set $active (v128.and (local.get $active)
                                             (i32x4.ge_s (local.get $counter) (v128.const i32x4 0 0 0 0))))
                (br_if $IBLOCK (i32.eqz (i32x4.any_true (local.get $active))))
                (local.set $tmp (f32x4.add (f32x4.sub (local.get $x_sq) (local.get $y_sq)) (local.get $x0)))
                (local.set $xy (f32x4.mul (local.get $x) (local.get $y)))
                (local.set $y (f32x4.add (f32x4.add (local.get $xy) (local.get $xy)) (local.get $y0)))
                (local.set $x (local.get $tmp))
                (local.set $counter (i32x4.add (local.get $counter) (local.get $active)))
                (br $ILOOP)))

            (local.set $counter (i32x4.sub (v128.const i32x4 ${CUTOFF} ${CUTOFF} ${CUTOFF} ${CUTOFF})
                                           (local.get $counter)))
            (v128.store (local.get $addr) (local.get $counter))
            (local.set $addr (i32.add (local.get $addr) (i32.const 16)))
            (local.set $Px (f32.add (local.get $Px) (f32.const 4)))
            (br $XLOOP)))

        (local.set $Py (f32.add (local.get $Py) (f32.const 1)))
        (br $YLOOP)))

))`;

var ins = new WebAssembly.Instance(new WebAssembly.Module(wasmTextToBinary(TXT)));
var then = Date.now();
ins.exports.mandel_simd();
var now = Date.now();

switch (OUTPUT) {
case "ppm": {
    let mem = new Int32Array(ins.exports.mem.buffer);
    let out = `P6 ${WIDTH} ${HEIGHT} 255\n`;
    for ( let h=HEIGHT; h > 0; h-- ) {
        for ( let w=0; w < WIDTH; w++ ) {
            let elem = mem[h*WIDTH + w];
            if (elem >= CUTOFF) {
                out += '   ';
            } else {
                out += '~~~';
            }
        }
    }
    print(out);
    break;
}
case "timing":
    print(now - then + "ms");
    break;
case "disassembly":
    wasmDis(ins.exports.mandel_simd);
    break;
default:
    throw "INVALID OUTPUT SPEC " + OUTPUT;
}

