// A resize back to a mark is balanced (§5.2) while nothing can write the
// mark (§4.4): reading it, passing it by value or to a `const` reference and
// binding a `const` reference to it leave it so, and v stays in use across
// the call. A mark written through a reference makes its resize an ordinary
// shrink, which a call with no view live across it may still make.

fn show(r: const i64&) -> i64 { r }
fn twice(x: i64) -> i64 { x * 2 }
fn setzero(r: i64&) { r = 0; }

fn helper(s: i64[>..<]&) -> i64 {
    let mark = s.len;
    s.push(7);
    let c: const i64& = mark;
    let got = show(mark) + twice(mark) + c;
    s.resize(mark);
    got
}

fn clearing(s: i64[>..<]&) {
    let mark = s.len;
    s.push(7);
    setzero(mark);
    s.resize(mark);
}

fn main() {
    var a: i64[>..<] = [1, 2];
    let v = a[0..];
    print(helper(a), " ", a.len, " ", v[1]);
    clearing(a);
    print(a.len);
}
