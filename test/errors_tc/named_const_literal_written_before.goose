// error: the use as a literal relies on K keeping its initializer's value, but a writable reference bound to K at
// error: named_const_literal_written_before.goose:10 may change it
// §3.1, §4.4: the reference comes first; the operand would still adapt K to
// u8 as its initializer says.

fn bump(r: i64&) { r += 1000; }
let K = 3;

fn main() {
    bump(K);
    let b: u8 = 7;
    print(b + K);
}
