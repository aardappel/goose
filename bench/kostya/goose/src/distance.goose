// Distance::Jaro and Distance::NGram: string similarity over pairs of random
// strings.

namespace distance;

import std;
import dictionary;
import bench;
import helper;

// The reference's (String, String) tuple: Goose has no tuple type.
struct Pair { s1: u8[], s2: u8[] }

// n pairs of strings over "abcdefghij", each 4 to m + 3 bytes long.
fn generate_pair_strings(n: i64, m: i64) -> Pair[>..] {
    var pairs: Pair[>..] = [];
    let chars = "abcdefghij";
    for i in n {
        let len1 = next_int(m) + 4;
        let len2 = next_int(m) + 4;
        var str1: u8[>..] = [];
        for k in len1 { str1.push(chars[next_int(10)]); }
        var str2: u8[>..] = [];
        for k in len2 { str2.push(chars[next_int(10)]); }
        pairs.push(Pair { s1: copy(str1), s2: copy(str2) });
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
    let count = config_i64(b.name, "count");
    let size = config_i64(b.name, "size");
    let pairs = generate_pair_strings(count, size);
    var result: u32 = 0;
    measure(b) {
        for p in pairs { result += (jaro(p.s1, p.s2) * 1000.0) as u32; }
    };
    return result;
}

// --- Distance::NGram ------------------------------------------------------------

let N = 4;

fn ngram(s1: u8[:], s2: u8[:]) -> f64 {
    let len1 = s1.len;
    let len2 = s2.len;
    if len1 < N || len2 < N { return 0.0; }

    var grams1 = dictionary<u32, i32> {};
    grams1.reserve(len1);
    for i in 0..len1 - N + 1 {
        let gram = ((s1[i] as u32) << 24) | ((s1[i + 1] as u32) << 16) | ((s1[i + 2] as u32) << 8) | s1[i + 3];
        grams1.update(gram, 0) { it += 1; };
    }

    var grams2 = dictionary<u32, i32> {};
    grams2.reserve(len2);
    var intersection = 0;
    for i in 0..len2 - N + 1 {
        let gram = ((s2[i] as u32) << 24) | ((s2[i + 1] as u32) << 16) | ((s2[i + 2] as u32) << 8) | s2[i + 3];
        let count2: i32& = grams2.get_or_insert(gram, 0);
        count2 += 1;
        let count1 = grams1.get(gram);
        if count1 && count2 <= count1 { intersection++; }
    }

    let total = grams1.count + grams2.count;
    return if total > 0 { intersection as f64 / total } else { 0.0 };
}

fn run_ngram(b: Bench&) -> u32 {
    let count = config_i64(b.name, "count");
    let size = config_i64(b.name, "size");
    let pairs = generate_pair_strings(count, size);
    var result: u32 = 0;
    measure(b) {
        for p in pairs { result += (ngram(p.s1, p.s2) * 1000.0) as u32; }
    };
    return result;
}
