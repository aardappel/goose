// A slice lvalue bound by reference is the reference `&` of it is (§4.1,
// §9.5): as writable as the path to its slot and, for a variable, its
// binding. So a callee re-points the slice of a `var`, of a plain `let`
// (written through a reference as a var is, §4.4), a field or an element of
// a `var` or of one reached through a writable reference, and a field of a
// `const u8[:]` type in a `var` struct, whose slice stays read-only however
// the reference is (a load through it sees the field's type). In a loop too,
// whose later pass checks the `&` the first one wrote in (§6.5).
struct B { s: const u8[:] }
struct S { f: u8[:] }
var buf: u8[>..] = [];

fn skip(p: (const u8[:])&) { p = p[1..]; }
fn set(q: u8[:]&) { q = buf[1..]; }
fn via(c: S&) { set(c.f); }

fn main() {
    buf.append("xyz");
    var b = B { s: "abcdef" };
    skip(b.s);
    print(b.s);
    let r: (const u8[:])& = b.s;
    r = r[1..];
    print(b.s);
    var o: (const u8[:])&? = null;
    for i in 0..2 {
        skip(b.s);
        o .= b.s;
    }
    if o { print(o); }

    var v = buf[..];
    set(v);
    let t = buf[..];
    set(t);
    print(v, " ", t);

    var s = S { f: buf[..] };
    via(s);
    print(s.f);
    var arr: u8[:][>..] = [];
    arr.push(buf[..]);
    set(arr[0]);
    print(arr[0]);
}
