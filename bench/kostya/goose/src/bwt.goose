// Compress::BWTEncode and Compress::BWTDecode: the Burrows-Wheeler transform
// of a repeated "ABRACADABRA", sorting its rotations by prefix doubling, and
// the inverse transform, which follows the rotations in text order.

namespace bwt;

import std;
import bench;
import compress_data;

// A rotation's sort key in one doubling round: the rank of its first k
// characters and the rank of the k after them.
struct RankPair { first: u32, second: u32 }

// The last column of the sorted rotations of `input`, and the row at which
// `input` itself sorts.
fn bwt_transform(input: u8[:]) -> u8[>..], i64 {
    let n = input.len;
    var transformed: u8[>..] = [];
    if n == 0 { return transformed, 0; }

    // The rotations bucketed by their first byte, in order of position.
    var counts: u32[256] = [0; 256];
    for c in input { counts[c] += 1; }
    var positions: u32[256] = [0; 256];
    var total: u32 = 0;
    for i in 256 {
        positions[i] = total;
        total += counts[i];
    }
    var sa: u32[>..] = [];
    sa.resize(n, 0);
    var temp_counts: u32[256] = [0; 256];
    for c, i in input {
        sa[positions[c] + temp_counts[c]] = i as u32;
        temp_counts[c] += 1;
    }

    if n > 1 {
        var rank: u32[>..] = [];
        rank.resize(n, 0);
        var current_rank: u32 = 0;
        var prev_char = input[sa[0]];
        for idx in sa {
            if input[idx] != prev_char {
                current_rank++;
                prev_char = input[idx];
            }
            rank[idx] = current_rank;
        }

        // Each round sorts the rotations by their first 2k characters, as
        // the pair of ranks of two k-character halves, and re-ranks them. The
        // sort is stable, so ties keep the previous round's order.
        var pairs: RankPair[>..] = [];
        pairs.resize(n, RankPair { 0, 0 });
        var k = 1;
        while k < n {
            for i in n { pairs[i] = RankPair { rank[i], rank[(i + k) % n] }; }
            stable_sort(sa) { a, b =>
                let pa = pairs[a];
                let pb = pairs[b];
                if pa.first != pb.first { pa.first < pb.first } else { pa.second < pb.second }
            };
            // The pairs hold the old ranks, so the new ones can replace them.
            var r: u32 = 0;
            rank[sa[0]] = 0;
            for i in 1..n {
                if pairs[sa[i - 1]] != pairs[sa[i]] { r++; }
                rank[sa[i]] = r;
            }
            k *= 2;
        }
    }

    var original_idx = 0;
    for suffix, i in sa {
        if suffix == 0 {
            transformed.push(input[n - 1]);
            original_idx = i;
        } else {
            transformed.push(input[suffix - 1]);
        }
    }
    return transformed, original_idx;
}

fn run_encode(b: Bench&) -> u32 {
    let data = ::generate_test_data(config_i64(b.name, "size"));
    var checksum: u32 = 0;
    measure(b) {
        let transformed, original_idx = bwt_transform(data);
        checksum += transformed.len as! u32;
    };
    return checksum;
}

// The text whose transform `bwt` is, given the row the text sorted to.
fn bwt_inverse(bwt: u8[:], original_idx: i64) -> u8[>..] {
    let n = bwt.len;
    var result: u8[>..] = [];
    if n == 0 { return result; }

    var counts: u32[256] = [0; 256];
    for c in bwt { counts[c] += 1; }
    var positions: u32[256] = [0; 256];
    var total: u32 = 0;
    for i in 256 {
        positions[i] = total;
        total += counts[i];
    }

    // next[j] is the row of the rotation one character on from row j's.
    var next: u32[>..] = [];
    next.resize(n, 0);
    var temp_counts: u32[256] = [0; 256];
    for c, i in bwt {
        next[positions[c] + temp_counts[c]] = i as u32;
        temp_counts[c] += 1;
    }

    var idx = original_idx;
    for i in n {
        idx = next[idx];
        result.push(bwt[idx]);
    }
    return result;
}

fn run_decode(b: Bench&) -> u32 {
    let data = ::generate_test_data(config_i64(b.name, "size"));
    let transformed, original_idx = bwt_transform(data);
    var inverted: u8[>..] = [];
    var checksum: u32 = 0;
    measure(b) {
        inverted = bwt_inverse(transformed, original_idx);
        checksum += inverted.len as! u32;
    };
    if inverted == data { checksum += 100000; }
    return checksum;
}
