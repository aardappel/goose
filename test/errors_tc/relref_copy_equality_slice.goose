// error: its links, read out of this copy, would be measured from the copy
struct Node { next: Node&<u32>? }

fn main() {
    var rows: Node[1][>..] = [];
    rows.push([Node { next: null }]);
    let other = [Node { next: null }];
    print(rows.pop() == other[..]);
}
