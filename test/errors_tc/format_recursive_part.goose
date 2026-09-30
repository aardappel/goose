// error: print cannot render Item, which reaches itself through references, around the format overload for Money: give Item a format overload of its own
// A type that reaches itself through references renders structurally only
// where no format overload renders a part of it (§3.7).
struct Money { cents: i64 }
fn format(out: u8[>..]&, m: Money) { format(out, "$", m.cents); }
struct Item { price: Money, next: Item? }
fn main() {
    var b = Item { price: Money { cents: 2 }, next: null };
    let a = Item { price: Money { cents: 1 }, next: b };
    print(a);
}
