#ifndef LAPI_h
#define LAPI_h

typedef struct _vm DictuVM;

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} DictuInterpretResult;

DictuVM *dictuInitVM(bool repl, int argc, char *argv[]);

void dictuFreeVM(DictuVM *vm);

DictuInterpretResult dictuInterpret(DictuVM *vm, char *moduleName, char *source);

typedef uint64_t Value;

typedef struct sObj Obj;
typedef struct sObjString ObjString;
typedef struct sObjList ObjList;
typedef struct sObjDict ObjDict;
typedef struct sObjSet  ObjSet;
typedef struct sObjFile ObjFile;
typedef struct sObjAbstract ObjAbstract;

typedef enum {
    OBJ_MODULE,
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_LIST,
    OBJ_DICT,
    OBJ_SET,
    OBJ_FILE,
    OBJ_ABSTRACT,
    OBJ_UPVALUE
} ObjType;

struct sObj {
    ObjType type;
    bool isDark;
    struct sObj *next;
};

struct sObjString {
    Obj obj;
    int length;
    char *chars;
    uint32_t hash;
};

typedef struct {
    ObjString *key;
    Value value;
    uint32_t psl;
} Entry;

typedef struct {
    int count;
    int capacityMask;
    Entry *entries;
} Table;

typedef struct sObjClassNative {
    Obj obj;
    ObjString *name;
    Table methods;
    Table properties;
} ObjClassNative;

typedef struct {
    Obj obj;
    ObjString* name;
    ObjString* path;
    Table values;
} ObjModule;

typedef ObjModule *(*BuiltinModule)(DictuVM *vm);
typedef struct {
    char *name;
    BuiltinModule module;
} BuiltinModules;

typedef void (*AbstractFreeFn)(DictuVM *vm, ObjAbstract *abstract);

struct sObjAbstract {
    Obj obj;
    Table values;
    void *data;
    AbstractFreeFn func;
};

#define SIGN_BIT ((uint64_t)1 << 63)
#define QNAN ((uint64_t)0x7ffc000000000000)

#define TAG_NIL    1
#define TAG_FALSE  2
#define TAG_TRUE   3
#define TAG_EMPTY  4

#define BOOL_VAL(boolean)   ((boolean) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL           ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL            ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL             ((Value)(uint64_t)(QNAN | TAG_NIL))
#define EMPTY_VAL           ((Value)(uint64_t)(QNAN | TAG_EMPTY))
#define NUMBER_VAL(num)     numToValue(num)

#define OBJ_TYPE(value)         (AS_OBJ(value)->type)

#define IS_MODULE(value)        isObjType(value, OBJ_MODULE)
#define IS_BOUND_METHOD(value)  isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)         isObjType(value, OBJ_CLASS)
#define IS_NATIVE_CLASS(value)  isObjType(value, OBJ_NATIVE_CLASS)
#define IS_TRAIT(value)         isObjType(value, OBJ_TRAIT)
#define IS_CLOSURE(value)       isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)      isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value)      isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value)        isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)        isObjType(value, OBJ_STRING)
#define IS_LIST(value)          isObjType(value, OBJ_LIST)
#define IS_DICT(value)          isObjType(value, OBJ_DICT)
#define IS_SET(value)           isObjType(value, OBJ_SET)
#define IS_FILE(value)          isObjType(value, OBJ_FILE)
#define IS_BOOL(v)              (((v) & FALSE_VAL) == FALSE_VAL)
#define IS_NIL(v)               ((v) == NIL_VAL)
#define IS_EMPTY(v)             ((v) == EMPTY_VAL)
#define IS_NUMBER(v)            (((v) & QNAN) != QNAN)
#define IS_OBJ(v)               (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_MODULE(value)        ((ObjModule*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)         ((ObjClass*)AS_OBJ(value))
#define AS_CLASS_NATIVE(value)  ((ObjClassNative*)AS_OBJ(value))
#define AS_TRAIT(value)         ((ObjTrait*)AS_OBJ(value))
#define AS_CLOSURE(value)       ((ObjClosure*)AS_OBJ(value))
#define AS_FUNCTION(value)      ((ObjFunction*)AS_OBJ(value))
#define AS_INSTANCE(value)      ((ObjInstance*)AS_OBJ(value))
#define AS_NATIVE(value)        (((ObjNative*)AS_OBJ(value))->function)
#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
#define AS_LIST(value)          ((ObjList*)AS_OBJ(value))
#define AS_DICT(value)          ((ObjDict*)AS_OBJ(value))
#define AS_SET(value)           ((ObjSet*)AS_OBJ(value))
#define AS_FILE(value)          ((ObjFile*)AS_OBJ(value))
#define AS_BOOL(v)              ((v) == TRUE_VAL)
#define AS_NUMBER(v)            valueToNum(v)
#define AS_OBJ(v)               ((Obj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))
#define AS_ABSTRACT(value)      ((ObjAbstract*)AS_OBJ(value))

