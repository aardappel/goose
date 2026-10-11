// pop_n of a negative count is a runtime abort (§9.3) rather than a growth:
// the slots it would add have no values.

fn drop(a: i64[..4]&, n: i32) { a.pop_n(n); }

fn main_pop_n_negative() {
    var a: i64[..4] = [1, 2];
    drop(a, 1);
    print(a);                            // [1]
    drop(a, -1);                         // Aborts.
}

fn main() { main_pop_n_negative(); }
