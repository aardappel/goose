// runtime-define: GS_CONTRACTS=1
// A failed requires aborts with its clause before the body runs. Expected to
// abort after the first two lines.

fn halve(n: i64) -> i64
    requires n % 2 == 0
{
    print("halving ", n);
    n / 2
}

fn main() {
    print(halve(10));
    print(halve(7));
}
