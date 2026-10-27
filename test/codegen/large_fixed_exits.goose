// runtime-define: GS_STACK_RESERVE=4194304
// Fixed values above the C backend's native-value threshold live on data
// stacks. A large return can unwind through other large locals, and a
// continue or break leaves a loop body holding one. Repeat exits often enough
// that failing to restore a 256 KB data-stack watermark exhausts the 4 MB
// reservation above.
struct Game { blocks: u8[256000], seed: i64 }

fn bail(n: i64) {
    let scratch = Game { seed: n, .. };
    if n > 0 { return scratch from early; }
}
fn early(n: i64) -> Game {
    let live = Game { seed: -1, .. };
    bail(n);
    return live;
}
fn scopes() {
    for i in 1100 {
        let g = Game { seed: i, .. };
        if i % 2 == 0 { continue; }
        assert(g.seed == i);
    }
    for i in 1100 {
        let result = early(i);
        assert(result.seed == (if i == 0 { -1 } else { i }));
        while true {
            let g = Game { seed: i, .. };
            assert(g.seed == i);
            break;
        }
    }
}

fn main() {
    scopes();
    print("large fixed exits ok");
}
