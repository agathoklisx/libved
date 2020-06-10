#include "led.h"

private void createVedClass(VM *vm) {
    ObjString *name = copyString(vm, "Ved", 3);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define Ved methods
     */
    defineNative(vm, &klass->methods, "strerror", strerrorNative);

    /**
     * Define Ved properties
     */

    defineNativeProperty(vm, &klass->properties, "errno", NUMBER_VAL(0));

    Table table = vm_get_globals (vm);
    tableSet(vm, &table, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}

public void __init_led__ (VM *vm) {
  createVedClass (vm);
}
