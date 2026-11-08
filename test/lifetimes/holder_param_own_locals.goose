// A by-value holder parameter whose views are not of one array exactly, as
// of a temporary, may point anywhere at its class's depth or outside it. A
// temporary's class is as deep as the callee's outermost scope, but nothing
// the caller passed can view an array the callee declares itself, at that
// scope or deeper, so the callee may shrink it (§5.1).
struct P { s: const i32[:] }
struct B { s: const u8[:] }
struct Opts { k: f64 = 1.0, s: const u8[:] = [] }

fn f(p: P) -> i64 {
    var buf: i32[>..] = [1];
    buf.clear();
    return buf.len + p.s.len;
}

fn g(s: const i32[:]) -> i64 {
    var buf: i32[>..] = [1];
    buf.clear();
    return buf.len + s.len;
}

fn nested(p: P) -> i64 {
    var n = 0;
    if p.s.len > 0 {
        var buf: i32[>..] = [1, 2];
        buf.clear();
        n = buf.len;
    }
    return n + p.s.len;
}

// u8 views, which may view any array's bytes.
fn bytes(b: B) -> i64 {
    var buf: u8[>..] = [1];
    buf.clear();
    return buf.len + b.s.len;
}

fn opts(o: Opts) -> i64 {
    var buf: u8[>..] = [1, 2, 3];
    buf.resize(1);
    var other: f64[>..] = [o.k];
    other.clear();
    return buf.len + o.s.len + other.len;
}

fn main() {
    print(f(P { s: [1, 2] }));
    print(g([1, 2]));
    print(nested(P { s: [1, 2, 3] }));
    print(bytes(B { s: [1, 2] }));
    print(opts(Opts { .. }));
    print(opts(Opts { s: "abc", .. }));
}
