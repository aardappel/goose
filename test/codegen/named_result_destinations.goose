// A named variable-size local is built at the destination it is returned in.
fn one() -> u8[] { let s: u8[] = "fgh"; return s; }
fn two_diff() -> u8[], u8[] { let s: u8[] = "fgh"; let t: u8[] = "ij"; return s, t; }
fn two_first() -> u8[], i64 { let s: u8[] = "fgh"; return s, 5; }
fn two_same() -> u8[], u8[] { let s: u8[] = "fgh"; return s, s; }
fn named_bytes() {
    print(one());
    let a, b = two_diff();
    print(a, " ", b);
    let c, n = two_first();
    print(c, " ", n);
    let d, e = two_same();
    print(d, " ", e);
}

// Named results and other values bound for their destination: a return of a
// copy from a function value's body, run inside a callee, and a long-distance
// return land over the named result built there (build, grow); a local
// returned in two result positions is built at neither.
fn each<F>(n: i64) { for i in n { F(i); } }
fn build(n: i64) -> u8[] {
    var s: u8[>..] = [];
    s.push('a');
    each(n) { i =>
        s.push('b');
        if i == 1 { return copy(s); }
    }
    s.push('z');
    return s;
}
fn leave() { return [7, 8, 9] from grow; }
fn grow(n: i64) -> i64[>..] {
    var out: i64[>..] = [1, 2];
    out.push(3);
    if n > 0 { leave(); }
    return out;
}
fn two() -> u8[>..], u8[>..] {
    var s: u8[>..] = [];
    s.push('a');
    s.push('b');
    return s, s;
}
fn two_arr() -> u8[], u8[] {
    var s: u8[>..] = [];
    s.push('c');
    s.push('d');
    return s, s;
}
fn exclusions() {
    print(build(5), " ", build(0));
    var g = grow(1);
    g.push(4);
    print(g, " ", grow(0));
    let a, b = two();
    print(a, " ", b);
    let c, d = two_arr();
    print(c, " ", d);
}

// An inlined body's implicit return must land over its named early result.
struct Result { code: i32, out: u8[] }
fn run(reject: bool) -> Result {
    var result = Result { code: 1, out: "rejected" };
    if reject { return result; }
    var out: u8[>..] = "success";
    Result { code: 0, out: out }
}
fn command(reject: bool) -> Result { run(reject) }
fn array(reject: bool) -> u8[] {
    var result: u8[>..] = "early";
    if reject { return result; }
    "tail"
}
fn explicit(reject: bool) -> Result {
    var result = Result { code: 2, out: "early" };
    if reject { return result; }
    return Result { code: 3, out: "explicit" };
}
fn inlined_tail() {
    for i in 2 {
        let reject = i == 1;
        let result = command(reject);
        assert(result.code == (if reject { 1 } else { 0 }));
        assert(result.out == (if reject { "rejected" } else { "success" }));
        print(result.code, " ", result.out);
        var receiver: u8[>..] = "prefix:";
        receiver.append(array(reject));
        assert(receiver == (if reject { "prefix:early" } else { "prefix:tail" }));
        print(receiver);
        let other = explicit(reject);
        assert(other.code == (if reject { 2 } else { 3 }));
        assert(other.out == (if reject { "early" } else { "explicit" }));
        print(other.code, " ", other.out);
    }
}

fn main() {
    named_bytes();
    exclusions();
    inlined_tail();
}
