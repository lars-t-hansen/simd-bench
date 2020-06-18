let bin = os.file.readFile("const.wasm", "binary");
let ins = new WebAssembly.Instance(new WebAssembly.Module(bin));

let then = Date.now();
ins.exports.run_ffoo(1000000000);
print("constant " + (Date.now() - then));
