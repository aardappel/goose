// Compress::BWTEncode and Compress::BWTDecode: the Burrows-Wheeler transform
// of a repeated "ABRACADABRA", sorting its rotations by prefix doubling, and
// the inverse transform, which follows the rotations in text order.

namespace bwt;

import std;
import bench;
import compress_data;

// The last column of the sorted rotations, and the row at which the input
// itself sorts.
struct BWTResult { original_idx: i32, transformed: u8[>..] }

// A rotation's sort key in one doubling round: the rank of its first k
// characters and the rank of the k after them.
struct RankPair { first: i32, second: i32 }

fn bwt_transform(input: u8[:]) -> BWTResult {
    let n = input.len;
    if n == 0 { return BWTResult { original_idx: 0, transformed: [] }; }

    var sa: i32[>..] = [];
    sa.resize(n, 0);

    var counts: i32[256] = [0; 256];
    for byte in input { counts[byte] += 1; }

    var positions: i32[256] = [0; 256];
    var total: i32 = 0;
    for i in 256 {
        positions[i] = total;
        total += counts[i];
    }

    var temp_counts: i32[256] = [0; 256];
    for i in n {
        let byte = input[i];
        let pos = positions[byte] + temp_counts[byte];
        sa[pos] = i as i32;
        temp_counts[byte] += 1;
    }

    if n > 1 {
        var rank: i32[>..] = [];
        rank.resize(n, 0);
        var current_rank: i32 = 0;
        var prev_char = input[sa[0]];

        for idx in sa {
            if input[idx] != prev_char {
                current_rank += 1;
                prev_char = input[idx];
            }
            rank[idx] = current_rank;
        }

        var k = 1;
        while k < n {
            var pairs: RankPair[>..] = [];
            for i in n { pairs.push(RankPair { rank[i], rank[(i + k) % n] }); }

            stable_sort(sa) { a, b =>
                let pa = pairs[a];
                let pb = pairs[b];
                if pa.first != pb.first { pa.first < pb.first } else { pa.second < pb.second }
            };

            var new_rank: i32[>..] = [];
            new_rank.resize(n, 0);
            new_rank[sa[0]] = 0;
            for i in 1..n {
                let prev = sa[i - 1];
                let curr = sa[i];
                let same = pairs[prev] == pairs[curr];
                new_rank[curr] = new_rank[prev] + (if same { 0 } else { 1 });
            }

            // Goose has no move for a growable array: the new ranks are copied.
            rank = copy(new_rank);
            k <<= 1;
        }
    }

    // The result is built where it is returned to, its bytes in place.
    var result = BWTResult { original_idx: 0, transformed: [] };
    let transformed: u8[>..]& = result.transformed;
    transformed.resize(n, 0);
    for suffix, i in sa {
        if suffix == 0 {
            transformed[i] = input[n - 1];
            result.original_idx = i as i32;
        } else {
            transformed[i] = input[suffix - 1];
        }
    }
    return result;
}

fn run_encode(b: Bench&) -> u32 {
    let test_data = ::generate_test_data(config_i64(b.name, "size"));
    var bwt_result = BWTResult { original_idx: 0, transformed: [] };
    var result_val: u32 = 0;
    measure(b) {
        bwt_result = bwt_transform(test_data);
        result_val += bwt_result.transformed.len as! u32;
    };
    return result_val;
}

fn bwt_inverse(bwt_result: BWTResult&) -> u8[>..] {
    let bwt = bwt_result.transformed;
    let n = bwt.len;
    if n == 0 { return []; }

    var counts: i32[256] = [0; 256];
    for byte in bwt { counts[byte] += 1; }

    var positions: i32[256] = [0; 256];
    var total: i32 = 0;
    for i in 256 {
        positions[i] = total;
        total += counts[i];
    }

    var next: i32[>..] = [];
    next.resize(n, 0);
    var temp_counts: i32[256] = [0; 256];

    for byte, i in bwt {
        let pos = positions[byte] + temp_counts[byte];
        next[pos] = i as i32;
        temp_counts[byte] += 1;
    }

    var result: u8[>..] = [];
    result.resize(n, 0);
    var idx = bwt_result.original_idx;

    for i in n {
        idx = next[idx];
        result[i] = bwt[idx];
    }
    return result;
}

fn run_decode(b: Bench&) -> u32 {
    // BWTEncode's prepare and run(0).
    let test_data = ::generate_test_data(config_i64(b.name, "size"));
    let bwt_result = bwt_transform(test_data);
    var inverted: u8[>..] = [];
    var result_val: u32 = 0;
    measure(b) {
        inverted = bwt_inverse(bwt_result);
        result_val += inverted.len as! u32;
    };
    if inverted == test_data { result_val += 100000; }
    return result_val;
}
