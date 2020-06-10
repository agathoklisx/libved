#ifndef LAPI_h
#define LAPI_h

typedef struct _vm VM;
typedef uint64_t Value;

typedef struct sObj Obj;
typedef struct sObjString ObjString;
typedef struct sObjList ObjList;
typedef struct sObjDict ObjDict;
typedef struct sObjSet  ObjSet;
typedef struct sObjFile ObjFile;

typedef enum {
    OBJ_BOUND_METHOD,
    OBJ_CLASS,
    OBJ_NATIVE_CLASS,
    OBJ_TRAIT,
    OBJ_CLOSURE,
    OBJ_FUNCTION,
    OBJ_INSTANCE,
    OBJ_NATIVE,
    OBJ_STRING,
    OBJ_LIST,
    OBJ_DICT,
    OBJ_SET,
    OBJ_FILE,
    OBJ_UPVALUE
} ObjType;

struct sObj {
    ObjType type;
    bool isDark;
    struct sObj *next;
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

#define OBJ_TYPE(value)         (AS_OBJ(value)->type)

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

#define SIGN_BIT ((uint64_t)1 << 63)
#define QNAN ((uint64_t)0x7ffc000000000000)

#define TAG_NIL    1
#define TAG_FALSE  2
#define TAG_TRUE   3
#define TAG_EMPTY  4

#define IS_BOOL(v)    (((v) & FALSE_VAL) == FALSE_VAL)
#define IS_NIL(v)     ((v) == NIL_VAL)
#define IS_EMPTY(v)   ((v) == EMPTY_VAL)
#define IS_NUMBER(v)  (((v) & QNAN) != QNAN)
#define IS_OBJ(v)     (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#define AS_BOOL(v)    ((v) == TRUE_VAL)
#define AS_NUMBER(v)  valueToNum(v)
#define AS_OBJ(v)     ((Obj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))

#define BOOL_VAL(boolean)   ((boolean) ? TRUE_VAL : FALSE_VAL)
#define FALSE_VAL           ((Value)(uint64_t)(QNAN | TAG_FALSE))
#define TRUE_VAL            ((Value)(uint64_t)(QNAN | TAG_TRUE))
#define NIL_VAL             ((Value)(uint64_t)(QNAN | TAG_NIL))
#define EMPTY_VAL           ((Value)(uint64_t)(QNAN | TAG_EMPTY))
#define NUMBER_VAL(num)     numToValue(num)

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

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef Value (*NativeFn)(VM *vm, int argCount, Value *args);

#define OK     0
#define NOTOK -1
#define UNUSED(__x__) (void) __x__

VM *initVM(bool repl, const char *scriptName, int argc, const char *argv[]);

void freeVM(VM *vm);

InterpretResult interpret(VM *vm, const char *source);

void push(VM *vm, Value value);

Value peek(VM *vm, int distance);

void runtimeError(VM *vm, const char *format, ...);

Value pop(VM *vm);

bool isFalsey(Value value);

typedef VM Lstate;

typedef struct l_table_t {
  bool (*get) (Table *, ObjString *, Value *);
} l_table_t;

typedef struct l_t {
  l_table_t table;

  Lstate *(*init) (const char *, int, const char **);

  void
    (*deinit) (Lstate **),
    (*defineProp) (Lstate *, Table *, const char *, Value),
    (*defineFun) (Lstate *, Table *, const char *, NativeFn);

  InterpretResult (*compile) (Lstate *, const char *);
  ObjString *(*newString) (Lstate *, const char *, int);

} l_t;

typedef struct lang_t {
  l_t self;
  Lstate **states;
  int num_states;
  int cur_state;
} lang_t;

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

char *readFile(const char *path);
ObjString *copyString(VM *vm, const char *chars, int length);
bool tableGet(Table *table, ObjString *key, Value *value);
Value strerrorGeneric(VM *vm, int error);
ObjDict *initDict(VM *vm);
bool dictSet(VM *vm, ObjDict *dict, Value key, Value value);
ObjClassNative *newClassNative(VM *vm, ObjString *name);
void defineNative(VM *vm, Table *table, const char *name, NativeFn function);
bool tableSet(VM *vm, Table *table, ObjString *key, Value value);
void defineNativeProperty(VM *vm, Table *table, const char *name, Value value);
void defineNative(VM *vm, Table *table, const char *name, NativeFn function);

/* extensions */
Table vm_get_globals(VM *vm);
Value strerrorNative(VM *vm, int argCount, Value *args);
size_t vm_sizeof (void);
#endif /* LAPI */
