// Hash::SHA256 and Hash::CRC32: one buffer of random bytes, hashed whole on
// every run, the hash added to the checksum.

namespace buffer_hash;

import std;
import binary;
import bench;
import helper;

// The size bytes both benchmarks hash, drawn from the shared generator.
fn random_data(b: Bench&) -> u8[>..] {
    let size = config_i64(b.name, "size");
    var data: u8[>..] = [];
    for i in size { data.push(next_int(256) as! u8); }
    return data;
}

// --- Hash::SHA256 ---------------------------------------------------------------

// Not SHA-256 itself: eight 32-bit lanes seeded with SHA-256's initial hash
// values, byte i stirred into lane i % 8 by a shift-add-xor mix, and the
// lanes written out big-endian as a 32-byte digest.
fn simple_sha256(data: u8[:]) -> u8[32] {
    var hashes: u32[8] = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
    for byte, i in data {
        let lane = i % 8;
        var hash = hashes[lane];
        hash = (hash << 5) + hash + byte;
        hash = (hash + (hash << 10)) ^ (hash >> 6);
        hashes[lane] = hash;
    }
    var digest: u8[32] = [0; 32];
    for hash, i in hashes {
        digest[i * 4] = (hash >> 24) as! u8;
        digest[i * 4 + 1] = (hash >> 16) as! u8;
        digest[i * 4 + 2] = (hash >> 8) as! u8;
        digest[i * 4 + 3] = hash as! u8;
    }
    return digest;
}

fn sha256(b: Bench&) -> u32 {
    let data = random_data(b);
    var checksum: u32 = 0;
    measure(b) {
        let digest = simple_sha256(data);
        let hash, ok = binary::read_u32le(digest, 0);
        checksum += hash;
    };
    return checksum;
}

// --- Hash::CRC32 ----------------------------------------------------------------

// The reflected CRC-32 (polynomial 0xEDB88320), a bit at a time.
fn crc32_of(data: u8[:]) -> u32 {
    var crc: u32 = 0xFFFFFFFF;
    for byte in data {
        crc ^= byte;
        for j in 8 {
            if crc & 1 != 0 {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}

fn crc32(b: Bench&) -> u32 {
    let data = random_data(b);
    var checksum: u32 = 0;
    measure(b) { checksum += crc32_of(data); };
    return checksum;
}
