#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <libved.h>
#include <libved+.h>

private void __tcc_free (tcc_t **thisp) {
  if (NULL is thisp) return;

  tcc_t *this = *thisp;
  ifnot (NULL is this->handler) {
    tcc_delete (this->handler);
    this->handler = NULL;
  }

  free (this);
  thisp = NULL;
}

private tcc_t *__tcc_new (void) {
  tcc_t *this = AllocType (tcc);
  this->handler = tcc_new ();
  return this;
}

private int tcc_set_path (tcc_t *this, char *path, int type) {
  switch (type) {
    case TCC_CONFIG_TCC_DIR:
      tcc_set_lib_path (this->handler, path);
      return 0;

    case TCC_ADD_INC_PATH:
      return tcc_add_include_path (this->handler, path);

    case TCC_ADD_SYS_INC_PATH:
      return tcc_add_sysinclude_path (this->handler, path);

    case TCC_ADD_LPATH:
      return tcc_add_library_path (this->handler, path);

    case TCC_ADD_LIB:
      return tcc_add_library (this->handler, path);

    case TCC_SET_OUTPUT_PATH:
      return tcc_output_file (this->handler, path);

    default:
      return NOTOK;
    }

  return NOTOK;
}

private void __tcc_set_options (tcc_t *this, char *opt) {
  tcc_set_options (this->handler, opt);
}

private int __tcc_set_output_type (tcc_t *this, int type) {
  return (this->retval = tcc_set_output_type (this->handler, type));
}

private void tcc_set_error_handler (tcc_t *this, void *obj, TCCErrorFunc cb) {
  tcc_set_error_func (this->handler, obj, cb);
}

private int __tcc_compile_string (tcc_t *this, char *src) {
  return (this->retval = tcc_compile_string (this->handler, src));
}

private int tcc_compile_file (tcc_t *this, char *file) {
  return (this->retval = tcc_add_file (this->handler, file));
}

private int __tcc_run (tcc_t *this, int argc, char **argv) {
  return (this->retval = tcc_run (this->handler, argc, argv));
}

private int __tcc_relocate (tcc_t *this, void *mem) {
  return (this->retval = tcc_relocate (this->handler, mem));
}

public tcc_T __init_tcc__ (void) {
  return ClassInit (tcc,
    .self = SelfInit (tcc,
      .free = __tcc_free,
      .new = __tcc_new,
      .compile_string = __tcc_compile_string,
      .compile_file = tcc_compile_file,
      .run = __tcc_run,
      .relocate = __tcc_relocate,
      .set = SubSelfInit (tcc, set,
        .path = tcc_set_path,
        .output_type = __tcc_set_output_type,
        .options = __tcc_set_options,
        .error_handler = tcc_set_error_handler
      )
    )
  );
}

public void __deinit_tcc__ (tcc_T **thisp) {
  (void) thisp;
}
