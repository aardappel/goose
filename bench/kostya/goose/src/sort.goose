// Sort::Quick, Sort::Merge and Sort::Self: one array of random integers,
// sorted afresh every run by a hand-written quicksort, a hand-written
// top-down merge sort, and the standard library's sort.

namespace sort;

import std;
import bench;
import helper;

// The reference's SortBenchmark: every run sorts a copy of `data` with S,
// and adds one element picked at random before the sort and one picked
// after it to the result.
fn run<S>(b: Bench&) -> u32 {
    let size = config_i64(b.name, "size");
    var data: i32[>..] = [];
    for i in size { data.push(next_int(1000000)); }
    var result: u32 = 0;
    measure(b) {
        result += data[next_int(size)] as! u32;
        var t = copy(data);
        S(t);
        result += t[next_int(size)] as! u32;
    };
    return result;
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

fn run_quick(b: Bench&) -> u32 { run(b, quick_sort) }

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

fn run_merge(b: Bench&) -> u32 { run(b, merge_sort_inplace) }

// --- Sort::Self -----------------------------------------------------------------

fn run_self(b: Bench&) -> u32 { run(b, sort) }
