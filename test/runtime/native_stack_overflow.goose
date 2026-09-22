// Recursion deeper than the thread's native stack holds is a safe abort
// (§9.3): the runtime reports it rather than the process dying of the fault.
// Expected to abort.
//
// No loop can take the recursion's place: the call's result indexes an array
// that has to outlive the call, so neither Goose's accumulator loops nor a C
// compiler's tail calls apply, and whether the base case is ever reached
// depends on values no compiler follows.

recursive fn descend(n: i64) -> i64 {
    if n < 0 { return 0; }                          // Never true.
    let a: i64[4] = [n, n * 3 + 1, n ^ 5, n * 7];
    let r = descend(a[n % 4] % 1000 + 1);
    r + a[r % 4]
}

fn main() {
    print("descending");
    print(descend(1));                              // Aborts.
}
