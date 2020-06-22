#include "led.h"

private Value validateU8Native(VM *vm, int argCount, Value *args) {
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

private Value getcodeAtU8Native(VM *vm, int argCount, Value *args) {
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

private Value charU8Native(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "char() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "char() argument must be a number");
        return EMPTY_VAL;
    }

    int c = AS_NUMBER(args[1]);
    int len = 0;
    char buf[8];
    Ustring.character (c, buf, &len);
    return OBJ_VAL(copyString(vm, "buf", len));
}

private void createU8Class(VM *vm) {
    ObjString *name = copyString(vm, "U8", 2);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define U8 methods
     */
    defineNative(vm, &klass->methods, "strerror", strerrorNative);
    defineNative(vm, &klass->methods, "validate", validateU8Native);
    defineNative(vm, &klass->methods, "get_code_at", getcodeAtU8Native);
    defineNative(vm, &klass->methods, "character", charU8Native);

    /**
     * Define U8 properties
     */

    defineNativeProperty(vm, &klass->properties, "errno", NUMBER_VAL(0));

    Table *table = vm_get_globals (vm);
    tableSet(vm, table, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}

private void createEdClass(VM *vm) {
    ObjString *name = copyString(vm, "Ed", 2);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define Ed methods
     */
    defineNative(vm, &klass->methods, "strerror", strerrorNative);

    /**
     * Define Ed properties
     */

    defineNativeProperty(vm, &klass->properties, "errno", NUMBER_VAL(0));

    Table *table = vm_get_globals (vm);
    tableSet(vm, table, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}

public void __init_led__ (VM *vm) {
  createEdClass(vm);
  createU8Class(vm);
}
