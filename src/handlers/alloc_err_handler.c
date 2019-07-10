/* one idea is to hold with a question, to give some time to the
 * user to free resources and retry; in that case the signature
 * should change to an int as return value plus the Alloc* macros */
mutable public void __alloc_error_handler__ (
int err, size_t size,  char *file, const char *func, int line) {
  fprintf (stderr, "MEMORY_ALLOCATION_ERROR\n");
  fprintf (stderr, "File: %s\nFunction: %s\nLine: %d\n", file, func, line);
  fprintf (stderr, "Size: %zd\n", size);

  if (err is INTEGEROVERFLOW_ERROR)
    fprintf (stderr, "Error: Integer Overflow Error\n");
  else
    fprintf (stderr, "Error: Not Enouch Memory\n");

  ifnot (NULL is E) __deinit_ved__ (E);
  exit (1);
}

