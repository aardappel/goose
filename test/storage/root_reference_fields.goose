// runtime-debug: root fields have ordinary slot semantics, including through descriptors.
struct Holder { value: i64&, text: const u8[:] }
root struct State {
    value: i64&,
    holder: Holder,
    text: const u8[:],
    items: Holder[>..],
    scratch: i64[>..<],
    array: i64[>..]&
}
var number = 3;
var numbers: i64[>..] = [4];
var global = State { number, Holder { number, "hi" }, "world", [], [], numbers };

fn update(s: State&, n: i64&, text: const u8[:]) {
    s.value .= n;
    s.holder = Holder { n, text };
    s.text = text;
    s.items.push(Holder { n, text });
    s.array.push(7);
    s.scratch.push(8);
}
fn read(s: State&) -> i64& { s.holder.value }
fn main() {
    var n = 10;
    var a: i64[>..] = [11];
    var text: u8[>..] = "local";
    var s = State { n, Holder { n, text[..] }, text[..], [], [], a };
    update(s, n, text[..]);
    s.value += 2;
    print(read(s), " ", s.text, " ", s.items[0].value, " ", a[1]);
    update(global, number, "global");
    global.value += 1;
    print(read(global), " ", global.text, " ", numbers[1]);
}
