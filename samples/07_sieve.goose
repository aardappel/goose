// The sieve of Eratosthenes: the classic tight loop. Everything here runs
// at the width it is declared at -- the flags are bytes, the loop counters
// are i32 -- and the compiler proves every index in range, so the inner
// loop is the same three instructions C would emit, with a bounds check
// nowhere in sight.
//
// Build and run (from this directory):
//   goose -o sieve.c 07_sieve.goose && cl sieve.c && sieve

import std;
import os;

let N = 10000000;

fn main() {
    // One byte per candidate, all "prime" until crossed out: ten million
    // bytes in one block, filled in place by resize.
    var flags: u8[>..] = [];
    flags.resize(N + 1, 1);
    flags[0] = 0;
    flags[1] = 0;
    let t0 = clock();
    var i = 2;
    while i * i <= N {
        if flags[i] == 1 {
            // Cross out the multiples from i*i on: smaller ones were hit
            // by a smaller prime already.
            var j = i * i;
            while j <= N { flags[j] = 0; j += i; }
        }
        i++;
    }
    let t1 = clock();

    // Collect the primes. `%` is Euclidean, so bucketing by residue needs
    // no care about signs; here it counts the primes by their last digit.
    var primes: i32[>..] = [];
    var by_last_digit: i64[10] = [0; 10];
    for n, k in flags {
        if n == 1 {
            primes.push(k as i32);
            by_last_digit[k % 10] += 1;
        }
    }
    // Twin primes: pairs two apart.
    var twins = 0;
    for k in 1..primes.len {
        if primes[k] - primes[k - 1] == 2 { twins++; }
    }
    print(primes.len, " primes up to ", N);
    // Timing goes to stderr, so the output proper stays the same run to run.
    var note: u8[>..] = [];
    format(note, "sieve took ");
    format_flt(note, (t1 - t0) * 1000.0, 1);
    format(note, " ms\n");
    write_stderr(note);
    print("last ten: ", primes[^10..]);
    print("twin pairs: ", twins);
    print("by last digit: ", by_last_digit);
    // A checksum in wrapping unsigned arithmetic, which is what unsigned
    // types mean: no overflow checks, no surprises.
    var h: u32 = 0;
    for p in primes { h = h * 31 + p as u32; }
    print("hash ", h);
}
