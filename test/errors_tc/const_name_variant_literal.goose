// error: array size K names a local variable, not a constant
// As is a variant literal's (§3.3, §3.5).
let K = 3;

enum E<T> { A { t: T }, B }

fn main() {
    let K = 5;
    let e = E<i64[K]>.A { t: [1, 2, 3] };
    print(e.t.len, " ", K);
}
