// CLBG::Fannkuchredux: the maximum number of prefix reversals ("flips") over
// all permutations of n elements, and the alternating checksum of the flip
// counts, visiting the permutations in the order of the counting rotation.

namespace fannkuchredux;

import std;
import bench;

// The checksum and the largest flip count over all permutations of 0..n.
fn fannkuchredux(n: i32) -> i32, i32 {
    var perm1: i32[32] = [0; 32];
    for i in 32 { perm1[i] = i as i32; }

    var perm: i32[32] = [0; 32];
    var count: i32[32] = [0; 32];
    var max_flips_count: i32 = 0;
    var perm_count = 0;
    var checksum: i32 = 0;
    var r: i64 = n;

    loop {
        while r > 1 {
            count[r - 1] = r as i32;
            r--;
        }

        copy_into(perm[..n], perm1[..n]);

        var flips_count: i32 = 0;
        var k = perm[0];

        while k != 0 {
            let k2 = (k + 1) >> 1;
            for i in k2 {
                let j = k - i;
                swap(perm[i], perm[j]);
            }
            flips_count++;
            k = perm[0];
        }

        if flips_count > max_flips_count {
            max_flips_count = flips_count;
        }

        if perm_count % 2 == 0 {
            checksum += flips_count;
        } else {
            checksum -= flips_count;
        }

        loop {
            if r == n { return checksum, max_flips_count; }

            let perm0 = perm1[0];
            for i in r {
                let j = i + 1;
                swap(perm1[i], perm1[j]);
            }

            perm1[r] = perm0;
            count[r] -= 1;
            if count[r] > 0 { break; }
            r++;
        }

        perm_count++;
    }
}

struct Fannkuchredux { n: i64, result_val: u32 }

fn new_fannkuchredux() -> Fannkuchredux {
    Fannkuchredux { n: config_i64("CLBG::Fannkuchredux", "n"), result_val: 0 }
}

fn prepare(s: Fannkuchredux&) {}

fn run(s: Fannkuchredux&, iteration_id: i64) {
    let checksum, max_flips = fannkuchredux(s.n as i32);
    s.result_val += (checksum as! u32) * 100 + (max_flips as! u32);
}

fn checksum(s: Fannkuchredux&) -> u32 { s.result_val }
