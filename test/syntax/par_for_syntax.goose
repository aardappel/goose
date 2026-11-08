// goose-threads
// `par` is a keyword only before `for` (docs/design/child_threads.md §4.1):
// a function, a variable and a field of the name are what they were, and
// every form of `for` takes it, which the dump writes back as written.
struct Pair { par: i64 }

fn par(x: i64) -> i64 { x + 1 }

fn main() {
    let p = Pair { par: par(1) };
    var total = p.par;
    var xs = [1, 2, 3, 4];
    par for k in 4 { total += k; }
    par for k in 1..3 { total += k; }
    par for x in xs { total += x; }
    par for &x in xs { x = x * 2; }
    par for &x, i in xs { x = x + i; }
    par for x, i: u8 in xs { total += i; }
    par for k: i32 in 0..10 { total += k; }
    par for k, n in 5..9 { total += n; }
    print(total, " ", xs);
    {
        let par = 5;
        print(par * 2);
    }
}
