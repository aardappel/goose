// A generic `format` overload on a bare type variable, an array of one, or
// a non-generic type is an ordinary overload that a direct call may pick,
// not a rendering hook: print, str and format render as they would without
// it (§3.7).
struct Pt { x: i32 }
struct Box<T> { v: T }
fn format<T>(out: u8[>..]&, v: T) { out.push('!'); }
fn format<T>(out: u8[>..]&, a: T[]) { out.push('#'); }
fn format<T>(out: u8[>..]&, p: Pt) { out.push('?'); }
fn format<T>(out: u8[>..]&, b: Box<T>?) { out.push('o'); }

fn main() {
    var xs: i32[] = [1, 2];
    let r: Box<i32>? = null;
    print(3, " ", "text", " ", Pt { 1 }, " ", xs, " ", Box<i32> { 2 }, " ", r);
    print(str(xs), " ", str(4));
    let out: u8[>..] = [];
    format(out, 3);
    format(out, "x", " ", xs);
    print(out);
}
