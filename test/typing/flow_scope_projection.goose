// Flow joins project onto the variables still in scope. Match binders and
// locals from a break snapshot must not become facts about later locals;
// a callback can also name variables across gaps in the active call path.
enum Choice { Left { n: i64 }, Right { n: i64 } }

fn invoke<F>(choice: Choice) {
    let hidden = 90;
    F(choice);
    print(hidden);
}

fn run(choice: Choice) {
    var base = 10;
    var opt: i64? = base;
    let result: i64;
    invoke(choice) {
        block {
            match it {
                Left l => {
                    let scratch = l.n;
                    result = scratch;
                    break;
                },
                Right r => { result = r.n + 1; }
            }
            let later = 3;
            print(later);
        }
        if opt && result > 0 { print(opt + result); }
    };
    let after = result + 1;
    print(after);
}

fn main() {
    run(Choice.Left { 1 });
    run(Choice.Right { 2 });
}
