// A reference or slice read out of a field, an element or a global never
// points into a grow-shrink array, since none is ever stored there (§5.2),
// so a shrink of one passes over it, whatever the read-back rule says the
// read may point into: over a view sliced or referenced from it too, a call
// result every return of which is one, and a branch value both of whose
// branches are. Every shrink below was rejected as if the view might point
// into the array it shrinks.
struct Entry { text: const u8[:] }
struct Word { text: u8[:] }
struct Node { v: i64 }
struct Link { to: Node& }

var chars: u8[>..] = [];
var entries: Entry[>..] = [];
var words: Word[>..] = [];
var unrelated: i64[>..<] = [];
var related: u8[>..<] = [];
var base: u8[2] = [110, 111];
var shown: u8[:] = base[..];

// The recursive call counts as shrinking every grow-shrink global, the
// integer one included, while `name` is live.
recursive fn visit(depth: i64) {
    let name = entries[0].text;
    if depth > 0 { visit(depth - 1); }
    print(name);
}

// A const u8 view out of a global slot may be a byte view of any storage,
// the integer array's too, as far as the read-back rule knows.
fn clear_unrelated() {
    let name = entries[0].text;
    unrelated.clear();
    print(name);
}

// A writable u8 view out of a global table, beside a grow-shrink array of
// u8 it could view by its type.
fn clear_related() {
    let name = words[0].text;
    related.clear();
    print(name);
}

fn text_of(i: i64) -> const u8[:] { return entries[i].text; }

fn pair(a: const u8[:], n: u8) { print(a, " ", n); }

// Every array a local: the read-back candidates are the locals that can
// hold the elements, the grow-shrink array among them.
fn locals() {
    var text: u8[>..] = [];
    var table: Word[>..] = [];
    var spare: u8[>..<] = [];
    text.append("abcd");
    table.push(Word { text: text[0..2] });
    table.push(Word { text: text[2..4] });
    spare.push(1);
    let name = table[0].text;
    let c = spare.len > 0;
    let either = if c { table[0].text } else { table[1].text };
    let part = table[1].text[1..];
    spare.clear();
    print(name, " ", either, " ", part);
    // A `for` binder copies each view out of an element.
    var views: const u8[:][>..] = [];
    views.push(text[1..3]);
    views.push(text[3..4]);
    for v in views {
        spare.push(2);
        spare.clear();
        print(v);
    }
    // A reference out of a field, beside a grow-shrink array of its pointee.
    var nodes: Node[>..] = [];
    nodes.push(Node { v: 5 });
    var links: Link[>..] = [];
    links.push(Link { to: nodes[0] });
    var others: Node[>..<] = [];
    others.push(Node { v: 6 });
    let n: Node& = links[0].to;
    others.clear();
    print(n.v);
}

// A var bound only to slot reads, outside any loop: what it holds at each
// shrink is on its record. Its root is deeper than spare, so no rebind
// could have retargeted it there either.
fn rebound() {
    var spare: u8[>..<] = [];
    spare.push(7);
    {
        var text: u8[>..] = [];
        var table: Word[>..] = [];
        text.append("wxyz");
        table.push(Word { text: text[0..2] });
        table.push(Word { text: text[2..4] });
        var s = table[0].text;
        spare.clear();
        print(s);
        s = table[1].text;
        spare.push(8);
        spare.clear();
        print(s);
    }
}

fn main() {
    chars.append("okay");
    entries.push(Entry { text: chars[0..2] });
    entries.push(Entry { text: chars[2..4] });
    words.push(Word { text: chars[1..3] });
    unrelated.push(3);
    related.push(7);
    visit(1);
    clear_unrelated();
    clear_related();
    // A call result, and a view held by the call it is an argument of while
    // the next argument pops.
    related.push(7);
    let t = text_of(1);
    related.clear();
    print(t);
    related.push(8);
    pair(entries[0].text, related.pop());
    // A global view variable, and a slice of it.
    shown = chars[..];
    related.push(9);
    let g = shown;
    let tail = shown[3..];
    related.clear();
    print(g, " ", tail);
    locals();
    rebound();
}
