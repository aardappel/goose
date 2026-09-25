// error: b is not fixed-size and is not copied implicitly
// A later break agrees with the first break's type, which is no destination:
// the block's value is still a copy of the storage it names (§4.1).
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len > b.len;
    let x = block { if c { break copy(a); } break b; };
    print(x.len);
}
