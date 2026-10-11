// error: call process stores a reference rooted at storage into what its parameter c leads to, which may be outer, which storage does not outlive (§9.2)
// A view of what a struct's reference field refers to, made by a call, and
// stored into a table another of its fields refers to, is a store like any
// other: that table may outlive the array viewed (§9.2).
struct Context { storage: u8[>..]&, inputs: (const u8[:])[>..]& }
fn key(storage: u8[>..]&) -> const u8[:] {
    let b = storage.len;
    storage.push(65);
    return storage[b..];
}
fn process(c: Context&) {
    let name = key(c.storage);
    c.inputs.push(name);
}
fn main() {
    var outer: (const u8[:])[>..] = ["outer"];
    {
        var storage: u8[>..] = [];
        var c = Context { storage: storage, inputs: outer };
        process(c);
    }
    print(outer[1]);
}
