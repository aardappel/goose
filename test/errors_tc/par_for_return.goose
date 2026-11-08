// error: return out of a par for: an iteration ends by running to the end of the body or by continue
// R4 (§4.3): nothing leaves the body but the end of an iteration.
fn first_even(xs: const i64[:]) -> i64 {
    par for &x in xs { if x % 2 == 0 { return x; } }
    return -1;
}

fn main() { print(first_even([1, 3, 4, 5])); }
