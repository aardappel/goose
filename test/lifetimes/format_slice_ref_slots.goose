// A format hook taking a slice by reference (§3.7) takes the slot the slice
// lies in, as a reference parameter takes a slice lvalue (§4.1): it
// re-points a `var`, a plain `let` (written through a reference as a var
// is, §4.4), a field or an element of a `var`, and what a writable
// reference names. What the slot holds as print calls the hook is what a
// load through the parameter sees (§9.2), so a view of it may be kept
// wherever that slice may be.
struct S { f: u8[:] }
struct H { r: i64[:]& }
var buf: u8[>..] = [];
var nums: i64[>..] = [];
var kept: const i64[:][>..] = [];

// Renders the first byte and consumes it.
fn format(out: u8[>..]&, p: u8[:]&) {
    if p.len > 0 {
        out.push(p[0]);
        p = p[1..];
    }
}
// Renders the length and keeps a view of the rest.
fn format(out: u8[>..]&, p: i64[:]&) {
    kept.push(p[1..]);
    format(out, p.len);
}
fn show(r: u8[:]&) { print(r); }

fn main() {
    buf.append("abcdef");
    var v = buf[..];
    print(v);
    let t = buf[2..];
    print(t);
    print(v.len, " ", t.len);
    var s = S { f: buf[..] };
    print(s);
    print(s.f.len);
    var arr: u8[:][>..] = [];
    arr.push(buf[..]);
    arr.push(buf[3..]);
    print(arr);
    print(arr[0].len, " ", arr[1].len);
    show(v);
    print(v.len);

    nums.push(1);
    nums.push(2);
    nums.push(3);
    var ns = nums[..];
    print(ns);
    let h = H { r: ns };
    print(h);
    print(kept);
}
