// runtime-define: GS_STACK_RESERVE=100001
// A data stack reservation that is no whole number of pages is rounded down
// to one: the program starts where the guard gap behind a region has to
// start on a page (POSIX), and a stack grown past GS_STACK_RESERVE into its
// last page overruns, where a committed part page would let it on (Windows).
import std;

fn main() {
    var s: u8[>..] = [];
    for i in 100500 { s.push(1); }
    print(s.len);
}
