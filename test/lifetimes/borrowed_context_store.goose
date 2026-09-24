// run owns the byte and input tables its context borrows, and process builds
// an Input holding a view of the byte table in the input table. The element
// lands in what c.inputs points at, an Input[>..], which the argument list
// process is also given cannot hold, whatever else it can: it is no storage
// the element may be in, and the store passes (§9.2).
struct Input { text: const u8[:] }
struct Context { storage: u8[>..]&, inputs: Input[>..]& }

fn save(c: Context&, s: const u8[:]) -> const u8[:] {
    let begin = c.storage.len;
    c.storage.append(s);
    return c.storage[begin..];
}

fn process(c: Context&, arguments: const (const u8[:])[:]) {
    let name = save(c, arguments[0]);
    c.inputs.push(Input { text: name });
    print(arguments[0]);
}

fn run(arguments: const (const u8[:])[:]) {
    var storage: u8[>..] = [];
    var inputs: Input[>..] = [];
    var c = Context { storage: storage, inputs: inputs };
    process(c, arguments);
    print(inputs[0].text);
}

fn main() {
    var argument_text: u8[>..] = "argument";
    var arguments: (const u8[:])[>..] = [argument_text[..]];
    run(arguments);
}
