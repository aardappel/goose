// Sort::Quick, Sort::Merge and Sort::Self: one array of random integers,
// sorted afresh every run by a hand-written quicksort, a hand-written
// top-down merge sort, and the standard library's sort.

namespace sort;

import std;
import bench;
import helper;

// Every run sorts a copy of `data` with S, and adds one element picked at
// random before the sort and one picked after it to the checksum.
fn run<S>(b: Bench&) -> u32 {
    let size = config_i64(b.name, "size");
    var data: i32[>..] = [];
    for i in size { data.push(next_int(1000000)); }
    var checksum: u32 = 0;
    measure(b) {
        checksum += data[next_int(size)] as! u32;
        var t = copy(data);
        S(t);
        checksum += t[next_int(size)] as! u32;
    };
    return checksum;
}

// --- Sort::Quick ----------------------------------------------------------------

// Hoare partition around the middle element, then both sides.
fn quick_sort(arr: i32[:]) {
    // Sorts arr[lo..hi].
    recursive fn sort_range(lo: i64, hi: i64) {
        if hi - lo <= 1 { return; }
        let pivot = arr[lo + (hi - lo) / 2];
        var i = lo;
        var j = hi - 1;
        while i <= j {
            while arr[i] < pivot { i++; }
            while arr[j] > pivot { j--; }
            if i <= j {
                swap_at(arr, i, j);
                i++;
                j--;
            }
        }
        if j > lo { sort_range(lo, j + 1); }
        if i < hi { sort_range(i, hi); }
    }
    sort_range(0, arr.len);
}

fn run_quick(b: Bench&) -> u32 { run(b, quick_sort) }

// --- Sort::Merge ----------------------------------------------------------------

// Top-down merge sort through a scratch array as long as the input.
fn merge_sort(arr: i32[:]) {
    var temp: i32[>..] = [];
    temp.resize(arr.len, 0);

    // Sorts arr[left..=right].
    recursive fn sort_range(left: i64, right: i64) {
        if left >= right { return; }
        let mid = (left + right) / 2;
        sort_range(left, mid);
        sort_range(mid + 1, right);
        merge(left, mid, right);
    }

    // Merges the sorted runs arr[left..=mid] and arr[mid+1..=right] through temp.
    fn merge(left: i64, mid: i64, right: i64) {
        copy_into(temp[left..right + 1], arr[left..right + 1]);
        var i = left;
        var j = mid + 1;
        var k = left;
        while i <= mid && j <= right {
            if temp[i] <= temp[j] {
                arr[k] = temp[i];
                i++;
            } else {
                arr[k] = temp[j];
                j++;
            }
            k++;
        }
        while i <= mid {
            arr[k] = temp[i];
            i++;
            k++;
        }
    }

    sort_range(0, arr.len - 1);
}

fn run_merge(b: Bench&) -> u32 { run(b, merge_sort) }

// --- Sort::Self -----------------------------------------------------------------

// std's sort is overloaded, so it is not a function value of its own.
fn std_sort(arr: i32[:]) { sort(arr); }

fn run_self(b: Bench&) -> u32 { run(b, std_sort) }
