/* The header of c_import_skipped.goose: a function taking a union by
   value, which `import c` leaves out. */
union cis_u { int i; float f; };
int cis_take(union cis_u u);
