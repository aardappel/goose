// error: continue cannot leave a defer body (§6.6)
fn main() {
    for i in 3 {
        defer if i == 1 { continue; }
        print(i);
    }
}
