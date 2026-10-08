// runtime-debug: A type that reaches itself through references renders
// below its first level by a function of its own (§3.7), which takes no
// stack index: it opens no data stacks and runs no format overload. The
// overloads a rendering does run, those building their text on stacks of
// their own included, are called from the function printing, above the
// stacks it has live -- its locals' and the builder's -- and an overload
// rendering a level of its own type calls the next level above its own.

struct Money { cents: i64 }
fn format(out: u8[>..]&, m: Money) {
    var whole: u8[>..] = [];
    format(whole, m.cents / 100);
    var frac: u8[>..] = [];
    format(frac, m.cents % 100);
    format(out, "$", whole, ".", frac);
}

struct N { v: i64, next: N? }

struct Item { price: Money, next: Item? }
// The price's text, built aside first: the overload below keeps no
// non-fixed local of its own across the call into its cycle (§7.8).
fn head(out: u8[>..]&, i: Item&) {
    var text: u8[>..] = [];
    format(text, "(", i.price);
    format(out, text);
}
recursive fn format(out: u8[>..]&, i: Item&) {
    head(out, i);
    let n .= i.next;
    if n { format(out, " ", n); }
    format(out, ")");
}

struct Outer { id: i64, m: Money, list: N, items: Item }

fn main() {
    var nums: i64[>..] = [1, 2, 3];
    var name: u8[>..] = [];
    format(name, "outer");
    var n2 = N { v: 2, next: null };
    var n1 = N { v: 1, next: n2 };
    var c = Item { price: Money { cents: 305 }, next: null };
    var b = Item { price: Money { cents: 1250 }, next: c };
    let o = Outer { id: 7, m: Money { cents: 99 }, list: N { v: 0, next: n1 },
                    items: Item { price: Money { cents: 8 }, next: b } };
    let s = str(o);
    nums.push(4);
    format(name, "!");
    print(s, " ", nums, " ", name);
    print(o, " ", [o.items, b], " ", o.list);
    var buf: u8[>..] = [];
    format(buf, nums, " ", o.items, " ", o.list, " ", name);
    print(buf);
}
