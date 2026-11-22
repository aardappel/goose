/* The C side of export_from_worker.goose: calls the program's export back
   before it returns. */
#include <stdint.h>
int64_t on_worker_callback(int64_t n);
static inline int64_t call_back(int64_t n) { return on_worker_callback(n); }
