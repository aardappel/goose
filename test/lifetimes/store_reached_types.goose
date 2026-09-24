// A store through a reference read out of a borrowed context goes into what
// that reference points at, which whatever owns the slot holds (§9.2). names
// can hold a const u8[:], log an Input and a const Input?, and extra views
// Inputs, but none of them can hold what c leads to: no store into the
// context's tables may land in them, however the destination is built or
// reached, and each passes.
struct Input { text: const u8[:] }
struct Entry { input: Input, n: i64 }
struct Node { name: const u8[:], from: const Input? }
struct Item { name: const u8[:] }
struct Tables { count: i64, inputs: Input[>..] }
struct Log { first: Input, last: const Input? }
struct Context {
    storage: u8[>..]&,
    inputs: Input[>..]&,
    entries: Entry[>..]&,
    node: Node&,
    items: Item[:],
    tables: Tables&,
}

fn save(c: Context&, s: const u8[:]) -> const u8[:] {
    let begin = c.storage.len;
    c.storage.append(s);
    return c.storage[begin..];
}

fn process(c: Context&, names: const (const u8[:])[:], log: Log&, extra: const Input[:]) {
    // An element built in a table, and one copied into it.
    c.inputs.push(Input { text: save(c, names[0]) });
    let local = Input { text: save(c, "local") };
    c.inputs.push(local);
    // A run appended as a literal, and one copied out of what extra views.
    c.inputs.append([Input { text: save(c, "run") }]);
    c.inputs.append(extra);
    // A literal nested in the element built.
    c.entries.push(Entry { input: Input { text: save(c, "nested") }, n: 1 });
    // What a reference read out of the context points at, assigned whole, a
    // field of it assigned and rebound, and an element's field through a
    // slice read out of the context.
    c.node = Node { name: save(c, "whole"), from: null };
    print(c.node.name);
    c.node.name = save(c, "assigned");
    c.node.from .= c.inputs[1];
    c.items[0].name = save(c, "item");
    // An array in what a reference read out of the context points at.
    c.tables.inputs.push(Input { text: save(c, "tables") });
    print(log.first.text);
}

fn run(names: const (const u8[:])[:], log: Log&) {
    var storage: u8[>..] = [];
    var inputs: Input[>..] = [];
    var entries: Entry[>..] = [];
    var node = Node { name: "none", from: null };
    var items: Item[2] = [Item { name: "i0" }, Item { name: "i1" }];
    var tables = Tables { count: 0, inputs: [] };
    var extra: Input[>..] = [Input { text: "extra" }];
    var c = Context { storage: storage, inputs: inputs, entries: entries, node: node,
                      items: items[..], tables: tables };
    process(c, names, log, extra[..]);
    for i in inputs { print(i.text); }
    print(entries[0].input.text, " ", entries[0].n);
    print(node.name);
    let from = node.from;
    if from { print(from.text); }
    print(items[0].name, " ", items[1].name);
    print(tables.inputs[0].text);
}

fn main() {
    var text: u8[>..] = "argument";
    var names: (const u8[:])[>..] = [text[..]];
    var log = Log { first: Input { text: "first" }, last: null };
    run(names, log);
}
