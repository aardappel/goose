// error: the array fill count relies on K keeping its initializer's value, but a writable reference bound to K at
// error: named_const_fill_written_before.goose:10 may change it
// §11.1, §4.4: the reference comes first; the fill count is still compiled
// in as K's initializer says.

fn bump(r: i64&) { r += 10; }
let K = 3;

fn main() {
    bump(K);
    let a = [0; 1 + K];
    print(a.len, " ", 1 + K);
}
