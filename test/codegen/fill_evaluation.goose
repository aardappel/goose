var calls = 0;
fn tick() -> i64 { calls++; return calls; }
struct Node { value: i64, next: Node&<u8> }
struct Text { value: i64, text: u8[] }
fn text() -> u8[] { return str(tick()); }

fn main() {
    var fixed: Node[3] = [Node { tick(), self }; 3];
    var grow: Node[>..] = [Node { tick(), self }; 3];
    var limited: Node[..4] = [Node { tick(), self }; 3];
    var dynamic: Node[..] = [Node { tick(), self }; 3];
    for &n in fixed { print(n.value, n.next.value); }
    for &n in grow { print(n.value, n.next.value); }
    for &n in limited { print(n.value, n.next.value); }
    for &n in dynamic { print(n.value, n.next.value); }
    let strings: u8[][] = [text(); 3];
    let records: Text[] = [Text { tick(), text() }; 2];
    print(strings, records, calls);
    let empty: Node[0] = [Node { tick(), self }; 0];
    let emptytext: u8[][] = [text(); 0];
    print(empty.len, emptytext.len, calls);
    var nested: Node[2][2] = [[Node { tick(), self }; 2]; 2];
    print(nested[0][0].value, nested[1][1].next.value, calls);
    grow.append([Node { tick(), grow[0] }; 3]);
    for i in 3..6 { print(grow[i].value, grow[i].next.value); }
}