#define OBJ_VAL(obj) \
    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

typedef union {
    uint64_t bits64;
    uint32_t bits32[2];
    double num;
} DoubleUnion;

static inline double valueToNum(Value value) {
    DoubleUnion data;
    data.bits64 = value;
    return data.num;
}

static inline Value numToValue(double num) {
    DoubleUnion data;
    data.num = num;
    return data.bits64;
}

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

static inline ObjType getObjType(Value value) {
    return AS_OBJ(value)->type;
}

typedef Value (*NativeFn)(DictuVM *vm, int argCount, Value *args);

#define FREE(vm, type, pointer) \
    reallocate(vm, pointer, sizeof(type), 0)

#define ALLOCATE(vm, type, count) \
    (type*)reallocate(vm, NULL, 0, sizeof(type) * (count))

#define OK     0
#define NOTOK -1
#define UNUSED(__x__) (void) __x__

#define GET_SELF_CLASS \
  AS_CLASS_NATIVE(args[-1])

#define SET_ERRNO(klass_)                                              \
  defineNativeProperty(vm, &klass_->properties, "errno", NUMBER_VAL(errno))

#define GET_ERRNO(klass_)({                          \
  Value errno_value = 0;                             \
  ObjString *name = copyString(vm, "errno", 5);      \
  tableGet(&klass_->properties, name, &errno_value); \
  errno_value;                                       \
})

DictuInterpretResult dictuInterpret(DictuVM *vm, char *module, char *source);

void push(DictuVM *vm, Value value);

Value peek(DictuVM *vm, int distance);

void runtimeError(DictuVM *vm, const char *format, ...);

Value pop(DictuVM *vm);

bool isFalsey(Value value);

typedef DictuVM Lstate;

typedef struct l_table_get_t {
  Value *(*value) (Lstate *, Table *, ObjString *, Value *);

  Table
    *(*globals) (Lstate *),
    *(*module)  (Lstate *, char *, int);

} l_table_get_t;

typedef struct l_table_t {
  l_table_get_t get;
} l_table_t;

typedef struct l_module_t {
  ObjModule *(*get) (Lstate *, char *, int len);
} l_module_t;

typedef struct l_t {
  l_table_t table;
  l_module_t module;

  Lstate *(*init) (const char *, int, const char **);

  void
    (*deinit) (Lstate **),
    (*defineProp) (Lstate *, Table *, const char *, Value),
    (*defineFun) (Lstate *, Table *, const char *, NativeFn);

  size_t (*vmsize) (void);

  DictuInterpretResult (*compile) (Lstate *, char *, char *);
  ObjString *(*newString) (Lstate *, const char *, int);

} l_t;

typedef struct lang_t {
  l_t self;
  Lstate **states;
  int num_states;
  int cur_state;
} lang_t;

char *readFile(DictuVM *vm, const char *path);
ObjString *copyString(DictuVM *vm, const char *chars, int length);
bool tableGet(Table *table, ObjString *key, Value *value);
Value strerrorGeneric(DictuVM *vm, int error);
ObjDict *initDict(DictuVM *vm);
bool dictSet(DictuVM *vm, ObjDict *dict, Value key, Value value);
ObjClassNative *newClassNative(DictuVM *vm, ObjString *name);
void defineNative(DictuVM *vm, Table *table, const char *name, NativeFn function);
bool tableSet(DictuVM *vm, Table *table, ObjString *key, Value value);
void defineNativeProperty(DictuVM *vm, Table *table, const char *name, Value value);
void defineNative(DictuVM *vm, Table *table, const char *name, NativeFn function);
void *reallocate(DictuVM *vm, void *previous, size_t oldSize, size_t newSize);
ObjAbstract *initAbstract(DictuVM *vm, AbstractFreeFn func);
ObjModule *newModule(DictuVM *vm, ObjString *name);
int findBuiltinModule(char *name, int length);
ObjModule *importBuiltinModule(DictuVM *vm, int index);

/* extensions */
Table *vm_get_globals(DictuVM *vm);
ObjModule *vm_module_get(DictuVM *vm, char *name, int len);
Table *vm_get_module_table(DictuVM *vm, char *name, int len);
Value *vm_table_get_value(DictuVM *vm, Table *table, ObjString *obj, Value *value);
Value strerrorNative(DictuVM *vm, int argCount, Value *args);
size_t vm_sizeof (void);
#endif /* LAPI */
