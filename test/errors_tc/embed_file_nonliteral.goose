// embed_file reads at compile time, so its path is a literal.
// error: embed_file takes a string literal
fn main() {
    var p: u8[>..] = "embed_file.txt";
    let b = embed_file(p);
    print(b.len);
}
