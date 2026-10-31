// Contracts are checked only where GS_CONTRACTS is on (debug builds, or
// --check-contracts). This release build runs on past the false ones.

fn next_up(x: i64) -> i64
    requires x >= 0
    ensures result > x
{
    x
}

fn main() {
    print(next_up(-1));
    var n: i64 = 3;
    while n > 0 invariant n > 5 { n--; }
    print(n);
}
