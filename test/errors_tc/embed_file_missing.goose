// A file embed_file cannot read is an error at the call.
// error: embed_file: cannot open
let X = embed_file("nope.bin");
fn main() {}
