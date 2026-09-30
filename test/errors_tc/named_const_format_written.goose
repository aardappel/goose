// error: K is bound to a writable reference here, through which it may change, while the array size at
// error: named_const_format_written.goose:9 relies on it keeping its initializer's value
// §11.1, §3.7, §4.4: print hands K itself to the format overload taking an
// i64 by reference, which makes it 13, while a's type took it at 3.

fn format(out: u8[>..]&, v: i64&) { v += 10; out.append("w"); }

let K = 3;
var a: i64[K] = [1, 2, 3];

fn main() {
    print(K);
    print(a.len == 3);
}
