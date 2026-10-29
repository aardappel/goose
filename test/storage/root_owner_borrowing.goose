// runtime-debug: whole-owner parameters, returned component references and captures.
root struct State<T> { first: T[>..<], second: T[>..<], count: i64 = 0 }

fn head<T>(s: State<T>&) -> T& { s.first[0] }
fn clear_other<T>(s: State<T>&) { s.second.clear(); s.count += 1; }
fn append<T>(s: State<T>&, x: T) { s.first.push(x); }
fn same<T>(s: State<T>&) -> State<T>& { s }
fn position<T>(s: State<T>&, x: T&) -> i64 { s.first.index_of(x) }
fn exercise(s: State<i64>&) {
    let r .= head(s);
    clear_other(s);
    append(s, 12);
    print(r, " ", s.first.index_of(r), " ", s.first.len, " ", s.count);
}
fn main() {
    var s = State<i64> { [10], [20], .. };
    let alias = s;
    exercise(alias);
    let returned = same(s);
    print(position(returned, returned.first[0]));
    fn captured() { s.second.push(30); }
    captured();
    print(s.second[0]);
}
