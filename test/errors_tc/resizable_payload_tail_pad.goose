// error: resizable field items of variant E.A must be the final field, with no pad after it
// A resizable payload field must be the final one (§3.4), a bare pad after
// it included, which in a variable-class layout inserts nothing.
enum E { A { k: u8, items: u8[>..], pad }, B }

fn main() {
    let e: E.. = E.B {};
    match e { A => print("A"), B => print("B") }
}
