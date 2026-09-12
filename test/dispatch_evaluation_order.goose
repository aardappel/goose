import std;
enum Choice { A, B }
fn first() -> i64 { print("first"); 3 }
fn choose(a: bool) -> Choice { print("choice"); if a { Choice.A {} } else { Choice.B {} } }
fn last() -> i64 { print("last"); 7 }
fn use(a: i64, c: Choice.A, b: i64) { print(a, " A ", b); }
fn use(a: i64, c: Choice.B, b: i64) { print(a, " B ", b); }
enum Message { A { n: i64 }, B { s: u8[] } }
fn payload(m: Message.A, b: i64) { print(m.n); }
fn payload(m: Message.B, b: i64) { print(m.s); }
fn main() {
    use(first(), choose(true), last());
    use(first(), choose(false), last());
    var n = 1;
    fn change() -> Choice { n = 9; Choice.A {} }
    use(n, change(), n);
    var c: Choice = Choice.A {};
    fn change_tag() -> i64 { c = Choice.B {}; 4 }
    use(3, c, change_tag());
    c = Choice.A {};
    var r .= c;
    use(3, r, change_tag());
    var m: Message.. = Message.A { n: 1 };
    fn change_payload() -> i64 {
        match &m { A &a => { a.n = 9; }, B b => {} }
        return 0;
    }
    payload(m, change_payload());
    var m2: Message.. = Message.A { n: 2 };
    var mr .= m2;
    fn change_reference_payload() -> i64 {
        match &m2 { A &a => { a.n = 10; }, B b => {} }
        return 0;
    }
    payload(mr, change_reference_payload());
    var text: Message.. = Message.B { s: "copied" };
    payload(text, 0);
}
