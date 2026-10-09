// error: no matching overload for call to twice
// The nested functions of a name in an inner scope hide the outer scope's
// set of that name entirely (§11.1).
fn main() {
    fn twice(x: i64) -> i64 { x * 2 }
    {
        fn twice(s: u8[:]) -> i64 { s.len * 2 }
        print(twice(21));
    }
}
