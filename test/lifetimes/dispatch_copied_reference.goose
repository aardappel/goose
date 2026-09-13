// runtime-debug: copied case values release their source borrow; reference arms retain it.
enum Choice { A, B }
fn use(c: Choice.A, unused: Choice) { print("copied A"); }
fn use(c: Choice.B, unused: Choice) { print("copied B"); }
enum Mixed { A { n: i64 }, B { text: u8[] } }
fn mixed(v: Mixed.A&, tail: i64) { v.n += tail; print(v.n); }
fn mixed(v: Mixed.B, tail: i64) { print(v.text); }
fn main() {
    var choices: Choice[>..<] = [Choice.A {}];
    use(&choices[0], choices.pop());
    print(choices.len);
    var a: Mixed.. = Mixed.A { n: 3 };
    mixed(a, 4);
    match &a { A &v => print(v.n), B v => {} }
    var b: Mixed.. = Mixed.B { text: "value arm" };
    mixed(b, 0);
}
