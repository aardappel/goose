// error: rec_bytes calls into its recursive cycle while by-value non-fixed-size parameter s
// A by-value non-fixed parameter is in scope for the whole body, so every
// call into the cycle keeps it on its data stack (§7.8).
recursive fn rec_bytes(s: u8[], k: i64) -> i64 {
    if k == 0 { return s.len; }
    return rec_bytes(copy(s), k - 1) + 1;
}
fn main() { print(rec_bytes("hello", 3)); }
