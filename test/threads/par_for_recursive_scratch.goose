// goose-threads
// A recursive function whose par for keeps a large fixed local across its
// call back into the cycle (§7.8): the outlined body is the cycle's, as the
// function's body is, so the local stays native, as it does in a plain loop
// (CodeGen::OutlineParLoop).
recursive fn f(n: i64) -> i64 {
    if n == 0 { return 1; }
    var t = 0;
    par for k in 2 {
        let scratch: i64[1024] = [n; 1024];
        t += f(n - 1) + scratch[k] - n;
    }
    return t;
}

fn main() {
    print(f(3));
    print(f(6));
}
