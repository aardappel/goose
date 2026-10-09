// runtime-define: GS_DEBUG=1
// A member's negation overflows as a scalar `-` does (§6.1, §6.2): a debug
// build aborts at the line of the negation.
struct Pair { lo: i32, hi: i32 }

fn flip(p: Pair) -> Pair {
    -p
}

fn main() {
    var ps: Pair[>..] = [Pair { lo: 1, hi: -2 }, Pair { lo: 3, hi: -2147483647 - 1 }];
    for p in ps {
        let q = flip(p);
        print(q.lo, " ", q.hi);
    }
}
