// error: call process stores a reference rooted at c into what its parameter c leads to, which may be outer, which c does not outlive
// process cannot tell that arguments views the table c.inputs points at,
// and need not: what it pushes goes into a table c leads to, which the call
// checks against every table that may be (§9.2). outer, one of them,
// outlives the storage the pushed view is into, which ends with its block.
struct Context { storage: u8[>..]&, inputs: (const u8[:])[>..]& }

fn save(c: Context&, s: const u8[:]) -> const u8[:] {
    let begin = c.storage.len;
    c.storage.append(s);
    return c.storage[begin..];
}

fn process(c: Context&, arguments: const (const u8[:])[:]) {
    let name = save(c, arguments[0]);
    c.inputs.push(name);
}

fn main() {
    var outer: (const u8[:])[>..] = ["outer"];
    {
        var storage: u8[>..] = [];
        var c = Context { storage: storage, inputs: outer };
        process(c, outer[..]);
    }
    print(outer[1]);
}
