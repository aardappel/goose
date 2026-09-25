// error: a is not fixed-size and is not copied implicitly
// Case functions taking a variant by value receive a copy of the if's value
// (§8.2), and a branch naming a variable-size ADT variable is copied into it
// only through copy() (§4.1).
enum Msg { Text { s: u8[] }, Num { n: i64 } }
fn size(t: Msg.Text) -> i64 { t.s.len }
fn size(n: Msg.Num) -> i64 { n.n }
fn make(n: i64) -> Msg.. { Msg.Num { n: n } }
fn main() {
    let a: Msg.. = Msg.Text { s: "hello" };
    var k = 3;
    print(size(if k > 2 { a } else { make(k) }));
}
