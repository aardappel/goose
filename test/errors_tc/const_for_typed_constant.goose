// error: expected a value of type u8, got i64
// error: in const for constant entry 1 (WIDE)
// A typed constant bound by const for keeps its written type, i64, and does
// not adapt as a literal would.
const WIDE: i64 = 1;
fn main() { const for <C> in [WIDE] { let n: u8 = C; print(n); } }
