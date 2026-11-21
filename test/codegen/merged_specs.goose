// Specializations whose C is the same but for the names of the functions
// they call and the numbers of their long-distance returns are written once
// (CodeGen::MergeSpecs). Each function here is specialized twice, for a
// slice rooted at a global and for one rooted at a local, and run_tests.py
// counts one C function of each in the -O0 C, where nothing is inlined: a
// plain function, two functions calling each other, a long-distance return
// target, a target inside another that passes the outer one's returns on, a
// simd function with its versions, and an element-run twin (C.3). A
// function given two blocks is specialized for each, and those copies,
// whose C differs in a constant, stay two. The runs check that the calls
// the merge redirected, and the long-distance returns it renumbered, still
// land where they did.
var table: i64[>..] = [1, 2, 3, 4, 5];

fn total(xs: i64[:]) -> i64 {
    var s = 0;
    for x in xs { s += x * 3; }
    return s;
}

recursive fn ping(xs: i64[:], acc: i64) -> i64 {
    if xs.len == 0 { return acc; }
    return pong(xs[1..], acc + xs[0]);
}
recursive fn pong(xs: i64[:], acc: i64) -> i64 {
    if xs.len == 0 { return acc; }
    return ping(xs[1..], acc * 2 + xs[0]);
}

fn reject(x: i64) {
    if x < 0 { return x from scan; }
}
fn check(xs: i64[:]) {
    for x in xs { reject(x); }
}
fn scan(xs: i64[:]) -> i64 {
    check(xs);
    return xs.len;
}

fn outer(xs: i64[:], stop: i64) -> i64 {
    fn inner(ys: i64[:]) -> i64 {
        fn leaf(y: i64) {
            if y == stop { return 100 + y from outer; }
            if y < 0 { return y from inner; }
        }
        for y in ys { leaf(y); }
        return 1;
    }
    return inner(xs) + 1000;
}

simd fn wide_sum(xs: const i64[:]) -> i64 {
    var s = 0;
    for x in xs { s += x; }
    return s;
}

fn doubled(xs: i64[:]) -> i64[] {
    var out: i64[>..] = [];
    for x in xs { out.push(x * 2); }
    return out;
}

fn mapped<F>(xs: i64[:]) -> i64 {
    var s = 0;
    for x in xs { s += F(x); }
    return s;
}

fn main() {
    // Built from the global, so that no copy knows a length the other
    // does not (BCE's loop bounds).
    var local: i64[>..] = [];
    for x in table { local.push(x * 3 - 7); }
    print(total(table), " ", total(local));
    print(ping(table, 0), " ", ping(local, 0));
    print(scan(table), " ", scan(local));
    print(outer(table, 3), " ", outer(table, 99), " ", outer(local, 5), " ", outer(local[2..], 5));
    print(wide_sum(table), " ", wide_sum(local));
    var all: i64[>..] = [];
    all.append(doubled(table));
    all.append(doubled(local));
    print(all.len, " ", all[0], " ", all[all.len - 1]);
    print(mapped(table) { x => x * 2 }, " ", mapped(local) { x => x * 5 });
}
