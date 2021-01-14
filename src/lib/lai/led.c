#include "led.h"

private Value validateU8Native(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "validate() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "validate() argument must be a string");
        return EMPTY_VAL;
    }

    ObjString *str = AS_STRING(args[0]);

    char *message;
    int num_faultbytes;
    return NUMBER_VAL(Ustring.validate ((unsigned char *) str->chars, str->length, &message, &num_faultbytes));
}

private Value getcodeAtU8Native(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "getcode() takes 2 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "getcode() first argument must be a string");
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[1])) {
        runtimeError(vm, "getcode() seconde argument must be a number");
        return EMPTY_VAL;
    }


    ObjString *str = AS_STRING(args[0]);
    int idx = AS_NUMBER(args[1]);
    int len = 0;
    int code = Ustring.get.code_at (str->chars, str->length, idx, &len);
    return NUMBER_VAL(code);
}

private Value charU8Native(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "char() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "char() argument must be a number");
        return EMPTY_VAL;
    }

    int c = AS_NUMBER(args[0]);
    int len = 0;
    char buf[8];
    Ustring.character (c, buf, &len);
    return OBJ_VAL(copyString(vm, buf, len));
}

ObjModule *createU8Module(DictuVM *vm) {
    ObjString *name = copyString(vm, "U8", 2);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define U8 methods
     */
    defineNative(vm, &module->values, "validate", validateU8Native);
    defineNative(vm, &module->values, "get_code_at", getcodeAtU8Native);
    defineNative(vm, &module->values, "character", charU8Native);

    Table *table = vm_get_globals (vm);
    tableSet(vm, table, name, OBJ_VAL(module));
    pop(vm);
    pop(vm);

    return module;
}

#if 0
ObjModule *createEdModule (DictuVM *vm) {
    ObjString *name = copyString(vm, "Ed", 2);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Ed methods
     */
    defineNative(vm, &module->values, "strerror", strerrorNative);

    /**
     * Define Ed properties
     */

    defineNativeProperty(vm, &module->values, "errno", NUMBER_VAL(0));

    Table *table = vm_get_globals (vm);
    tableSet(vm, table, name, OBJ_VAL(module));
    pop(vm);
    pop(vm);

    return module;
}

typedef struct {
  char mode;
  struct termios orig_mode;
  struct termios raw_mode;
} TermType;

#define AS_TERM(v) ((TermType*)AS_ABSTRACT(v)->data)

private Value orig_modeTerm (DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "orig_mode() takes one argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    TermType *term = AS_TERM(args[0]);

    if (term->mode == 'o')
        return NUMBER_VAL(OK);

    while (NOTOK == tcsetattr (STDIN_FILENO, TCSAFLUSH, &term->orig_mode)) {
        if (errno != EINTR) {
            SET_ERRNO(GET_SELF_CLASS);
            return NUMBER_VAL(NOTOK);
        }
    }

    term->mode = 'o';
    return NUMBER_VAL(OK);
}

private Value raw_modeTerm (DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "raw_mode() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    TermType *term = AS_TERM(args[0]);

    if (term->mode == 'r')
        return NUMBER_VAL(OK);

    if (0 == isatty (STDIN_FILENO)) {
        SET_ERRNO(GET_SELF_CLASS);
        return NUMBER_VAL(NOTOK);
    }

    while (NOTOK == tcgetattr (STDIN_FILENO, &term->orig_mode)) {
        if (errno != EINTR) {
            SET_ERRNO(GET_SELF_CLASS);
           return NUMBER_VAL(NOTOK);
        }
    }

    term->raw_mode = term->orig_mode;
    term->raw_mode.c_iflag &= ~(INLCR|ICRNL|IXON|ISTRIP);
    term->raw_mode.c_cflag |= (CS8);
    term->raw_mode.c_oflag &= ~(OPOST);
    term->raw_mode.c_lflag &= ~(ECHO|ISIG|ICANON|IEXTEN);
    term->raw_mode.c_lflag &= NOFLSH;
    term->raw_mode.c_cc[VEOF] = 1;
    term->raw_mode.c_cc[VMIN] = 0;   /* 1 */
    term->raw_mode.c_cc[VTIME] = 1;  /* 0 */

    while (NOTOK == tcsetattr (STDIN_FILENO, TCSAFLUSH, &term->raw_mode)) {
        if (errno != EINTR) {
            SET_ERRNO(GET_SELF_CLASS);
           return NUMBER_VAL(NOTOK);
        }
    }

    term->mode = 'r';
    return NUMBER_VAL(OK);
}

private void releaseTerm (DictuVM *vm, ObjAbstract *abstract) {
  FREE(vm, TermType, abstract->data);
}

ObjAbstract *initTerm(DictuVM *vm) {
    ObjAbstract *abstract = initAbstract(vm, releaseTerm);
    push(vm, OBJ_VAL(abstract));

    TermType *term = ALLOCATE(vm, TermType, 1);
    term->mode = 's';

    abstract->data = term;

    defineNative(vm, &abstract->values, "raw_mode", raw_modeTerm);
    defineNative(vm, &abstract->values, "orig_mode", orig_modeTerm);
    pop(vm);

    return abstract;
}

private Value newTerm (DictuVM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "new() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjAbstract *s = initTerm(vm);
    return OBJ_VAL(s);
}

private ObjModule *createTermModule (DictuVM *vm) {
    ObjString *name = copyString(vm, "Term", 4);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Term methods
     */
    defineNative(vm, &module->values, "strerror", strerrorNative);
    defineNative(vm, &module->values, "new", newTerm);

    /**
     * Define Term properties
     */

    defineNativeProperty(vm, &module->values, "errno", NUMBER_VAL(0));

    Table *table = vm_get_globals (vm);
    tableSet(vm, table, name, OBJ_VAL(module));
    pop(vm);
    pop(vm);

    return module;
}
#endif

public void __init_led__ (DictuVM *vm) {
//  createEdModule(vm);
  createU8Module(vm);
//  createTermModule(vm);
}
