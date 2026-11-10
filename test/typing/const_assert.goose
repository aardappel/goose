// `const assert c, "message";` holds a function to a condition of its
// specialization (§6.5), and `holds_own<T>` is the query that says whether a
// type carries an obligation or has a part that does (§5.5): the two are how
// a library keeps an operation that copies or reuses storage away from
// elements that own something.
struct Plain { n: i64 }
struct Buf { n: i64 }
struct Conn { id: i64, input: own Buf? }
struct Pair<T> { a: T, b: T }
const LIMIT = 4;

fn kind<T>(v: T&) -> u8[:] {
    const if holds_own<T> { return "owns"; } else { return "plain"; }
}

fn fill<T>(dst: T[:], v: T) {
    const assert !holds_own<T>, "fill copies v into every element, which a T that holds own values does not allow";
    for i in dst.len { dst[i] = v; }
}

fn small<T>() -> i64 {
    const assert same<T, u8> || same<T, i64>, "small takes a u8 or an i64";
    const assert LIMIT > 2 && LIMIT * 2 == 8, "LIMIT is 4";
    const if same<T, u8> { return 1; } else { return 8; }
}

fn main() {
    var p = Plain { n: 1 };
    var c = Conn { id: 1, input: null };
    var pc = Pair<Conn> { a: Conn { id: 2, input: null }, b: Conn { id: 3, input: null } };
    var n = 3;
    print(kind(p), " ", kind(c), " ", kind(pc), " ", kind(n));
    var xs = [0, 0, 0];
    fill(xs, 7);
    print(xs, " ", small<u8>(), " ", small<i64>());
    const assert true, "never said";
    // `assert` after `const` begins an assertion only where a condition
    // follows: it is still a name a constant can have.
    const assert = 5;
    print(assert);
}
