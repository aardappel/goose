// error: cannot call f, which may shrink b while s
// error: is still used: it may refer into b
// f shrinks an array its parameter only leads to: at the call, any array the
// argument's references may lead to counts as shrunk (§5.2).
struct P { x: i64[>..<]&, y: i64[>..<]& }
fn f(pp: P&) -> i64 { pp.y.pop() }
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    var p = P { x: a, y: b };
    let s = b[..];
    f(p);
    b.push(77);
    print(s[1]);
}
