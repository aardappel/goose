struct Show { x: i64 }
var data: i64[>..<] = [42];
fn format(out: u8[>..]&, v: Show&) { data.clear(); format(out, v.x); }
fn main() {
    let s = Show { 1 };
    if data.len > 0 { print(s, data[0]); }
}
