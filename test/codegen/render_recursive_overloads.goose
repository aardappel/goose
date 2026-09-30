// runtime-debug: A type that reaches itself through references renders
// structurally only where no format overload renders a part of it (§3.7):
// one whose parts have overloads is given an overload of its own, which
// renders the parts through theirs and the level below through itself.
// Overloads elsewhere in the same argument apply as ever.

struct Money { cents: i64 }
fn format(out: u8[>..]&, m: Money) { format(out, "$", m.cents / 100, ".", m.cents % 100); }

struct Item { price: Money, next: Item? }
recursive fn format(out: u8[>..]&, i: Item&) {
    format(out, "(", i.price);
    let n .= i.next;
    if n { format(out, " ", n); }
    format(out, ")");
}

struct N { v: i64, next: N? }
struct Tagged { m: Money, list: N, rest: N? }

fn main() {
    var c = Item { price: Money { cents: 305 }, next: null };
    var b = Item { price: Money { cents: 1250 }, next: c };
    let a = Item { price: Money { cents: 99 }, next: b };
    print(a);                       // ($0.99 ($12.50 ($3.5)))
    print(str(b), " ", [c, c]);     // ($12.50 ($3.5)) [($3.5), ($3.5)]

    var n2 = N { v: 2, next: null };
    var n1 = N { v: 1, next: n2 };
    let t = Tagged { m: Money { cents: 7 }, list: N { v: 0, next: n1 }, rest: n2 };
    print(t);                       // Tagged { $0.7, N { 0, N { 1, N { 2, null } } }, N { 2, null } }
    var buf: u8[>..] = [];
    format(buf, t.m, " ", n1);
    print(buf);                     // $0.7 N { 1, N { 2, null } }
}
