// A reference bound only inside a loop takes its root from a scan of the
// loop's rebinds (§9.2), which here reads a recursive function's return
// root before anything has called it. The cycle's prediction (§7.8) is
// settled all the same, so the back edge's result can still be returned.
recursive fn first_at_least(p: i64[>..]&, i: i64, k: i64) -> i64& {
    if i + 1 < p.len && p[i] < k { return first_at_least(p, i + 1, k); }
    return p[i];
}
fn main() {
    var pool: i64[>..] = [3, 8, 5, 13];
    var r: i64&? = null;
    for k in 2 {
        r .= first_at_least(pool, 0, k * 6 + 4);
        if r { print(r, " "); }
    }
    if r { r += 1; }
    print(pool[3]);
}
