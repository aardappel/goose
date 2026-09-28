// error: storing a reference rooted at <temporary>, which does not outlive the destination
// An array literal viewed by a field of a value bound to a variable dies with
// its statement (§3.10, §9.2).
struct H { s: const i64[:], n: i64 }
fn main() {
    let h = H { s: [1, 2], n: 0 };
    print(h.s);
}
