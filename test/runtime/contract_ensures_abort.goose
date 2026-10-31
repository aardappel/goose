// runtime-define: GS_CONTRACTS=1
// A failed ensures aborts with its clause where contracts are checked: here
// in every build, by the define that --check-contracts passes. Expected to
// abort after the first line.

fn next_up(x: i64) -> i64
    ensures result > x
{
    if x > 10 { return x; }
    x + 1
}

fn main() {
    print(next_up(3));
    print(next_up(20));
}
