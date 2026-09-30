// error: expected a value of type u8, got i64 (narrowing and sign changes require an explicit `as`)
// A typed count naming a constant is a value like any other (§6.5): 10 / Z
// is an i64 whose division runs at run time, whatever Z holds (§6.2), and
// narrows to the u8 binder only with `as`.
let Z = 0;

fn main() {
    for i: u8 in 10 / Z {
        print(i);
    }
}
