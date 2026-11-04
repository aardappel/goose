/* The header of c_import_cfn_signature.goose: a callback C calls with its
   user data and a value. */
typedef int (*cfs_cb)(void *ud, int v);
int cfs_each(cfs_cb f, void *ud);
