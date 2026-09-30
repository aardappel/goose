// error: the match pattern relies on K keeping its initializer's value, but a writable reference bound to K at
// error: named_const_format_written_before.goose:10 may change it
// §8.1, §11.1, §3.7, §4.4: str hands K itself to the format overload taking
// an i64 by reference, after which K would not match itself.

fn format(out: u8[>..]&, v: i64&) { v += 10; out.append("w"); }
let K = 3;

fn main() {
    let s = str(K);
    match K { K => print(s), _ => print("not K") }
}
