// Simplistic wasm benchmark: sum columns in a long array of v128, compare simd
// and scalar, for a few widths

let bin = os.file.readFile("sumcols.wasm", "binary");
let ins = new WebAssembly.Instance(new WebAssembly.Module(bin));

let mem = new Float32Array(ins.exports.mem.buffer);
for ( let i=0 ; i < 1000*4; i++ )
    mem[i] = i;
let then = Date.now();
for ( let i=0 ; i < 30000 ; i++ )
    ins.exports.sumf32x4(16, 1000);
print("f32 simd " + (Date.now() - then));
let xs=get(mem, 4);
then = Date.now();
for ( let i=0 ; i < 30000 ; i++ )
    ins.exports.sumf32x4_scalar(16, 1000);
print("f32 scalar " + (Date.now() - then));
assertSame(xs, get(mem, 4));

mem = new Float64Array(ins.exports.mem.buffer);
for ( let i=0 ; i < 1000*2; i++ )
    mem[i] = i;
then = Date.now();
for ( let i=0 ; i < 30000 ; i++ )
    ins.exports.sumf64x2(16, 1000);
print("f64 simd " + (Date.now() - then));
xs=get(mem, 2);
then = Date.now();
for ( let i=0 ; i < 30000 ; i++ )
    ins.exports.sumf64x2_scalar(16, 1000);
print("f64 scalar " + (Date.now() - then));
assertSame(xs, get(mem, 2));

mem = new Int32Array(ins.exports.mem.buffer);
for ( let i=0 ; i < 1000*4; i++ )
    mem[i] = i;
then = Date.now();
for ( let i=0 ; i < 30000 ; i++ )
    ins.exports.sumi32x4(16, 1000);
print("i32 simd " + (Date.now() - then));
xs=get(mem, 4);
then = Date.now();
for ( let i=0 ; i < 30000 ; i++ )
    ins.exports.sumi32x4_scalar(16, 1000);
print("i32 scalar " + (Date.now() - then));
assertSame(xs, get(mem, 4));

mem = new Int8Array(ins.exports.mem.buffer);
for ( let i=0 ; i < 1000*16; i++ )
    mem[i] = i;
then = Date.now();
for ( let i=0 ; i < 30000 ; i++ )
    ins.exports.sumi8x16(16, 1000);
print("i8 simd " + (Date.now() - then));
xs=get(mem, 16);
then = Date.now();
for ( let i=0 ; i < 30000 ; i++ )
    ins.exports.sumi8x16_scalar(16, 1000);
print("i8 scalar " + (Date.now() - then));
assertSame(xs, get(mem, 16));

function assertSame(xs, ys) {
    assertEq(xs.length, ys.length);
    for ( let i=0 ; i < xs.length ; i++ )
        assertEq(xs[i], ys[i]);
}

function get(mem, n) {
    let xs = [];
    for ( let i=0; i < n; i++ )
        xs.push(mem[i]);
    return xs;
}
