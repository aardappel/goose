// Defaults that construct their own type or call their own function, and
// still take no default of theirs again before a function body (§3.2, §7.1):
// another type's defaults, default<T>() of another type, nested literals, one
// default taken twice side by side, the own type with the field written, a
// larger instantiation with the field written, and two types whose defaults
// meet with one of them written. Through the body of a top-level function,
// or of a function declared in one, the chain is a recursion (§7.8), here
// ending when a global says so.
var level = 0;
var depth = 0;

struct Inner { v: i64 = 7, w: i64[2] = [1, 2] }
struct Outer {
    i: Inner = Inner { .. },
    j: i64 = default<Inner>().v + 1,
    k: i64 = Inner { v: 3, .. }.v + Inner { .. }.w[1],
}

struct Own { a: i64 = 1, d: i64 = Own { d: 5, .. }.a + 10 }

struct Grow<T> { v: T[] = [], d: i64 = Grow<T[]> { d: 2, .. }.v.len + 20 }

struct Ping { a: i64 = 1, p: i64 = Pong { .. }.q }
struct Pong { q: i64 = Ping { p: 30, .. }.a + 100 }

struct Nest { d: i64 = nest() }
recursive fn nest() -> i64 {
    fn again() -> i64 { Nest { .. }.d + 1 }
    if level >= 3 { return 0; }
    level += 1;
    return again();
}

fn count(k: i64 = below()) -> i64 { k }
recursive fn below() -> i64 {
    if depth >= 4 { return 0; }
    depth += 1;
    return count() + 1;
}

fn main() {
    let o = Outer { .. };
    print(o.i.v, " ", o.i.w[0], " ", o.j, " ", o.k);
    print(Own { .. }.d);
    print(Grow<i64> { .. }.d);
    print(Ping { .. }.p);
    let n = Nest { .. };
    print(n.d, " ", level);
    print(count(), " ", depth);
}
