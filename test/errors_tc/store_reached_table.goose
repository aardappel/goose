// error: storing a reference rooted at c, which does not outlive the destination: it is reached through a reference that may point into table
// The table c.inputs points at may be the one process is given, an array
// of the type it pushes into: process cannot tell the two apart, and table
// may be further out than the storage the pushed view is into (§9.2).
struct Context { storage: u8[>..]&, inputs: (const u8[:])[>..]& }

fn save(c: Context&, s: const u8[:]) -> const u8[:] {
    let begin = c.storage.len;
    c.storage.append(s);
    return c.storage[begin..];
}

fn process(c: Context&, table: (const u8[:])[>..]&) {
    let name = save(c, table[0]);
    c.inputs.push(name);
}

fn run(table: (const u8[:])[>..]&) {
    var storage: u8[>..] = [];
    var inputs: (const u8[:])[>..] = [];
    var c = Context { storage: storage, inputs: inputs };
    process(c, table);
    print(inputs[0]);
}

fn main() {
    var text: u8[>..] = "argument";
    var table: (const u8[:])[>..] = [text[..]];
    run(table);
}
