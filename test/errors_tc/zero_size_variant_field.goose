// error: field v of struct S cannot be a variant type with no fields: Mode.Off
// A variant type with no fields takes no bytes, as a payload behind its
// ADT's tag does, but C has no empty structs and gives it one: a field of it
// would add that byte to the struct while its layout counts none (§3.4).
enum Mode { Off, Level { v: i64 } }
struct S { v: Mode.Off, b: i64 }

fn main() {
    var ss: S[>..] = [];
    ss.push(S { v: Mode.Off {}, b: 3 });
    print(ss[0].b);
}
