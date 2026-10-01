// error: field a of struct S cannot be a zero-length array: i64[0]
// C has no empty arrays: a T[0] takes one element there, which a field of
// it would add to the struct while its layout counts none (§3.4).
struct S { a: i64[0], b: i64 }

fn main() {
    let s = S { a: [], b: 3 };
    print(s.b);
}
