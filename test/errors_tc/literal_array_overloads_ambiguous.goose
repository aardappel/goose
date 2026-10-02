// error: ambiguous call to pick: multiple overloads match equally well
// An array literal of constants fits u8[2] and i16[2] alike, each a
// coercion (§7.1), as the constant 1 fits u8 and i16.
fn pick(c: u8[2]) -> i64 { 8 }
fn pick(c: i16[2]) -> i64 { 16 }
fn main() {
    print(pick([1, 2]));
}
