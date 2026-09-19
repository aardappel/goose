// A shrink through a reference whose root only bounds its array counts as a
// shrink of every array that root bounds (§5.2, §9.2), and of no other: not
// one declared further in, not one whose elements a live view's pointee
// cannot be, and nothing once the views are dead.
struct P { x: i64[>..<]&, y: i64[>..<]&, name: const u8[:] }

fn pick(c: bool, a: i64[>..<]&, b: i64[>..<]&) -> i64[>..<]& { if c { a } else { b } }
fn drop(xs: i64[>..<]&) -> i64 { xs.pop() }

// pp.y and pp.name are both read out of the caller's storage behind pp, but
// only a view of the popped array's element type is at risk.
fn drop_y(pp: P&) -> i64 {
    let n = pp.name;
    let v = pp.y.pop();
    print(n);
    v
}

fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5, 6, 7];
    var names: u8[>..<] = [104, 105];
    let c = a.len > 5;
    let r: i64[>..<]& = if c { a } else { b };
    // A slice used for the last time before the pop.
    let s = b[..];
    print(s[1]);
    r.pop();
    // A view whose pointee b's elements cannot be.
    let t = names[..];
    r.push(6);
    r.pop();
    print(t.len, " ", a.len, " ", b.len);
    {
        // An array declared further in than r's root is none it may name.
        var z: i64[>..<] = [7, 8];
        let v = z[..];
        drop(pick(c, a, b));
        print(v[1], " ", b.len);
    }
    // Through a parameter that only leads to b: views of other types stay.
    var p = P { x: a, y: b, name: "p" };
    let w = names[..];
    print(drop_y(p), " ", w.len, " ", b.len);
}
