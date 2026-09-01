// §6.1: u64 may meet a signed operand only where the compiler knows it is
// non-negative. A parameter is not, so this needs an explicit cast.

fn cmp(h: u64, k: i64) -> bool { h < k }

fn main() { print(cmp(1 as u64, 2)); }
