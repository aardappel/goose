// runtime-debug all-levels
import slot_pool;
import os;

root struct S { first: i64[>..] = [], second: i64[>..] = [] }
struct H { s: S& }
struct HH { h: H }
var global = S { .. };
var held = H { global };
fn get() -> S& { global }
fn mixed(p: S&, h: H&) {
    p.first.push(4); h.s.first.push(5); p.first.push(6);
}
recursive fn recurse(p: S&, h: H&, depth: i64) {
    p.second.push(depth);
    if depth > 0 { recurse(p, h, depth - 1); }
    h.s.second.push(10 + depth);
}
struct PH { p: slot_pool<i64>& }
var pool = slot_pool<i64> {};
var ph = PH { pool };

root struct Refs { items: i64&[>..] = [], spare: i64[>..] = [] }
struct RH { r: Refs& }
root struct Bytes { items: u8[>..] = [], spare: u8[>..] = [] }
struct BH { b: Bytes& }

fn main() {
    var s = S { .. }; var h = H { s }; var nested = HH { h };
    var owners: S&[1] = [s];
    s.first.push(1); h.s.first.push(2); s.first.push(3);
    mixed(s, h);
    nested.h.s.first.push(7); owners[0].first.push(8); s.first.push(9);
    assert(s.first == [1, 2, 3, 4, 5, 6, 7, 8, 9]);
    global.first.push(1); held.s.first.push(2); global.first.push(3);
    mixed(global, held);
    get().first.push(7); global.first.push(8);
    assert(global.first == [1, 2, 3, 4, 5, 6, 7, 8]);
    recurse(s, h, 2);
    assert(s.second == [2, 1, 0, 10, 11, 12]);
    assert(global.second.len == 0);

    var loop_owner = S { .. }; var lh = H { loop_owner }; loop_owner.first.push(1);
    var i = 0; var total = 0;
    while i < loop_owner.first.len {
        total += loop_owner.first[i];
        if loop_owner.first.len < 5 { lh.s.first.push(i + 10); }
        i += 1;
    }
    assert(total == 47 && loop_owner.first == [1, 10, 11, 12, 13]);

    pool.items.push(3);
    let built = pool.alloc_with() { ph.p.items.push(5); 7 };
    assert(built == 7 && pool.items == [3, 5, 7]);

    var a = 11; var b = 22; var c = 33;
    var refs = Refs { .. }; var rh = RH { refs };
    refs.items.push(a); rh.r.items.push(b); refs.items.push(c);
    assert(refs.items.len == 3);
    assert(refs.items[0] == 11 && refs.items[1] == 22 && refs.items[2] == 33);

    var bytes = Bytes { .. }; var bh = BH { bytes };
    bytes.items.resize(1024 * 1024, 11);
    bh.b.items.resize(3 * 1024 * 1024, 22);
    assert(trim_memory());
    bytes.items.push(33);
    assert(bytes.items.len == 3 * 1024 * 1024 + 1);
    assert(bytes.items[0] == 11 && bytes.items[2 * 1024 * 1024] == 22);
    assert(bytes.items[bytes.items.len - 1] == 33);
    print("root loaded aliases: ok");
}
