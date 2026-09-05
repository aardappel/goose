// Records with variable-size parts, kept flat. An order has a customer
// name and a list of items, each with a SKU string: in most languages that
// is a struct owning a string and a vector of structs owning strings, an
// allocation per part. Here every order is one contiguous run of bytes --
// varint lengths and numbers, strings inline, items inline -- and the
// whole order book is one array of them. The report groups totals per
// customer with a dictionary keyed by slices into the orders.
//
// Build and run (from this directory):
//   goose -o records.c records.goose && cl records.c && records

import std;
import dictionary;

struct Item { sku: u8[varint], qty: varint, cents: varint }
struct Order { id: varint, customer: u8[varint], items: Item[varint] }
struct Row { name: u8[:], total: i64, orders: i32 }

// Money renders as dollars everywhere it is printed.
struct Money { cents: i64 }
fn format(out: u8[>..]&, m: Money) {
    format(out, "$", m.cents / 100, ".");
    format_int(out, m.cents % 100, 10, 2, '0');
}

let CSV = "1001,alice,SKU-441:2:1999;SKU-7:1:500\n1002,bob,SKU-12:10:250\n1003,alice,SKU-7:3:500;SKU-900:1:12999;SKU-441:1:1999\n1004,carol,SKU-12:1:250;SKU-13:1:275\n1005,bob,SKU-900:2:12999\n1006,dave,SKU-1:1:99\n1007,carol,SKU-441:4:1999;SKU-7:2:500;SKU-12:2:250;SKU-13:2:275\n";

// "SKU:qty:cents;..." into items, built straight into the order's field.
fn parse_items(field: u8[:]) -> Item[>..] {
    var items: Item[>..] = [];
    each_split(field, ';') {
        let parts = split(it, ':');
        assert(parts.len == 3);
        let qty, ok1 = parse_int(parts[1]);
        let cents, ok2 = parse_int(parts[2]);
        assert(ok1 && ok2);
        items.push(Item { sku: parts[0], qty: qty, cents: cents });
    };
    return items;
}

fn order_total(o: Order&) -> i64 {
    var t = 0;
    for item in o.items { t += item.qty * item.cents; }
    return t;
}

fn main() {
    // Parse: one record per line, each a variable-size value pushed onto
    // the book. Slices of the CSV text feed the string fields, which copy
    // the bytes into place; the items are built directly into their field.
    var book: Order[>..] = [];
    each_split(CSV, '\n') {
        guard it.len > 0;
        let f = split(it, ',');
        let id, ok = parse_int(f[0]);
        assert(ok && f.len == 3);
        book.push(Order { id: id, customer: f[1], items: parse_items(f[2]) });
    };

    // Walk the book: sequential, by reference, nothing copied out.
    var grand = 0;
    var items = 0;
    var biggest = 0;
    var biggest_id = 0;
    for o in book {
        let t = order_total(o);
        grand += t;
        items += o.items.len;
        if t > biggest { biggest = t; biggest_id = o.id; }
    }
    print(book.len, " orders, ", items, " items, ", Money { grand }, " in total");
    print("biggest order #", biggest_id, " at ", Money { biggest });
    for o in book { if o.id == biggest_id { print(o); } }

    // Per customer. The keys are the customer strings inside the book.
    var per = dictionary<u8[:], i64> {};
    for o in book { per.update(o.customer[..], 0) { it += order_total(o); }; }
    var rows: Row[>..] = [];
    per.each() { name, total => rows.push(Row { name: name, total: total, orders: 0 }); };
    for &r in rows {
        for o in book { if o.customer == r.name { r.orders += 1; } }
    }
    sort(rows) { a, b => a.total > b.total };
    print("");
    print("customer  orders  total");
    for r in rows {
        var line = str(r.name);          // a fresh builder per row, gone at the brace
        while line.len < 10 { line.push(' '); }
        format_int(line, r.orders, 10, 6, ' ');
        format(line, "  ", Money { r.total });
        print(line);
    }

    // Per SKU: quantities sold.
    var sold = dictionary<u8[:], i64> {};
    for o in book {
        for item in o.items { sold.update(item.sku[..], 0) { it += item.qty; }; }
    }
    var skus: Row[>..] = [];
    sold.each() { sku, qty => skus.push(Row { name: sku, total: qty, orders: 0 }); };
    sort(skus) { a, b => a.total > b.total || (a.total == b.total && compare(a.name, b.name) < 0) };
    print("");
    for s in skus { print(s.name, ": ", s.total, " sold"); }
}
