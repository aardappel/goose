// Distance::Jaro and Distance::NGram: string similarity over pairs of random
// strings. The pairs are one contiguous block: each pair is its two strings
// back to back, lengths inline.

namespace distance;

import std;
import dictionary;
import bench;
import helper;

struct Pair { s1: u8[], s2: u8[] }

fn random_string(len: i64) -> u8[>..] {
    var s: u8[>..] = [];
    for i in len { s.push(('a' + next_int(10)) as! u8); }
    return s;
}

// n pairs of strings over "abcdefghij", each 4 to m + 3 bytes long.
fn generate_pair_strings(n: i64, m: i64) -> Pair[>..] {
    var pairs: Pair[>..] = [];
    for i in n {
        let len1 = next_int(m) + 4;
        let len2 = next_int(m) + 4;
        pairs.push(Pair { s1: random_string(len1), s2: random_string(len2) });
    }
    return pairs;
}

// --- Distance::Jaro -------------------------------------------------------------

fn jaro(s1: u8[:], s2: u8[:]) -> f64 {
    let len1 = s1.len;
    let len2 = s2.len;
    if len1 == 0 || len2 == 0 { return 0.0; }
    let match_dist = max(max(len1, len2) / 2 - 1, 0);

    var s1_matches: bool[>..] = [];
    s1_matches.resize(len1, false);
    var s2_matches: bool[>..] = [];
    s2_matches.resize(len2, false);

    var matches = 0;
    for i in len1 {
        let start = max(i - match_dist, 0);
        let end = min(len2 - 1, i + match_dist);
        for j in start..end + 1 {
            if !s2_matches[j] && s1[i] == s2[j] {
                s1_matches[i] = true;
                s2_matches[j] = true;
                matches++;
                break;
            }
        }
    }
    if matches == 0 { return 0.0; }

    var transpositions = 0;
    var k = 0;
    for i in len1 {
        if s1_matches[i] {
            while k < len2 && !s2_matches[k] { k++; }
            if k < len2 {
                if s1[i] != s2[k] { transpositions++; }
                k++;
            }
        }
    }
    transpositions /= 2;

    let m = matches as f64;
    return (m / len1 + m / len2 + (m - transpositions) / m) / 3.0;
}

fn run_jaro(b: Bench&) -> u32 {
    let pairs = generate_pair_strings(config_i64(b.name, "count"), config_i64(b.name, "size"));
    var result: u32 = 0;
    measure(b) {
        for p in pairs { result += (jaro(p.s1, p.s2) * 1000.0) as u32; }
    };
    return result;
}

// --- Distance::NGram ------------------------------------------------------------

let N = 4;

// The N-gram of s at i, its bytes packed big-endian into a u32.
fn gram_at(s: u8[:], i: i64) -> u32 {
    ((s[i] as u32) << 24) | ((s[i + 1] as u32) << 16) | ((s[i + 2] as u32) << 8) | s[i + 3]
}

// The counts are kept with `get`, and `insert` for a gram not seen before:
// that looks a new gram up once, where `update` would look it up again after
// inserting it.
fn ngram(s1: u8[:], s2: u8[:]) -> f64 {
    if s1.len < N || s2.len < N { return 0.0; }

    var grams1 = dictionary<u32, i32> {};
    grams1.reserve(s1.len);
    for i in 0..s1.len - N + 1 {
        let gram = gram_at(s1, i);
        let count = grams1.get(gram);
        if count { count += 1; } else { grams1.insert(gram, 1); }
    }

    var grams2 = dictionary<u32, i32> {};
    grams2.reserve(s2.len);
    var intersection = 0;
    for i in 0..s2.len - N + 1 {
        let gram = gram_at(s2, i);
        var count2: i32 = 1;
        let count = grams2.get(gram);
        if count { count += 1; count2 = count; } else { grams2.insert(gram, 1); }
        let count1 = grams1.get(gram);
        if count1 && count2 <= count1 { intersection++; }
    }

    let total = grams1.count + grams2.count;
    return if total > 0 { intersection as f64 / total } else { 0.0 };
}

fn run_ngram(b: Bench&) -> u32 {
    let pairs = generate_pair_strings(config_i64(b.name, "count"), config_i64(b.name, "size"));
    var result: u32 = 0;
    measure(b) {
        for p in pairs { result += (ngram(p.s1, p.s2) * 1000.0) as u32; }
    };
    return result;
}
