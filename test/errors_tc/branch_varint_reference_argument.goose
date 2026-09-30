// error: no matching overload for call to set
// A reference parameter binds each branch as the storage it names (§4.1):
// varint fields as a varint&, which an i64& parameter does not bind ("cannot
// pass varint as i64&", §3.6), as it does not bind either field alone.
struct V { n: varint, m: varint }
fn set(r: i64&) { r = 7; }
fn main() {
    var v = V { n: 300, m: 5 };
    set(if v.n > v.m { v.n } else { v.m });
    print(v);
}
