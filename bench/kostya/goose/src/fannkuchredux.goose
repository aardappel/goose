// CLBG::Fannkuchredux: the maximum number of prefix reversals ("flips") over
// all permutations of n elements, and the alternating checksum of the flip
// counts, visiting the permutations in the order of the counting rotation.

namespace fannkuchredux;

import std;
import bench;

// The checksum and the largest flip count over all permutations of 0..n.
fn fannkuch(n: i64) -> i32, i32 {
    var perm1: i32[32] = [0; 32];
    for i in 32 { perm1[i] = i as i32; }
    var perm: i32[32] = [0; 32];
    var count: i32[32] = [0; 32];
    var max_flips: i32 = 0;
    var perm_count = 0;
    var checksum: i32 = 0;
    var r = n;
    loop {
        while r > 1 {
            count[r - 1] = r as i32;
            r--;
        }
        copy_into(perm[..n], perm1[..n]);
        var flips: i32 = 0;
        var k = perm[0];
        while k != 0 {
            let k2 = (k + 1) >> 1;
            for i in k2 { swap(perm[i], perm[k - i]); }
            flips++;
            k = perm[0];
        }
        max_flips = max(max_flips, flips);
        if perm_count % 2 == 0 { checksum += flips; } else { checksum -= flips; }
        loop {
            if r == n { return checksum, max_flips; }
            // Rotate perm1[..r + 1] left by one.
            let perm0 = perm1[0];
            for i in r { perm1[i] = perm1[i + 1]; }
            perm1[r] = perm0;
            count[r] -= 1;
            if count[r] > 0 { break; }
            r++;
        }
        perm_count++;
    }
}

fn run(b: Bench&) -> u32 {
    let n = config_i64(b.name, "n");
    var result: u32 = 0;
    measure(b) {
        let checksum, max_flips = fannkuch(n);
        result += (checksum as! u32) * 100 + (max_flips as! u32);
    };
    return result;
}
