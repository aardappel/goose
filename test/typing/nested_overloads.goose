// Nested functions of one name declared in one scope are an overload set
// (§7.5, §11.1), so case functions (§8.2) can be nested and reach the
// enclosing function's locals.

enum Op { Dec, Inc, Prev, Next, Print, Loop { body: Op[:] } }

// A tree-walking interpreter in the visitor shape: one case function per
// variant, all nested in run and sharing its tape as free variables.
fn run(prog: Op[:]) -> i64 {
    var cells: u8[>..] = [0];
    var pos = 0;
    var out = 0;
    fn exec(o: Op.Dec) { cells[pos] -= 1; }
    fn exec(o: Op.Inc) { cells[pos] += 1; }
    fn exec(o: Op.Prev) { pos -= 1; }
    fn exec(o: Op.Next) {
        pos += 1;
        if pos == cells.len { cells.push(0); }
    }
    fn exec(o: Op.Print) { out = out * 31 + cells[pos]; }
    // The loop case calls the set again, its own overload included.
    recursive fn exec(o: Op.Loop) {
        while cells[pos] != 0 {
            for op in o.body { exec(op); }
        }
    }
    for op in prog { exec(op); }
    return out;
}

fn main() {
    // ++[>+++<-]>.  prints 6; then [-] clears it, and . prints 0.
    var pool: Op[>..] = [];
    pool.push(Op.Next);
    for i in 3 { pool.push(Op.Inc); }
    pool.push(Op.Prev);
    pool.push(Op.Dec);
    let inner = pool[0..6];
    let clear = pool[5..6];
    var prog: Op[>..] = [Op.Inc, Op.Inc];
    prog.push(Op.Loop { body: inner });
    prog.push(Op.Next);
    prog.push(Op.Print);
    prog.push(Op.Loop { body: clear });
    prog.push(Op.Print);
    print(run(prog), " ", inner.len);

    // Overloads by parameter type, called from the enclosing code once all
    // are declared, from each other in either order, and passed as one
    // function value.
    var log: i64[>..] = [];
    fn note(x: i64) { log.push(x); }
    fn note(s: u8[:]) { note(s.len); }
    fn note(x: f64) { note(x as! i64 * 10); }
    note(1);
    note("four");
    note(2.5);
    fn apply<F>(a: i64, s: u8[:]) {
        F(a);
        F(s);
    }
    apply(7, "", note);
    print(log);

    // A later function with the parameter types of an earlier one shadows
    // it from there on, as a later local does; the other overloads stay.
    fn pick(x: i64) -> i64 { 1 }
    fn pick(s: u8[:]) -> i64 { 2 }
    print(pick(0), " ", pick(""));
    fn pick(x: i64) -> i64 { 3 }
    print(pick(0), " ", pick(""));

    // An inner scope's set hides the outer one entirely; outside it the
    // outer set is back.
    fn twice(x: i64) -> i64 { x * 2 }
    {
        fn twice(s: u8[:]) -> i64 { s.len * 2 }
        fn twice(x: f64) -> f64 { x * 2.0 }
        print(twice("abc"), " ", twice(1.25));
    }
    print(twice(21));
}
