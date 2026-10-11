// Hash::SHA256 and Hash::CRC32: one buffer of random bytes, hashed whole on
// every run, the hash added to the checksum.

namespace buffer_hash;

import std;
import binary;
import bench;
import helper;

// The reference's BufferHashBenchmark, the base of both hash benchmarks: the
// size bytes they hash, drawn from the shared generator once.
struct BufferHashBenchmark { size_val: i64, result_val: u32, prepared: bool, data: u8[>..] }

fn new_base() -> BufferHashBenchmark {
    BufferHashBenchmark { size_val: 0, result_val: 0, prepared: false, data: [] }
}

fn prepare(base: BufferHashBenchmark&, class_name: u8[:]) {
    if !base.prepared {
        base.size_val = config_i64(class_name, "size");
        base.data.resize(base.size_val, 0);
        for i in base.data.len { base.data[i] = next_int(256) as! u8; }
        base.prepared = true;
    }
}

// --- Hash::SHA256 ---------------------------------------------------------------

// SimpleSHA256::digest. Not SHA-256 itself: eight 32-bit lanes seeded with
// SHA-256's initial hash values, byte i stirred into lane i % 8 by a
// shift-add-xor mix, and the lanes written out big-endian as a 32-byte digest.
fn simple_sha256_digest(data: u8[:]) -> u8[32] {
    var result: u8[32] = [0; 32];
    var hashes: u32[8] = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
    for byte, i in data {
        let hash_idx = i % 8;
        var hash = hashes[hash_idx];
        hash = (hash << 5) + hash + byte;
        hash = (hash + (hash << 10)) ^ (hash >> 6);
        hashes[hash_idx] = hash;
    }
    for i in 8 {
        let hash = hashes[i];
        result[i * 4] = (hash >> 24) as! u8;
        result[i * 4 + 1] = (hash >> 16) as! u8;
        result[i * 4 + 2] = (hash >> 8) as! u8;
        result[i * 4 + 3] = hash as! u8;
    }
    return result;
}

struct BufferHashSHA256 { base: BufferHashBenchmark }

fn new_buffer_hash_sha256() -> BufferHashSHA256 { BufferHashSHA256 { base: new_base() } }

fn prepare(s: BufferHashSHA256&) { s.base.prepare("Hash::SHA256"); }

fn run(s: BufferHashSHA256&, iteration_id: i64) {
    let digest = simple_sha256_digest(s.base.data);
    let hash, ok = binary::read_u32le(digest, 0);
    s.base.result_val += hash;
}

fn checksum(s: BufferHashSHA256&) -> u32 { s.base.result_val }

// --- Hash::CRC32 ----------------------------------------------------------------

// The reflected CRC-32 (polynomial 0xEDB88320), a bit at a time.
fn crc32(data: u8[:]) -> u32 {
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

struct BufferHashCRC32 { base: BufferHashBenchmark }

fn new_buffer_hash_crc32() -> BufferHashCRC32 { BufferHashCRC32 { base: new_base() } }

fn prepare(s: BufferHashCRC32&) { s.base.prepare("Hash::CRC32"); }

fn run(s: BufferHashCRC32&, iteration_id: i64) {
    let hash = crc32(s.base.data);
    s.base.result_val += hash;
}

fn checksum(s: BufferHashCRC32&) -> u32 { s.base.result_val }
