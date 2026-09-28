// error: cannot pop a: the for loop at
// error: iterates pick(c, mk()[..], a[..]), which may refer into a
// The slice may view the temporary, which nothing shrinks, or a: the loop
// holds both for its whole body (§5.2, §6.5).
fn mk() -> i64[4] { [7, 8, 9, 10] }
fn pick(c: bool, a: i64[:], b: i64[:]) -> i64[:] { if c { a } else { b } }
fn main() {
    var a: i64[>..<] = [1, 2, 3, 4];
    let c = a.len > 10;
    for x in pick(c, mk()[..], a[..]) {
        a.pop();
        a.pop();
        a.push(55);
        a.push(66);
        print(x);
    }
}
