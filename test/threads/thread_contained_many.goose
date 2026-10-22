// thread_spawn_contained (§11.2) a few hundred times, every third worker
// aborting: waited for in the reverse of the order they started in, twice
// each, every wait answers for its own worker, and only the first wait for
// one that aborted takes its message.
import std;

thread_fn job(n: i64) {
    if n % 3 == 0 { abort(str("job ", n, " gives up")); }
}

fn main() {
    var ids: i64[>..] = [];
    for n in 300 { ids.push(thread_spawn_contained(job, n)); }
    var returned = 0;
    var told = 0;
    var n = ids.len;
    while n > 0 {
        n--;
        var why: u8[>..] = [];
        let first = thread_wait(ids[n], why);
        var again: u8[>..] = [];
        let second = thread_wait(ids[n], again);
        assert(first == second && first == (n % 3 != 0) && again.len == 0);
        if first {
            assert(why.len == 0);
            returned++;
        } else {
            let want = str("job ", n, " gives up");
            assert(why.len > want.len && why[..want.len] == want);
            told++;
        }
    }
    print(returned, " returned, ", told, " aborted and told why");
}
