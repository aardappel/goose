// runtime-define: GS_DEBUG=1
// Signed overflow aborts a debug build (§6.2), naming the operands, the
// operation's type and the line of the operation itself, which is inside
// `next` even where -O2 inlines it into main.
fn next(score: i32, bonus: i32) -> i32 {
    score + bonus
}

fn main() {
    var bonuses: i32[>..] = [1000000000, 1000000000, 1000000000];
    var score: i32 = 0;
    for b in bonuses {
        score = next(score, b);
        print(score);
    }
}
