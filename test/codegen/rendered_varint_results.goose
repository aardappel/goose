// A call's reference result to a varint is rendered as the i64 it decodes to
// (§3.6), a value no storage holds: a user format overload taking an i64 by
// reference is handed that value, read-only, whether the call is inlined or
// not.
struct V { n: varint, w: i32 }
var gv = V { n: 3, w: 7 };

// Renders a one-digit value.
fn format(out: u8[>..]&, v: i64&) { out.push((v + 48) as u8); }
fn get(v: V&) -> const varint& { v.n }

fn main() {
    print(get(gv));
    let w: i64 = gv.w;
    print(get(gv), " ", w);
}
