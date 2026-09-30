// A construct whose branches are all [] is literal-like, as a lone [] is
// (§6.4): an argument's first check, made before its parameter's type is
// known, leaves the element type open, and the parameter it is passed to, or
// whose default it is, gives it one (§4.2).
fn count(xs: i64[]) -> i64 { xs.len }
fn room(xs: u8[..4]) -> i64 { xs.cap - xs.len }
fn size<T>(xs: T) -> i64 { xs.len }
fn none(xs: i64[] = if true { [] } else { [] }) -> i64 { xs.len }

fn main() {
    let c = true;
    print(count(if c { [] } else { [] }));                     // 0
    print(room(match 2 { 1 => [], _ => [] }));                 // 4
    print(size<u8[]>(if c { [] } else { [] }));                // 0
    print((if !c { [] } else { { [] } }).count());             // 0
    print(none());                                             // 0
}
