// runtime-debug: byte walks decode reference members at each field address.
struct Node { value: i64, next: Node&<varint>? }
enum Item { End, Link { value: i64, next: Item..&<u16>? } }
enum VItem { End, Link { value: i64, next: VItem..&<varint>? } }
struct Mixed { values: u8[], next: Mixed&<u32>? }

fn main() {
    var ns: Node[>..] = [];
    let ns0 .= ns.push(Node { value: 2, next: null });
    let ns1 .= ns.push(Node { value: 2, next: ns0 });
    let ns2 .= ns.push(Node { value: 2, next: ns0 });
    let ns3 .= ns.push(Node { value: 2, next: ns2 });
    let ns4 .= ns.push(Node { value: 2, next: null });
    let ns5 .= ns.push(Node { value: 3, next: ns0 });
    print("targets ", ns1.next .== ns2.next, " ", ns1.next .== ns3.next);
    print("records ", ns1 == ns2, " ", ns1 == ns3, " ", ns1 != ns2, " ", ns1 != ns3);
    print("controls ", ns0 == ns4, " ", ns1 == ns1, " ", ns1 == ns4, " ", ns4 == ns1, " ", ns1 == ns5);
    print("ranges ", ns == ns);

    var items: Item..[>..] = [];
    let items0 .= items.push(Item.End);
    let items1 .= items.push(Item.Link { value: 2, next: items0 });
    let items2 .= items.push(Item.Link { value: 2, next: items0 });
    let items3 .= items.push(Item.Link { value: 2, next: items2 });
    match items1 {
        Link &a => match items2 {
            Link &b => print("adt targets ", a.next .== b.next),
            End => {},
        },
        End => {},
    }
    print("adt ", items1 == items2, " ", items1 == items3, " ", items0 == items1);
    match items1 {
        Link &a => match items2 {
            Link &b => print("variant ", a == b),
            End => {},
        },
        End => {},
    }

    var vs: VItem..[>..] = [];
    let vs0 .= vs.push(VItem.End);
    let vs1 .= vs.push(VItem.Link { value: 2, next: vs0 });
    let vs2 .= vs.push(VItem.Link { value: 2, next: vs0 });
    let vs3 .= vs.push(VItem.Link { value: 2, next: vs2 });
    match vs1 {
        Link &a => match vs2 {
            Link &b => print("varint adt targets ", a.next .== b.next),
            End => {},
        },
        End => {},
    }
    print("varint adt ", vs1 == vs2, " ", vs1 == vs3);

    var mixed: Mixed[>..] = [];
    let mixed0 .= mixed.push(Mixed { values: [1, 2], next: null });
    let mixed1 .= mixed.push(Mixed { values: [1, 2], next: mixed0 });
    let mixed2 .= mixed.push(Mixed { values: [1, 2], next: mixed0 });
    let mixed3 .= mixed.push(Mixed { values: [1, 2], next: mixed2 });
    print("mixed targets ", mixed1.next .== mixed2.next, " ", mixed1.next .== mixed3.next);
    print("mixed ", mixed1 == mixed2, " ", mixed1 == mixed3);
}
