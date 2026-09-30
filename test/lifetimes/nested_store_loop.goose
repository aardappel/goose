// A nested function, or a function value's block, storing what its call
// hands it into a variable outside it, called in a loop: the variable holds
// what each call passed, not the class the body's check saw, so the loop's
// passes settle, and a view the loop keeps outlives nothing it frees (§7.5).
struct H { s: const u8[:] }
fn put(h: H&, s: const u8[:]) { h.s = s; }
fn once<F>(s: const u8[:]) { F(s[0..1]); }

fn pushes() {
    var buf: u8[>..] = [];
    buf.append("the cat");
    var ws: (const u8[:])[>..] = [];
    fn add(w: const u8[:]) { ws.push(w); }
    for i in 0..3 { add(buf[i..i + 2]); }
    for w in ws { print(w); }
}

fn stores() {
    var buf: u8[>..] = [];
    buf.append("the dog");
    var h = H { s: "" };
    fn add(w: const u8[:]) { h.s = w; }
    for i in 0..3 { add(buf[0..i + 1]); }
    print(h.s);
}

fn through_callee() {
    var buf: u8[>..] = [];
    buf.append("the bird");
    var h = H { s: "" };
    fn add(w: const u8[:]) { put(h, w); }
    for i in 0..3 { add(buf[i..]); }
    print(h.s);
}

fn through_block() {
    var buf: u8[>..] = [];
    buf.append("xyz");
    var h = H { s: "" };
    for i in 0..3 { once(buf[i..]) { s => put(h, s); }; }
    print(h.s);
}

// The loop clears a scratch buffer the stored views never point into.
fn beside_scratch() {
    var buf: u8[>..] = [];
    buf.append("keep");
    var scratch: u8[>..] = [];
    var h = H { s: "" };
    fn add(w: const u8[:]) { put(h, w); }
    for i in 0..2 {
        scratch.clear();
        scratch.append("tmp");
        print(h.s, " ", scratch);
        add(buf[i..]);
    }
    print(h.s);
}

fn main() {
    pushes();
    stores();
    through_callee();
    through_block();
    beside_scratch();
}
