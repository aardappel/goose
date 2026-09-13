// error: comparing u64 with i64 needs the signed side to be known non-negative (a literal, .len/.cap, or a `let` bound to one); convert it with `as` otherwise
// §6.1: u64 may meet a signed operand only where the compiler knows it is
// non-negative. A parameter is not, so this needs an explicit cast.

fn cmp(h: u64, k: i64) -> bool { h < k }

fn main() { print(cmp(1 as u64, 2)); }
