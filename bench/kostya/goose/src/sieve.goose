// Etc::Sieve: the sieve of Eratosthenes over a fresh byte array per run.

namespace sieve;

import std;
import math;
import bench;

struct Sieve { limit: i64, checksum: u32 }

fn new_sieve() -> Sieve { Sieve { limit: config_i64("Etc::Sieve", "limit"), checksum: 0 } }

fn prepare(s: Sieve&) {}

fn run(s: Sieve&, iteration_id: i64) {
    let limit = s.limit;

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

    s.checksum += (last_prime + count) as! u32;
}

fn checksum(s: Sieve&) -> u32 { s.checksum }
