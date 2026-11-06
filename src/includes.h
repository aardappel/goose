// Goose compiler — external includes. All project headers assume this comes
// first, in the order established by main.cpp.
#pragma once

#include <assert.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace goose {
using namespace std;
}  // namespace goose
