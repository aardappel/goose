// Etc::Sieve: the sieve of Eratosthenes over a fresh byte array per run.

namespace sieve;

import std;
import math;
import bench;

fn run(b: Bench&) -> u32 {
    let limit = config_i64(b.name, "limit");
    var checksum: u32 = 0;
    measure(b) {
        var primes: u8[>..] = [];
        primes.resize(limit + 1, 1);
        primes[0] = 0;
        primes[1] = 0;
        let sqrt_limit = sqrt(limit as f64) as i64;
        for p in 2..sqrt_limit + 1 {
            if primes[p] == 1 {
                var multiple = p * p;
                while multiple <= limit {
                    primes[multiple] = 0;
                    multiple += p;
                }
            }
        }
        var last_prime = 2;
        var count = 1;
        var n = 3;
        while n <= limit {
            if primes[n] == 1 {
                last_prime = n;
                count++;
            }
            n += 2;
        }
        checksum += (last_prime + count) as! u32;
    };
    return checksum;
}
