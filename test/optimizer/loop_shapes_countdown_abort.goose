// runtime-debug: a `while v > 0 { v--; ... }` countdown run as a counted loop (optimize_loops.h) aborts at the same value of its counter as the loop as written.

fn walk(xs: i64[:], n: i64) -> i64 {
    var i = n;
    var s = 0;
    while i > 0 {
        i--;
        print(i);
        s += xs[i * 3 % 7];
    }
    return s;
}

fn main() {
    let xs: i64[6] = [1, 2, 3, 4, 5, 6];
    print(walk(xs, 2));
    print(walk(xs, 5));
}
