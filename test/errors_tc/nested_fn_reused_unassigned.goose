// error: variable r may be used before it is assigned
// rd's first call is where r has been assigned; its second is after the if,
// where r may not have been, so that call is checked again (§4.4, §7.5).
struct Cell { v: i64 }
var cells: Cell[>..] = [];
fn run(c: bool) {
    var r: Cell&;
    fn rd() { print(r.v); }
    if c {
        r .= cells[0];
        rd();
    }
    rd();
}
fn main() {
    cells.push(Cell { v: 5 });
    run(false);
}
