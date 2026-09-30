// A format overload taking a value by reference is handed the storage the
// value lies in (§3.7), and may keep a reference to it wherever that storage
// outlives the kept reference: what a reference names is such storage
// whatever roots the reference has, none yet for one bound only in a later
// iteration or none at all, and so is a field reached through it. A variant
// constant is no storage, and holds no references, whatever the enum's
// other variants hold.
struct S { x: i64 }
enum E { A { r: const i64& }, B }
var g: i64 = 5;
var gs = S { x: 3 };
var never: (const i64&)? = null;
var keep: (const i64&)? = null;
fn format(out: u8[>..]&, v: const i64&) {
    keep .= v;
    out.append("k");
}
fn main() {
    var r: (const i64&)? = null;
    var q: (const i64&)? = null;
    var rs: (const S&)? = null;
    for i in 0..3 {
        if r != null { print(r); }
        print(q);
        if rs != null { print(rs.x); }
        r .= g;
        q .= g;
        rs .= gs;
    }
    print(never);
    if keep != null { print(keep .== gs.x); }
    print(E.B, " ", E.A { r: g });
    if keep != null { print(keep .== g); }
}
