// error: cannot call run, which shrinks b while b is still used: a reference into it was stored there
// A reference of the element type pointing into the tail stops the tail's
// assignment through a reference to the struct (§5.1).
struct Pos { y: i32, x: i32 }
struct B { p: Pos&, r: u32, path: Pos[>..] }
fn run(s: B&) {
    s.path = [Pos { y: 1, x: 2 }];
    s.r += s.p.x as! u32;
}
fn main() {
    var q = Pos { y: 0, x: 0 };
    var b = B { p: q, r: 0, path: [Pos { y: 3, x: 4 }] };
    b.p .= b.path[0];
    run(b);
    print(b.r);
}
