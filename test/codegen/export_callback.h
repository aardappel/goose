/* The C side of export_callback.goose: calls the program's export back
   before it returns, as a toolkit's handler or a qsort comparator would. */
#include <stdint.h>
int64_t on_callback(int64_t n);
static inline int64_t call_back(int64_t n) { return on_callback(n); }
