// runtime-debug: a fixed field of a frame object literal that leaves the
// function (`return ... from`) before the literal reaches the tail: the exit
// restores the tail's stack from its watermark, which is already set. The
// object is a local, a nested local, and an argument's temporary. poison()
// leaves junk in the C stack slots the objects take.
struct Packet { header: i64, bytes: u8[>..] }
struct Outer { id: i64, inner: Packet }

fn poison(n: i64) -> u64 {
    var junk: u64[64] = [0xDEADBEEF; 64];
    junk[n] = 0x12345678;
    return junk[0] + junk[n];
}

fn read_header(fail: bool) -> i64 {
    if fail { return -1 from build; }
    return 42;
}
fn build(fail: bool) -> i64 {
    var p = Packet { header: read_header(fail), bytes: [] };
    p.bytes.push(1);
    return p.header + p.bytes.len;
}

fn read_id(fail: bool) -> i64 {
    if fail { return -2 from build_outer; }
    return 7;
}
fn build_outer(fail: bool) -> i64 {
    var o = Outer { id: 1, inner: Packet { header: read_id(fail), bytes: [3, 4] } };
    o.inner.bytes.push(5);
    return o.id + o.inner.header + o.inner.bytes.len;
}

fn total(p: Packet) -> i64 { p.header + p.bytes.len }
fn read_arg(fail: bool) -> i64 {
    if fail { return -3 from build_arg; }
    return 10;
}
fn build_arg(fail: bool) -> i64 {
    return total(Packet { header: read_arg(fail), bytes: [9, 9] });
}

fn main() {
    var after: i64[>..] = [];
    for fail in [false, true] {
        poison(1);
        after.push(build(fail));
        poison(2);
        after.push(build_outer(fail));
        poison(3);
        after.push(build_arg(fail));
    }
    var tail: u8[>..] = [];
    for i in 100 { tail.push(i as u8); }
    print(after, " ", tail.len, " ", tail[99]);
}
