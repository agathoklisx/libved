DeclareType (expr);
DeclareSelf (expr);

typedef void (*ExprFree_cb) (expr_t *);
typedef int  (*ExprEval_cb) (expr_t *);
typedef int  (*ExprInterp_cb) (expr_t *);
typedef int  (*ExprCompile_cb) (expr_t *);
typedef char *(*ExprStrError_cb) (expr_t *, int);

NewType (expr,
  int retval;
  int error;

  string_t
    *lang,
    *data,
    *error_string;

  union {
    double double_v;
       int int_v;
  } val;

  void *i_obj;
  void *ff_obj;

  ExprEval_cb eval;
  ExprInterp_cb interp;
  ExprCompile_cb compile;
  ExprFree_cb free;
  ExprStrError_cb strerror;
);

NewSelf (expr,
  expr_t
    *(*new) (char *, ExprCompile_cb, ExprEval_cb, ExprInterp_cb, ExprFree_cb);
  void
    (*free) (expr_t **);

  char
    *(*strerror) (expr_t *, int);

  int
    (*interp) (expr_t *),
    (*eval) (expr_t *),
    (*compile) (expr_t *);
);

NewClass (expr,
  Self (expr) self;
);


public expr_T __init_expr__ (void);
public void __deinit_expr__ (expr_T *);

#define EXPR_OK            OK
#define EXPR_NOTOK         NOTOK
#define EXPR_BASE_ERROR    2000
#define EXPR_COMPILE_NOREF (EXPR_BASE_ERROR + 1)
#define EXPR_EVAL_NOREF    (EXPR_BASE_ERROR + 2)
#define EXPR_INTERP_NOREF  (EXPR_BASE_ERROR + 3)
#define EXPR_COMPILE_ERROR (EXPR_BASE_ERROR + 4)
#define EXPR_EVAL_ERROR    (EXPR_BASE_ERROR + 5)
#define EXPR_INTERP_ERROR  (EXPR_BASE_ERROR + 6)
#define EXPR_LAST_ERROR    EXPR_INTERP_ERROR
#define EXPR_OUT_OF_BOUNDS_ERROR EXPR_LAST_ERROR + 1

private void expr_free (expr_t **thisp) {
  String.free ((*thisp)->data);
  String.free ((*thisp)->lang);
  String.free ((*thisp)->error_string);

  ifnot (NULL is (*thisp)->free)
    (*thisp)->free ((*thisp));

  free (*thisp);
  thisp = NULL;
}

private expr_t *expr_new (char *lang, ExprCompile_cb compile,
    ExprEval_cb eval, ExprInterp_cb interp, ExprFree_cb free_ref) {
  expr_t *this = AllocType (expr);
  this->retval = this->error = EXPR_OK;
  this->lang = String.new_with (lang);
  this->error_string = String.new (8);
  this->compile = compile;
  this->eval = eval;
  this->interp = interp;
  this->free = free_ref;
  return this;
}

private char *expr_strerror (expr_t *this, int error) {
  if (error > EXPR_LAST_ERROR)
    this->error = EXPR_OUT_OF_BOUNDS_ERROR;
  else
    this->error = error;

  char *expr_errors[] = {
    "NULL Function Reference (compile)",
    "NULL Function Reference (eval)",
    "NULL Function Reference (interp)",
    "Compilation ERROR",
    "Evaluation ERROR",
    "Interpretation ERROR",
    "INTERNAL ERROR, NO SUCH_ERROR, ERROR IS OUT OF BOUNDS"};

  String.append (this->error_string, expr_errors[this->error - EXPR_BASE_ERROR - 1]);

  ifnot (NULL is this->strerror) return this->strerror (this, error);

  return this->error_string->bytes;
}

private int expr_compile (expr_t *this) {
  if (NULL is this->compile) {
    this->retval = EXPR_NOTOK;
    expr_strerror (this, EXPR_COMPILE_NOREF);
    return EXPR_NOTOK;
  }

  return this->compile (this);
}

private int expr_eval (expr_t *this) {
  if (NULL is this->eval) {
    this->retval = EXPR_NOTOK;
    expr_strerror (this, EXPR_EVAL_NOREF);
    return EXPR_NOTOK;
  }

  return this->eval (this);
}

private int expr_interp (expr_t *this) {
  if (NULL is this->interp) {
    this->retval = EXPR_NOTOK;
    expr_strerror (this, EXPR_INTERP_NOREF);
    return EXPR_NOTOK;
  }

  return this->interp (this);
}

public expr_T __init_expr__ (void) {
  return ClassInit (expr,
    .self = SelfInit (expr,
      .new = expr_new,
      .free = expr_free,
      .compile = expr_compile,
      .interp = expr_interp,
      .eval = expr_eval,
      .strerror = expr_strerror
    )
  );
}

public void __deinit_expr__ (expr_T *this) {
  (void) this;
}
