// runtime-debug: flat resizable arguments own independent worker stacks.
struct Bag { id: i64, data: i64[>..] }
struct Nested { tag: u8, bag: Bag }
struct VariablePrefix { name: u8[], data: i64[>..] }
// A frame object behind a variable-size prefix is bytes of its owner (C.2).
struct PrefixedBag { name: u8[], bag: Bag }
var generation = 3;

thread_fn arrays(var xs: i64[>..], var words: u8[][>..], marker: i64) {
    xs.push(30);
    words.push("worker");
    print(xs, " ", words, " ", marker, " ", generation);
}
thread_fn bags(var value: Nested, var other: VariablePrefix) {
    value.bag.data.push(9);
    other.data.push(8);
    print(value.tag, " ", value.bag.id, " ", value.bag.data, " ", other.name, " ", other.data);
}
thread_fn prefixed(var value: PrefixedBag, var bag: Bag) {
    value.bag.data.push(6);
    bag.data.push(value.bag.id);
    print(value.name, " ", value.bag.id, " ", value.bag.data, " ", bag.id, " ", bag.data);
}
thread_fn child(var xs: i64[>..<]) {
    xs.push(2);
    print(xs);
}
thread_fn parent(var xs: i64[>..<]) {
    xs.push(1);
    thread_wait(thread_spawn(child, copy(xs)));
    print(xs);
}
fn main() {
    var xs: i64[>..] = [10, 20];
    var words: u8[][>..] = ["main"];
    thread_wait(thread_spawn(arrays, copy(xs), copy(words), 17));
    print(xs, " ", words);
    thread_wait(thread_spawn(bags,
        Nested { tag: 5, bag: Bag { id: 7, data: [1, 2] } },
        VariablePrefix { name: "prefix", data: [4] }));
    var kept = PrefixedBag { name: "kept", bag: Bag { id: 11, data: [12] } };
    thread_wait(thread_spawn(prefixed,
        PrefixedBag { name: "sent", bag: Bag { id: 13, data: [14, 15] } }, copy(kept.bag)));
    print(kept.name, " ", kept.bag.id, " ", kept.bag.data);
    thread_wait(thread_spawn(parent, []));
}
