struct Holder { r: i64& }
fn read(value: Holder, unused: i64) { print(value.r); }
fn main() {
    var a: i64[>..<] = [42];
    read(Holder { a[0] }, a.pop());
}
