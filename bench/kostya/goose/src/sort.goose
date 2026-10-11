// Sort::Quick, Sort::Merge and Sort::Self: one array of random integers,
// sorted afresh every run by a hand-written quicksort, a hand-written
// top-down merge sort, and the standard library's sort.

namespace sort;

import std;
import bench;
import helper;

// The reference's SortBenchmark, the base of each sort's benchmark: an array
// of random integers made once.
struct SortBenchmark { size_val: i64, result_val: u32, data: i32[>..] }

fn new_base() -> SortBenchmark { SortBenchmark { size_val: 0, result_val: 0, data: [] } }

fn prepare(base: SortBenchmark&, class_name: u8[:]) {
    if base.size_val == 0 {
        base.size_val = config_i64(class_name, "size");
        for i in base.size_val { base.data.push(next_int(1000000)); }
    }
}

// --- Sort::Quick ----------------------------------------------------------------

recursive fn quick_sort(arr: i32[:]) {
    if arr.len <= 1 { return; }
    let pivot = arr[arr.len / 2];
    var i = 0;
    var j = arr.len - 1;
    while i <= j {
        while arr[i] < pivot { i++; }
        while arr[j] > pivot { j--; }
        if i <= j {
            arr.swap_at(i, j);
            i++;
            j--;
        }
    }
    if j > 0 { quick_sort(arr[0..j + 1]); }
    if i < arr.len { quick_sort(arr[i..]); }
}

struct SortQuick { base: SortBenchmark }

fn new_sort_quick() -> SortQuick { SortQuick { base: new_base() } }

fn prepare(s: SortQuick&) { s.base.prepare("Sort::Quick"); }

fn run(s: SortQuick&, iteration_id: i64) {
    s.base.result_val += s.base.data[next_int(s.base.size_val)] as! u32;
    var t = copy(s.base.data);
    quick_sort(t);
    s.base.result_val += t[next_int(s.base.size_val)] as! u32;
}

fn checksum(s: SortQuick&) -> u32 { s.base.result_val }

// --- Sort::Merge ----------------------------------------------------------------

fn merge_sort_inplace(arr: i32[:]) {
    var temp: i32[>..] = [];
    temp.resize(arr.len, 0);
    merge_sort_helper(arr, temp, 0, arr.len - 1);
}

recursive fn merge_sort_helper(arr: i32[:], temp: i32[:], left: i64, right: i64) {
    if left >= right { return; }
    let mid = (left + right) / 2;
    merge_sort_helper(arr, temp, left, mid);
    merge_sort_helper(arr, temp, mid + 1, right);
    merge(arr, temp, left, mid, right);
}

fn merge(arr: i32[:], temp: i32[:], left: i64, mid: i64, right: i64) {
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

struct SortMerge { base: SortBenchmark }

fn new_sort_merge() -> SortMerge { SortMerge { base: new_base() } }

fn prepare(s: SortMerge&) { s.base.prepare("Sort::Merge"); }

fn run(s: SortMerge&, iteration_id: i64) {
    s.base.result_val += s.base.data[next_int(s.base.size_val)] as! u32;
    var t = copy(s.base.data);
    merge_sort_inplace(t);
    s.base.result_val += t[next_int(s.base.size_val)] as! u32;
}

fn checksum(s: SortMerge&) -> u32 { s.base.result_val }

// --- Sort::Self -----------------------------------------------------------------

struct SortSelf { base: SortBenchmark }

fn new_sort_self() -> SortSelf { SortSelf { base: new_base() } }

fn prepare(s: SortSelf&) { s.base.prepare("Sort::Self"); }

fn run(s: SortSelf&, iteration_id: i64) {
    s.base.result_val += s.base.data[next_int(s.base.size_val)] as! u32;
    var t = copy(s.base.data);
    t.sort();
    s.base.result_val += t[next_int(s.base.size_val)] as! u32;
}

fn checksum(s: SortSelf&) -> u32 { s.base.result_val }
