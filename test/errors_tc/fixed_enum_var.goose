// Fixed-mode use requires all payloads fixed-size (§3.5).
enum E { A { s: u8[] }, B }
fn main() {
    let e: E = E.B {};
    print(match e { A a => a.s.len, B => 0 });
}
