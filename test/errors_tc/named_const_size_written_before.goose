// error: the array size relies on K keeping its initializer's value, but a writable reference bound to K at
// error: named_const_size_written_before.goose:11 may change it
// §11.1, §4.4: the reference comes first, but a writable reference to K
// anywhere is enough: the capacity is compiled in as K's initializer says,
// 3, and the loop runs to 13.

fn bump(r: i64&) { r += 10; }
let K = 3;

fn main() {
    bump(K);
    var a: i64[..K] = [];
    for i in 0..K { a.push(i); }
    print(a.len);
}
