typedef struct _vm DictuVM;
typedef enum {
INTERPRET_OK,
INTERPRET_COMPILE_ERROR,
INTERPRET_RUNTIME_ERROR
} DictuInterpretResult;

    /* 47: vm/common.h */
#ifndef dictu_common_h
#define dictu_common_h


#define UNUSED(__x__) (void) __x__

#define MAX_ERROR_LEN 256

#define ERROR_RESULT             \
do {                             \
char buf[MAX_ERROR_LEN];         \
getStrerror(buf, errno);         \
return newResultError(vm, buf);  \
} while (false)

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
#define DIR_ALT_SEPARATOR '/'
#define DIR_SEPARATOR_AS_STRING "\\"
#define DIR_SEPARATOR_STRLEN 1
#define PATH_DELIMITER ';'
#define PATH_DELIMITER_AS_STRING ";"
#define PATH_DELIMITER_STRLEN 1
#else
#define HAS_REALPATH
#define DIR_SEPARATOR '/'
#define DIR_SEPARATOR_AS_STRING "/"
#define DIR_SEPARATOR_STRLEN 1
#define PATH_DELIMITER ':'
#define PATH_DELIMITER_AS_STRING ":"
#define PATH_DELIMITER_STRLEN 1
#endif

#ifdef DIR_ALT_SEPARATOR
#define IS_DIR_SEPARATOR(c) ((c) == DIR_SEPARATOR || (c) == DIR_ALT_SEPARATOR)
#else
#define IS_DIR_SEPARATOR(c) (c == DIR_SEPARATOR)
#endif

#define NAN_TAGGING
#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION
#define DEBUG_TRACE_GC
#define DEBUG_TRACE_MEM

#ifndef _MSC_VER
#define COMPUTED_GOTO
#endif

#undef DEBUG_PRINT_CODE
#undef DEBUG_TRACE_EXECUTION
#undef DEBUG_TRACE_GC
#undef DEBUG_TRACE_MEM

// #define DEBUG_STRESS_GC
// #define DEBUG_FINAL_MEM

#define UINT8_COUNT (UINT8_MAX + 1)

// typedef struct _vm DictuVM;

#endif

    /* 48: vm/value.h */
#ifndef dictu_value_h
#define dictu_value_h


typedef struct sObj Obj;
typedef struct sObjString ObjString;
typedef struct sObjList ObjList;
typedef struct sObjDict ObjDict;
typedef struct sObjSet  ObjSet;
typedef struct sObjFile ObjFile;
typedef struct sObjAbstract ObjAbstract;
typedef struct sObjResult ObjResult;

// A mask that selects the sign bit.
#define SIGN_BIT ((uint64_t)1 << 63)

// The bits that must be set to indicate a quiet NaN.
#define QNAN ((uint64_t)0x7ffc000000000000)

// Tag values for the different singleton values.
#define TAG_NIL    1
#define TAG_FALSE  2
#define TAG_TRUE   3
#define TAG_EMPTY  4

typedef uint64_t Value;

#define IS_BOOL(v)    (((v) | 1) == TRUE_VAL)
#define IS_NIL(v)     ((v) == NIL_VAL)
#define IS_EMPTY(v)   ((v) == EMPTY_VAL)
// If the NaN bits are set, it's not a number.
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
#define NUMBER_VAL(num)   numToValue(num)
// The triple casting is necessary here to satisfy some compilers:
// 1. (uintptr_t) Convert the pointer to a number of the right size.
// 2. (uint64_t)  Pad it up to 64 bits in 32-bit builds.
// 3. Or in the bits to make a tagged Nan.
// 4. Cast to a typedef'd value.
#define OBJ_VAL(obj) \
    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))

// A union to let us reinterpret a double as raw bits and back.
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

typedef struct {
    int capacity;
    int count;
    Value *values;
} ValueArray;

bool valuesEqual(Value a, Value b);

void initValueArray(ValueArray *array);

void writeValueArray(DictuVM *vm, ValueArray *array, Value value);

void freeValueArray(DictuVM *vm, ValueArray *array);

void grayDict(DictuVM *vm, ObjDict *dict);

bool dictSet(DictuVM *vm, ObjDict *dict, Value key, Value value);

bool dictGet(ObjDict *dict, Value key, Value *value);

bool dictDelete(DictuVM *vm, ObjDict *dict, Value key);

bool setGet(ObjSet *set, Value value);

bool setInsert(DictuVM *vm, ObjSet *set, Value value);

bool setDelete(DictuVM *vm, ObjSet *set, Value value);

void graySet(DictuVM *vm, ObjSet *set);

char *valueToString(Value value);

char *valueTypeToString(DictuVM *vm, Value value, int *length);

void printValue(Value value);

#endif

    /* 49: vm/chunk.h */
#ifndef dictu_chunk_h
#define dictu_chunk_h


typedef struct {
    int count;
    int capacity;
    uint8_t *code;
    int *lines;
    ValueArray constants;
} Chunk;

typedef enum {
    #define OPCODE(name) OP_##name,
    #include "opcodes.h"
    #undef OPCODE
} OpCode;

void initChunk(DictuVM *vm, Chunk *chunk);

void freeChunk(DictuVM *vm, Chunk *chunk);

void writeChunk(DictuVM *vm, Chunk *chunk, uint8_t byte, int line);

int addConstant(DictuVM *vm, Chunk *chunk, Value value);

#endif

    /* 50: vm/table.h */
#ifndef dictu_table_h
#define dictu_table_h



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

void initTable(Table *table);

void freeTable(DictuVM *vm, Table *table);

bool tableGet(Table *table, ObjString *key, Value *value);

bool tableSet(DictuVM *vm, Table *table, ObjString *key, Value value);

bool tableDelete(DictuVM *vm, Table *table, ObjString *key);

void tableAddAll(DictuVM *vm, Table *from, Table *to);

ObjString *tableFindString(Table *table, const char *chars, int length,
                           uint32_t hash);

void tableRemoveWhite(DictuVM *vm, Table *table);

void grayTable(DictuVM *vm, Table *table);

#endif

    /* 51: vm/object.h */
#ifndef dictu_object_h
#define dictu_object_h



#define OBJ_TYPE(value)         (AS_OBJ(value)->type)

#define AS_MODULE(value)        ((ObjModule*)AS_OBJ(value))
#define AS_BOUND_METHOD(value)  ((ObjBoundMethod*)AS_OBJ(value))
#define AS_CLASS(value)         ((ObjClass*)AS_OBJ(value))
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
#define AS_ABSTRACT(value)      ((ObjAbstract*)AS_OBJ(value))
#define AS_RESULT(value)        ((ObjResult*)AS_OBJ(value))

#define IS_MODULE(value)          isObjType(value, OBJ_MODULE)
#define IS_BOUND_METHOD(value)    isObjType(value, OBJ_BOUND_METHOD)
#define IS_CLASS(value)           isObjType(value, OBJ_CLASS)
#define IS_DEFAULT_CLASS(value)   isObjType(value, OBJ_CLASS) && AS_CLASS(value)->type == CLASS_DEFAULT
#define IS_TRAIT(value)           isObjType(value, OBJ_CLASS) && AS_CLASS(value)->type == CLASS_TRAIT
#define IS_CLOSURE(value)         isObjType(value, OBJ_CLOSURE)
#define IS_FUNCTION(value)        isObjType(value, OBJ_FUNCTION)
#define IS_INSTANCE(value)        isObjType(value, OBJ_INSTANCE)
#define IS_NATIVE(value)          isObjType(value, OBJ_NATIVE)
#define IS_STRING(value)          isObjType(value, OBJ_STRING)
#define IS_LIST(value)            isObjType(value, OBJ_LIST)
#define IS_DICT(value)            isObjType(value, OBJ_DICT)
#define IS_SET(value)             isObjType(value, OBJ_SET)
#define IS_FILE(value)            isObjType(value, OBJ_FILE)
#define IS_ABSTRACT(value)        isObjType(value, OBJ_ABSTRACT)
#define IS_RESULT(value)          isObjType(value, OBJ_RESULT)

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
    OBJ_RESULT,
    OBJ_UPVALUE
} ObjType;

typedef enum {
    CLASS_DEFAULT,
    CLASS_ABSTRACT,
    CLASS_TRAIT
} ClassType;

typedef enum {
    TYPE_FUNCTION,
    TYPE_ARROW_FUNCTION,
    TYPE_INITIALIZER,
    TYPE_METHOD,
    TYPE_STATIC,
    TYPE_ABSTRACT,
    TYPE_TOP_LEVEL
} FunctionType;

struct sObj {
    ObjType type;
    bool isDark;
    struct sObj *next;
};

typedef struct {
    Obj obj;
    ObjString* name;
    ObjString* path;
    Table values;
} ObjModule;

typedef struct {
    Obj obj;
    int arity;
    int arityOptional;
    int upvalueCount;
    Chunk chunk;
    ObjString *name;
    FunctionType type;
    ObjModule* module;
    int propertyCount;
    int *propertyNames;
    int *propertyIndexes;
} ObjFunction;

typedef Value (*NativeFn)(DictuVM *vm, int argCount, Value *args);

typedef struct {
    Obj obj;
    NativeFn function;
} ObjNative;

struct sObjString {
    Obj obj;
    int length;
    char *chars;
    uint32_t hash;
};

struct sObjList {
    Obj obj;
    ValueArray values;
};

typedef struct {
    Value key;
    Value value;
    uint32_t psl;
} DictItem;

struct sObjDict {
    Obj obj;
    int count;
    int capacityMask;
    DictItem *entries;
};

typedef struct {
    Value value;
    bool deleted;
} SetItem;

struct sObjSet {
    Obj obj;
    int count;
    int capacityMask;
    SetItem *entries;
};

struct sObjFile {
    Obj obj;
    FILE *file;
    char *path;
    char *openType;
};

typedef void (*AbstractFreeFn)(DictuVM *vm, ObjAbstract *abstract);

struct sObjAbstract {
    Obj obj;
    Table values;
    void *data;
    AbstractFreeFn func;
};

typedef enum {
    SUCCESS,
    ERR
} ResultStatus;

struct sObjResult {
    Obj obj;
    ResultStatus status;
    Value value;
};

typedef struct sUpvalue {
    Obj obj;

    // Pointer to the variable this upvalue is referencing.
    Value *value;

    // If the upvalue is closed (i.e. the local variable it was pointing
    // to has been popped off the stack) then the closed-over value is
    // hoisted out of the stack into here. [value] is then be changed to
    // point to this.
    Value closed;

    // Open upvalues are stored in a linked list. This points to the next
    // one in that list.
    struct sUpvalue *next;
} ObjUpvalue;

typedef struct {
    Obj obj;
    ObjFunction *function;
    ObjUpvalue **upvalues;
    int upvalueCount;
} ObjClosure;

typedef struct sObjClass {
    Obj obj;
    ObjString *name;
    struct sObjClass *superclass;
    Table methods;
    Table abstractMethods;
    Table properties;
    ClassType type;
} ObjClass;

typedef struct {
    Obj obj;
    ObjClass *klass;
    Table fields;
} ObjInstance;

typedef struct {
    Obj obj;
    Value receiver;
    ObjClosure *method;
} ObjBoundMethod;

ObjModule *newModule(DictuVM *vm, ObjString *name);

ObjBoundMethod *newBoundMethod(DictuVM *vm, Value receiver, ObjClosure *method);

ObjClass *newClass(DictuVM *vm, ObjString *name, ObjClass *superclass, ClassType type);

ObjClosure *newClosure(DictuVM *vm, ObjFunction *function);

ObjFunction *newFunction(DictuVM *vm, ObjModule *module, FunctionType type);

ObjInstance *newInstance(DictuVM *vm, ObjClass *klass);

ObjNative *newNative(DictuVM *vm, NativeFn function);

ObjString *takeString(DictuVM *vm, char *chars, int length);

ObjString *copyString(DictuVM *vm, const char *chars, int length);

ObjList *newList(DictuVM *vm);

ObjDict *newDict(DictuVM *vm);

ObjSet *newSet(DictuVM *vm);

ObjFile *newFile(DictuVM *vm);

ObjAbstract *newAbstract(DictuVM *vm, AbstractFreeFn func);

ObjResult *newResult(DictuVM *vm, ResultStatus status, Value value);

Value newResultSuccess(DictuVM *vm, Value value);

Value newResultError(DictuVM *vm, char *errorMsg);

ObjUpvalue *newUpvalue(DictuVM *vm, Value *slot);

char *setToString(Value value);
char *dictToString(Value value);
char *listToString(Value value);
char *classToString(Value value);
char *instanceToString(Value value);
char *objectToString(Value value);

static inline bool isObjType(Value value, ObjType type) {
    return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

static inline ObjType getObjType(Value value) {
    return AS_OBJ(value)->type;
}

#endif

    /* 52: vm/scanner.h */
#ifndef dictu_scanner_h
#define dictu_scanner_h

typedef enum {
    // Single-character tokens.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_QUESTION,

    TOKEN_QUESTION_DOT,
    TOKEN_PLUS_EQUALS, TOKEN_MINUS_EQUALS,
    TOKEN_MULTIPLY_EQUALS, TOKEN_DIVIDE_EQUALS,

    TOKEN_SEMICOLON, TOKEN_COLON, TOKEN_SLASH,
    TOKEN_STAR, TOKEN_STAR_STAR,
    TOKEN_PERCENT,

    // Bitwise
    TOKEN_AMPERSAND, TOKEN_CARET, TOKEN_PIPE,
    TOKEN_AMPERSAND_EQUALS, TOKEN_CARET_EQUALS, TOKEN_PIPE_EQUALS,

    // One or two character tokens.
    TOKEN_BANG, TOKEN_BANG_EQUAL,
    TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER, TOKEN_GREATER_EQUAL,
    TOKEN_LESS, TOKEN_LESS_EQUAL,
    TOKEN_R, TOKEN_ARROW,

    // Literals.
    TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_NUMBER,

    // Keywords.
    TOKEN_CLASS, TOKEN_ABSTRACT, TOKEN_TRAIT, TOKEN_USE, TOKEN_STATIC,
    TOKEN_THIS, TOKEN_SUPER, TOKEN_DEF, TOKEN_AS,
    TOKEN_IF, TOKEN_AND, TOKEN_ELSE, TOKEN_OR,
    TOKEN_VAR, TOKEN_CONST, TOKEN_TRUE, TOKEN_FALSE, TOKEN_NIL,
    TOKEN_FOR, TOKEN_WHILE, TOKEN_BREAK,
    TOKEN_RETURN, TOKEN_CONTINUE,
    TOKEN_WITH, TOKEN_EOF, TOKEN_IMPORT, TOKEN_FROM,
    TOKEN_ERROR

} TokenType;

typedef struct {
    TokenType type;
    const char *start;
    int length;
    int line;
} Token;

typedef struct {
    const char *start;
    const char *current;
    int line;
    bool rawString;
} Scanner;

void initScanner(Scanner *scanner, const char *source);

void backTrack(Scanner *scanner);

Token scanToken(Scanner *scanner);

#endif

    /* 53: vm/compiler.h */
#ifndef dictu_compiler_h
#define dictu_compiler_h



typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_BITWISE_OR,  // bitwise or
    PREC_BITWISE_XOR, // bitwise xor
    PREC_BITWISE_AND, // bitwise and
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_INDICES,     // **
    PREC_UNARY,       // ! -
    PREC_PREFIX,      // ++ --
    PREC_CHAIN,       // ?.
    PREC_CALL,        // . () []
    PREC_PRIMARY
} Precedence;

typedef struct {
    // The name of the local variable.
    Token name;

    // The depth in the scope chain that this variable was declared at.
    // Zero is the outermost scope--parameters for a method, or the first
    // local block in top level code. One is the scope within that, etc.
    int depth;

    // True if this local variable is captured as an upvalue by a
    // function.
    bool isUpvalue;

    // True if it's a constant value.
    bool constant;
} Local;

typedef struct {
    // The index of the local variable or upvalue being captured from the
    // enclosing function.
    uint8_t index;

    // Whether the captured variable is a local or upvalue in the
    // enclosing function.
    bool isLocal;
} Upvalue;

typedef struct ClassCompiler {
    struct ClassCompiler *enclosing;
    Token name;
    bool hasSuperclass;
    bool staticMethod;
    bool abstractClass;
} ClassCompiler;

typedef struct Loop {
    struct Loop *enclosing;
    int start;
    int body;
    int end;
    int scopeDepth;
} Loop;

typedef struct {
    DictuVM *vm;
    Scanner scanner;
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
    ObjModule *module;
} Parser;

typedef struct Compiler {
    Parser *parser;
    Table stringConstants;

    struct Compiler *enclosing;
    ClassCompiler *class;
    Loop *loop;

    ObjFunction *function;
    FunctionType type;

    Local locals[UINT8_COUNT];

    int localCount;
    Upvalue upvalues[UINT8_COUNT];

    int scopeDepth;
} Compiler;

typedef void (*ParsePrefixFn)(Compiler *compiler, bool canAssign);
typedef void (*ParseInfixFn)(Compiler *compiler, Token previousToken, bool canAssign);

typedef struct {
    ParsePrefixFn prefix;
    ParseInfixFn infix;
    Precedence precedence;
} ParseRule;

ObjFunction *compile(DictuVM *vm, ObjModule *module, const char *source);

void grayCompilerRoots(DictuVM *vm);

#endif

    /* 54: vm/vm.h */
#ifndef dictu_vm_h
#define dictu_vm_h


// TODO: Work out the maximum stack size at compilation time
#define STACK_MAX (64 * UINT8_COUNT)

typedef struct {
    ObjClosure *closure;
    uint8_t *ip;
    Value *slots;
} CallFrame;

struct _vm {
    Compiler *compiler;
    Value stack[STACK_MAX];
    Value *stackTop;
    bool repl;
    CallFrame *frames;
    int frameCount;
    int frameCapacity;
    ObjModule *lastModule;
    Table modules;
    Table globals;
    Table constants;
    Table strings;
    Table numberMethods;
    Table boolMethods;
    Table nilMethods;
    Table stringMethods;
    Table listMethods;
    Table dictMethods;
    Table setMethods;
    Table fileMethods;
    Table classMethods;
    Table instanceMethods;
    Table resultMethods;
    ObjString *initString;
    ObjString *replVar;
    ObjUpvalue *openUpvalues;
    size_t bytesAllocated;
    size_t nextGC;
    Obj *objects;
    int grayCount;
    int grayCapacity;
    Obj **grayStack;
};

#define OK     0
#define NOTOK -1

void push(DictuVM *vm, Value value);

Value peek(DictuVM *vm, int distance);

void runtimeError(DictuVM *vm, const char *format, ...);

Value pop(DictuVM *vm);

bool isFalsey(Value value);

#endif

    /* 55: vm/natives.h */
#ifndef dictu_natives_h
#define dictu_natives_h


void defineAllNatives(DictuVM *vm);

#endif //dictu_natives_h

    /* 56: vm/memory.h */
#ifndef dictu_memory_h
#define dictu_memory_h


#define ALLOCATE(vm, type, count) \
    (type*)reallocate(vm, NULL, 0, sizeof(type) * (count))

#define FREE(vm, type, pointer) \
    reallocate(vm, pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define SHRINK_CAPACITY(capacity) \
    ((capacity) < 16 ? 8 : (capacity) / 2)

#define GROW_ARRAY(vm, previous, type, oldCount, count) \
    (type*)reallocate(vm, previous, sizeof(type) * (oldCount), \
        sizeof(type) * (count))

#define SHRINK_ARRAY(vm, previous, type, oldCount, count) \
    (type*)reallocate(vm, previous, sizeof(type) * (oldCount), \
        sizeof(type) * (count))

#define FREE_ARRAY(vm, type, pointer, oldCount) \
    reallocate(vm, pointer, sizeof(type) * (oldCount), 0)

void *reallocate(DictuVM *vm, void *previous, size_t oldSize, size_t newSize);

void grayObject(DictuVM *vm, Obj *object);

void grayValue(DictuVM *vm, Value value);

void collectGarbage(DictuVM *vm);

void freeObjects(DictuVM *vm);

void freeObject(DictuVM *vm, Obj *object);

#endif

    /* 57: vm/util.h */
#ifndef dictu_util_h
#define dictu_util_h


char *readFile(DictuVM *vm, const char *path);

ObjString *getDirectory(DictuVM *vm, char *source);

void defineNative(DictuVM *vm, Table *table, const char *name, NativeFn function);

void defineNativeProperty(DictuVM *vm, Table *table, const char *name, Value value);

bool isValidKey(Value value);

Value boolNative(DictuVM *vm, int argCount, Value *args);

ObjString *dirname(DictuVM *vm, char *path, int len);

bool resolvePath(char *directory, char *path, char *ret);

#endif //dictu_util_h

    /* 58: bool.h */
#ifndef dictu_bool_h
#define dictu_bool_h


void declareBoolMethods(DictuVM *vm);

#endif //dictu_bool_h

    /* 59: class.h */
#ifndef dictu_class_h
#define dictu_class_h



void declareClassMethods(DictuVM *vm);

#endif //dictu_class_h

    /* 60: copy.h */
#ifndef dictu_copy_h
#define dictu_copy_h



ObjList *copyList(DictuVM *vm, ObjList *oldList, bool shallow);

ObjDict *copyDict(DictuVM *vm, ObjDict *oldDict, bool shallow);

ObjInstance *copyInstance(DictuVM *vm, ObjInstance *oldInstance, bool shallow);

#endif //dictu_copy_h

    /* 61: dicts.h */
#ifndef dictu_dicts_h
#define dictu_dicts_h



void declareDictMethods(DictuVM *vm);

#endif //dictu_dicts_h

    /* 62: files.h */
#ifndef dictu_files_h
#define dictu_files_h



void declareFileMethods(DictuVM *vm);

#endif //dictu_files_h

    /* 63: instance.h */
#ifndef dictu_instance_h
#define dictu_instance_h



void declareInstanceMethods(DictuVM *vm);

#endif //dictu_instance_h

    /* 64: result.h */
#ifndef dictu_result_h
#define dictu_result_h


void declareResultMethods(DictuVM *vm);

#endif //dictu_result_h

    /* 65: lists/list-source.h */
#define DICTU_LIST_SOURCE "/**\n" \
" * This file contains all the methods for Lists written in Dictu that\n" \
" * are unable to be written in C due to re-enterability issues.\n" \
" *\n" \
" * We should always strive to write methods in C where possible.\n" \
" */\n" \
"def map(list, func) {\n" \
"    const temp = [];\n" \
"\n" \
"    for (var i = 0; i < list.len(); i += 1) {\n" \
"        temp.push(func(list[i]));\n" \
"    }\n" \
"\n" \
"    return temp;\n" \
"}\n" \
"\n" \
"def filter(list, func=def(x) => x) {\n" \
"    const temp = [];\n" \
"\n" \
"    for (var i = 0; i < list.len(); i += 1) {\n" \
"        const result = func(list[i]);\n" \
"        if (result) {\n" \
"            temp.push(list[i]);\n" \
"        }\n" \
"    }\n" \
"\n" \
"    return temp;\n" \
"}\n" \
"\n" \
"def reduce(list, func, initial=0) {\n" \
"    var accumulator = initial;\n" \
"\n" \
"    for(var i = 0; i < list.len(); i += 1) {\n" \
"        accumulator = func(accumulator, list[i]);\n" \
"    }\n" \
"\n" \
"    return accumulator;\n" \
"}\n" \


    /* 66: lists/lists.h */
#ifndef dictu_lists_h
#define dictu_lists_h



void declareListMethods(DictuVM *vm);

#endif //dictu_lists_h

    /* 67: nil.h */
#ifndef dictu_nil_h
#define dictu_nil_h


void declareNilMethods(DictuVM *vm);

#endif //dictu_nil_h

    /* 68: number.h */
#ifndef dictu_number_h
#define dictu_number_h


void declareNumberMethods(DictuVM *vm);

#endif //dictu_number_h

    /* 69: sets.h */
#ifndef dictu_sets_h
#define dictu_sets_h



void declareSetMethods(DictuVM *vm);

#endif //dictu_sets_h

    /* 70: strings.h */
#ifndef dictu_strings_h
#define dictu_strings_h



void declareStringMethods(DictuVM *vm);

#endif //dictu_strings_h

    /* 71: optionals.h */
#ifndef dictu_optionals_h
#define dictu_optionals_h


typedef ObjModule *(*BuiltinModule)(DictuVM *vm);

typedef struct {
    char *name;
    BuiltinModule module;
} BuiltinModules;

ObjModule *importBuiltinModule(DictuVM *vm, int index);

int findBuiltinModule(char *name, int length);

#endif //dictu_optionals_h

    /* 72: base64/base64Lib.h */
/*
	base64.c - by Joe DF (joedf@ahkscript.org)
	Released under the MIT License

	Revision: 2015-06-12 01:26:51

	Thank you for inspiration:
	http://www.codeproject.com/Tips/813146/Fast-base-functions-for-encode-decode
*/


//Base64 char table function - used internally for decoding
unsigned int b64_int(unsigned int ch);

// in_size : the number bytes to be encoded.
// Returns the recommended memory size to be allocated for the output buffer excluding the null byte
unsigned int b64e_size(unsigned int in_size);

// in_size : the number bytes to be decoded.
// Returns the recommended memory size to be allocated for the output buffer
unsigned int b64d_size(unsigned int in_size);

// in : buffer of "raw" binary to be encoded.
// in_len : number of bytes to be encoded.
// out : pointer to buffer with enough memory, user is responsible for memory allocation, receives null-terminated string
// returns size of output including null byte
unsigned int b64_encode(const unsigned char* in, unsigned int in_len, unsigned char* out);

// in : buffer of base64 string to be decoded.
// in_len : number of bytes to be decoded.
// out : pointer to buffer with enough memory, user is responsible for memory allocation, receives "raw" binary
// returns size of output excluding null byte
unsigned int b64_decode(const unsigned char* in, unsigned int in_len, unsigned char* out);

// file-version b64_encode
// Input : filenames
// returns size of output
unsigned int b64_encodef(char *InFile, char *OutFile);

// file-version b64_decode
// Input : filenames
// returns size of output
unsigned int b64_decodef(char *InFile, char *OutFile);
    /* 73: base64.h */
#ifndef dictu_base64_h
#define dictu_base64_h


ObjModule *createBase64Module(DictuVM *vm);

#endif //dictu_base64_h

    /* 74: c.h */
#ifndef dictu_c_h
#define dictu_c_h

#ifndef _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#if (_POSIX_C_SOURCE >= 200112L)
#define POSIX_STRERROR
#endif
#endif


// error codes taken from https://github.com/torvalds/linux/blob/master/include/uapi/asm-generic/errno.h
#ifdef __FreeBSD__
#define EHWPOISON 133
#define ENOSTR 60
#define ENODATA 61
#define ETIME 62
#define ENOSR 63
#endif


#ifdef __APPLE__
#define LAST_ERROR 106
#elif defined(_WIN32)
#define LAST_ERROR EWOULDBLOCK
#else
#define LAST_ERROR EHWPOISON
#endif


void createCModule(DictuVM *vm);

void getStrerror(char *buf, int error);

#endif //dictu_c_h

    /* 75: datetime.h */
#ifndef dictu_datetime_h
#define dictu_datetime_h

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#ifndef __USE_XOPEN
#define __USE_XOPEN
#endif



ObjModule *createDatetimeModule(DictuVM *vm);

#endif //dictu_datetime_h

    /* 76: env.h */
#ifndef dictu_env_h
#define dictu_env_h



ObjModule *createEnvModule(DictuVM *vm);

#endif //dictu_env_h

    /* 77: hashlib/constants.h */
/* constants.h - TinyCrypt interface to constants */

/*
 *  Copyright (C) 2017 by Intel Corporation, All Rights Reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *    - Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *    - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *    - Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * @brief -- Interface to constants.
 *
 */

#ifndef __TC_CONSTANTS_H__
#define __TC_CONSTANTS_H__

#ifdef __cplusplus
extern "C" {
#endif


#ifndef NULL
#define NULL ((void *)0)
#endif

#define TC_CRYPTO_SUCCESS 1
#define TC_CRYPTO_FAIL 0

#define TC_ZERO_BYTE 0x00

#ifdef __cplusplus
}
#endif

#endif /* __TC_CONSTANTS_H__ */
    /* 78: hashlib/utils.h */
/* utils.h - TinyCrypt interface to platform-dependent run-time operations */

/*
 *  Copyright (C) 2017 by Intel Corporation, All Rights Reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *    - Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *    - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *    - Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * @brief Interface to platform-dependent run-time operations.
 *
 */

#ifndef __TC_UTILS_H__
#define __TC_UTILS_H__


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Copy the the buffer 'from' to the buffer 'to'.
 * @return returns TC_CRYPTO_SUCCESS (1)
 *         returns TC_CRYPTO_FAIL (0) if:
 *                from_len > to_len.
 *
 * @param to OUT -- destination buffer
 * @param to_len IN -- length of destination buffer
 * @param from IN -- origin buffer
 * @param from_len IN -- length of origin buffer
 */
unsigned int _copy(uint8_t *to, unsigned int to_len,
                   const uint8_t *from, unsigned int from_len);

/**
 * @brief Set the value 'val' into the buffer 'to', 'len' times.
 *
 * @param to OUT -- destination buffer
 * @param val IN -- value to be set in 'to'
 * @param len IN -- number of times the value will be copied
 */
void _set(void *to, uint8_t val, unsigned int len);

/**
 * @brief Set the value 'val' into the buffer 'to', 'len' times, in a way
 *         which does not risk getting optimized out by the compiler
 *        In cases where the compiler does not set __GNUC__ and where the
 *         optimization level removes the memset, it may be necessary to
 *         implement a _set_secure function and define the
 *         TINYCRYPT_ARCH_HAS_SET_SECURE, which then can ensure that the
 *         memset does not get optimized out.
 *
 * @param to OUT -- destination buffer
 * @param val IN -- value to be set in 'to'
 * @param len IN -- number of times the value will be copied
 */
#ifdef TINYCRYPT_ARCH_HAS_SET_SECURE
extern void _set_secure(void *to, uint8_t val, unsigned int len);
#else /* ! TINYCRYPT_ARCH_HAS_SET_SECURE */
static inline void _set_secure(void *to, uint8_t val, unsigned int len)
{
    (void) memset(to, val, len);
#ifdef __GNUC__
    __asm__ __volatile__("" :: "g"(to) : "memory");
#endif /* __GNUC__ */
}
#endif /* TINYCRYPT_ARCH_HAS_SET_SECURE */

/*
 * @brief AES specific doubling function, which utilizes
 * the finite field used by AES.
 * @return Returns a^2
 *
 * @param a IN/OUT -- value to be doubled
 */
uint8_t _double_byte(uint8_t a);

/*
 * @brief Constant-time algorithm to compare if two sequences of bytes are equal
 * @return Returns 0 if equal, and non-zero otherwise
 *
 * @param a IN -- sequence of bytes a
 * @param b IN -- sequence of bytes b
 * @param size IN -- size of sequences a and b
 */
int _compare(const uint8_t *a, const uint8_t *b, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __TC_UTILS_H__ */
    /* 79: hashlib/sha256.h */
/* sha256.h - TinyCrypt interface to a SHA-256 implementation */

/*
 *  Copyright (C) 2017 by Intel Corporation, All Rights Reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *    - Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *    - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *    - Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * @brief Interface to a SHA-256 implementation.
 *
 *  Overview:   SHA-256 is a NIST approved cryptographic hashing algorithm
 *              specified in FIPS 180. A hash algorithm maps data of arbitrary
 *              size to data of fixed length.
 *
 *  Security:   SHA-256 provides 128 bits of security against collision attacks
 *              and 256 bits of security against pre-image attacks. SHA-256 does
 *              NOT behave like a random oracle, but it can be used as one if
 *              the string being hashed is prefix-free encoded before hashing.
 *
 *  Usage:      1) call tc_sha256_init to initialize a struct
 *              tc_sha256_state_struct before hashing a new string.
 *
 *              2) call tc_sha256_update to hash the next string segment;
 *              tc_sha256_update can be called as many times as needed to hash
 *              all of the segments of a string; the order is important.
 *
 *              3) call tc_sha256_final to out put the digest from a hashing
 *              operation.
 */

#ifndef __TC_SHA256_H__
#define __TC_SHA256_H__


#ifdef __cplusplus
extern "C" {
#endif

#define TC_SHA256_BLOCK_SIZE (64)
#define TC_SHA256_DIGEST_SIZE (32)
#define TC_SHA256_STATE_BLOCKS (TC_SHA256_DIGEST_SIZE/4)

struct tc_sha256_state_struct {
    unsigned int iv[TC_SHA256_STATE_BLOCKS];
    uint64_t bits_hashed;
    uint8_t leftover[TC_SHA256_BLOCK_SIZE];
    size_t leftover_offset;
};

typedef struct tc_sha256_state_struct *TCSha256State_t;

/**
 *  @brief SHA256 initialization procedure
 *  Initializes s
 *  @return returns TC_CRYPTO_SUCCESS (1)
 *          returns TC_CRYPTO_FAIL (0) if s == NULL
 *  @param s Sha256 state struct
 */
int tc_sha256_init(TCSha256State_t s);

/**
 *  @brief SHA256 update procedure
 *  Hashes data_length bytes addressed by data into state s
 *  @return returns TC_CRYPTO_SUCCESS (1)
 *          returns TC_CRYPTO_FAIL (0) if:
 *                s == NULL,
 *                s->iv == NULL,
 *                data == NULL
 *  @note Assumes s has been initialized by tc_sha256_init
 *  @warning The state buffer 'leftover' is left in memory after processing
 *           If your application intends to have sensitive data in this
 *           buffer, remind to erase it after the data has been processed
 *  @param s Sha256 state struct
 *  @param data message to hash
 *  @param datalen length of message to hash
 */
int tc_sha256_update (TCSha256State_t s, const uint8_t *data, size_t datalen);

/**
 *  @brief SHA256 final procedure
 *  Inserts the completed hash computation into digest
 *  @return returns TC_CRYPTO_SUCCESS (1)
 *          returns TC_CRYPTO_FAIL (0) if:
 *                s == NULL,
 *                s->iv == NULL,
 *                digest == NULL
 *  @note Assumes: s has been initialized by tc_sha256_init
 *        digest points to at least TC_SHA256_DIGEST_SIZE bytes
 *  @warning The state buffer 'leftover' is left in memory after processing
 *           If your application intends to have sensitive data in this
 *           buffer, remind to erase it after the data has been processed
 *  @param digest unsigned eight bit integer
 *  @param Sha256 state struct
 */
int tc_sha256_final(uint8_t *digest, TCSha256State_t s);

#ifdef __cplusplus
}
#endif

#endif /* __TC_SHA256_H__ */
    /* 80: hashlib/hmac.h */
/* hmac.h - TinyCrypt interface to an HMAC implementation */

/*
 *  Copyright (C) 2017 by Intel Corporation, All Rights Reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *    - Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *    - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 *    - Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * @brief Interface to an HMAC implementation.
 *
 *  Overview:   HMAC is a message authentication code based on hash functions.
 *              TinyCrypt hard codes SHA-256 as the hash function. A message
 *              authentication code based on hash functions is also called a
 *              keyed cryptographic hash function since it performs a
 *              transformation specified by a key in an arbitrary length data
 *              set into a fixed length data set (also called tag).
 *
 *  Security:   The security of the HMAC depends on the length of the key and
 *              on the security of the hash function. Note that HMAC primitives
 *              are much less affected by collision attacks than their
 *              corresponding hash functions.
 *
 *  Requires:   SHA-256
 *
 *  Usage:      1) call tc_hmac_set_key to set the HMAC key.
 *
 *              2) call tc_hmac_init to initialize a struct hash_state before
 *              processing the data.
 *
 *              3) call tc_hmac_update to process the next input segment;
 *              tc_hmac_update can be called as many times as needed to process
 *              all of the segments of the input; the order is important.
 *
 *              4) call tc_hmac_final to out put the tag.
 */

#ifndef __TC_HMAC_H__
#define __TC_HMAC_H__


#ifdef __cplusplus
extern "C" {
#endif

struct tc_hmac_state_struct {
    /* the internal state required by h */
    struct tc_sha256_state_struct hash_state;
    /* HMAC key schedule */
    uint8_t key[2*TC_SHA256_BLOCK_SIZE];
};
typedef struct tc_hmac_state_struct *TCHmacState_t;

/**
 *  @brief HMAC set key procedure
 *  Configures ctx to use key
 *  @return returns TC_CRYPTO_SUCCESS (1)
 *          returns TC_CRYPTO_FAIL (0) if
 *                ctx == NULL or
 *                key == NULL or
 *                key_size == 0
 * @param ctx IN/OUT -- the struct tc_hmac_state_struct to initial
 * @param key IN -- the HMAC key to configure
 * @param key_size IN -- the HMAC key size
 */
int tc_hmac_set_key(TCHmacState_t ctx, const uint8_t *key,
                    unsigned int key_size);

/**
 * @brief HMAC init procedure
 * Initializes ctx to begin the next HMAC operation
 * @return returns TC_CRYPTO_SUCCESS (1)
 *         returns TC_CRYPTO_FAIL (0) if: ctx == NULL or key == NULL
 * @param ctx IN/OUT -- struct tc_hmac_state_struct buffer to init
 */
int tc_hmac_init(TCHmacState_t ctx);

/**
 *  @brief HMAC update procedure
 *  Mixes data_length bytes addressed by data into state
 *  @return returns TC_CRYPTO_SUCCCESS (1)
 *          returns TC_CRYPTO_FAIL (0) if: ctx == NULL or key == NULL
 *  @note Assumes state has been initialized by tc_hmac_init
 *  @param ctx IN/OUT -- state of HMAC computation so far
 *  @param data IN -- data to incorporate into state
 *  @param data_length IN -- size of data in bytes
 */
int tc_hmac_update(TCHmacState_t ctx, const void *data,
                   unsigned int data_length);

/**
 *  @brief HMAC final procedure
 *  Writes the HMAC tag into the tag buffer
 *  @return returns TC_CRYPTO_SUCCESS (1)
 *          returns TC_CRYPTO_FAIL (0) if:
 *                tag == NULL or
 *                ctx == NULL or
 *                key == NULL or
 *                taglen != TC_SHA256_DIGEST_SIZE
 *  @note ctx is erased before exiting. This should never be changed/removed.
 *  @note Assumes the tag bufer is at least sizeof(hmac_tag_size(state)) bytes
 *  state has been initialized by tc_hmac_init
 *  @param tag IN/OUT -- buffer to receive computed HMAC tag
 *  @param taglen IN -- size of tag in bytes
 *  @param ctx IN/OUT -- the HMAC state for computing tag
 */
int tc_hmac_final(uint8_t *tag, unsigned int taglen, TCHmacState_t ctx);

#ifdef __cplusplus
}
#endif

#endif /*__TC_HMAC_H__*/
    /* 81: hashlib/bcrypt/bcrypt.h */
#ifndef _bcrypt_
#define _bcrypt_


#if defined(_WIN32)
typedef unsigned char uint8_t;
typedef uint8_t u_int8_t;
typedef unsigned short uint16_t;
typedef uint16_t u_int16_t;
typedef unsigned uint32_t;
typedef uint32_t u_int32_t;
typedef unsigned long long uint64_t;
typedef uint64_t u_int64_t;
#define snprintf _snprintf
#define __attribute__(unused)
#elif defined(__sun) || !defined(__GLIBC__)
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
#else
#endif

#define explicit_bzero(s,n) memset(s, 0, n)
#define DEF_WEAK(f)

char *bcrypt_pass(const char *pass, const char *salt);
char *bcrypt_gensalt(u_int8_t log_rounds);
int bcrypt_checkpass(const char *pass, const char *goodhash);
int timingsafe_bcmp(const void *b1, const void *b2, size_t n);

#endif


    /* 82: hashlib/bcrypt/blf.h */
/*	$OpenBSD: blf.h,v 1.6 2007/02/21 19:25:40 grunk Exp $	*/

/*
 * Blowfish - a fast block cipher designed by Bruce Schneier
 *
 * Copyright 1997 Niels Provos <provos@physnet.uni-hamburg.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _BLF_H_
#define _BLF_H_


/* Schneier states the maximum key length to be 56 bytes.
 * The way how the subkeys are initialized by the key up
 * to (N+2)*4 i.e. 72 bytes are utilized.
 * Warning: For normal blowfish encryption only 56 bytes
 * of the key affect all cipherbits.
 */

#define BLF_N	16			/* Number of Subkeys */
#define BLF_MAXKEYLEN ((BLF_N-2)*4)	/* 448 bits */
#define BLF_MAXUTILIZED ((BLF_N+2)*4)	/* 576 bits */

/* Blowfish context */
typedef struct BlowfishContext {
	u_int32_t S[4][256];	/* S-Boxes */
	u_int32_t P[BLF_N + 2];	/* Subkeys */
} blf_ctx;

/* Raw access to customized Blowfish
 *	blf_key is just:
 *	Blowfish_initstate( state )
 *	Blowfish_expand0state( state, key, keylen )
 */

void Blowfish_encipher(blf_ctx *, u_int32_t *);
void Blowfish_decipher(blf_ctx *, u_int32_t *);
void Blowfish_initstate(blf_ctx *);
void Blowfish_expand0state(blf_ctx *, const u_int8_t *, u_int16_t);
void Blowfish_expandstate(blf_ctx *, const u_int8_t *, u_int16_t, const u_int8_t *, u_int16_t);

/* Standard Blowfish */

void blf_key(blf_ctx *, const u_int8_t *, u_int16_t);
void blf_enc(blf_ctx *, u_int32_t *, u_int16_t);
void blf_dec(blf_ctx *, u_int32_t *, u_int16_t);

/* Converts u_int8_t to u_int32_t */
u_int32_t Blowfish_stream2word(const u_int8_t *, u_int16_t ,
				    u_int16_t *);

void blf_ecb_encrypt(blf_ctx *, u_int8_t *, u_int32_t);
void blf_ecb_decrypt(blf_ctx *, u_int8_t *, u_int32_t);

void blf_cbc_encrypt(blf_ctx *, u_int8_t *, u_int8_t *, u_int32_t);
void blf_cbc_decrypt(blf_ctx *, u_int8_t *, u_int8_t *, u_int32_t);
#endif

    /* 83: hashlib/bcrypt/portable_endian.h */
// "License": Public Domain
// I, Mathias Panzenböck, place this file hereby into the public domain. Use it at your own risk for whatever you like.
// In case there are jurisdictions that don't support putting things in the public domain you can also consider it to
// be "dual licensed" under the BSD, MIT and Apache licenses, if you want to. This code is trivial anyway. Consider it
// an example on how to get the endian conversion functions on different platforms.

#ifndef PORTABLE_ENDIAN_H__
#define PORTABLE_ENDIAN_H__

#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__)

#   define __WINDOWS__

#endif

#if defined(__linux__) || defined(__CYGWIN__)
/* Define necessary macros for the header to expose all fields. */
#   if !defined(_BSD_SOURCE)
#       define _BSD_SOURCE
#   endif
#   if !defined(__USE_BSD)
#       define __USE_BSD
#   endif
#   if !defined(_DEFAULT_SOURCE)
#       define _DEFAULT_SOURCE
#   endif
#   include <endian.h>
#   include <features.h>
/* See http://linux.die.net/man/3/endian */
#   if defined(htobe16) && defined(htole16) && defined(be16toh) && defined(le16toh) && defined(htobe32) && defined(htole32) && defined(be32toh) && defined(htole32) && defined(htobe64) && defined(htole64) && defined(be64) && defined(le64)
/* Do nothing. The macros we need already exist. */
#   elif !defined(__GLIBC__) || !defined(__GLIBC_MINOR__) || ((__GLIBC__ < 2) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ < 9)))
#       include <arpa/inet.h>
#       if defined(__BYTE_ORDER) && (__BYTE_ORDER == __LITTLE_ENDIAN)
#           if !defined(htobe16)
#           define htobe16(x) htons(x)
#           endif
#           if !defined(htole16)
#           define htole16(x) (x)
#           endif
#           if !defined(be16toh)
#           define be16toh(x) ntohs(x)
#           endif
#           if !defined(htole16)
#           define le16toh(x) (x)
#           endif

#           if !defined(htobe32)
#           define htobe32(x) htonl(x)
#           endif
#           if !defined(htole32)
#           define htole32(x) (x)
#           endif
#           if !defined(be32toh)
#           define be32toh(x) ntohl(x)
#           endif
#           if !defined(le32toh)
#           define le32toh(x) (x)
#           endif

#           if !defined(htobe64)
#           define htobe64(x) (((uint64_t)htonl(((uint32_t)(((uint64_t)(x)) >> 32)))) | (((uint64_t)htonl(((uint32_t)(x)))) << 32))
#           endif
#           if !defined(htole64)
#           define htole64(x) (x)
#           endif
#           if !defined(be64toh)
#           define be64toh(x) (((uint64_t)ntohl(((uint32_t)(((uint64_t)(x)) >> 32)))) | (((uint64_t)ntohl(((uint32_t)(x)))) << 32))
#           endif
#           if !defined(le64toh)
#           define le64toh(x) (x)
#           endif
#       elif defined(__BYTE_ORDER) && (__BYTE_ORDER == __BIG_ENDIAN)
#           define htobe16(x) (x)
#           define htole16(x) (((((uint16_t)(x)) >> 8))|((((uint16_t)(x)) << 8)))
#           define be16toh(x) (x)
#           define le16toh(x) (((((uint16_t)(x)) >> 8))|((((uint16_t)(x)) << 8)))

#           define htobe32(x) (x)
#           define htole32(x) (((uint32_t)htole16(((uint16_t)(((uint32_t)(x)) >> 16)))) | (((uint32_t)htole16(((uint16_t)(x)))) << 16))
#           define be32toh(x) (x)
#           define le32toh(x) (((uint32_t)le16toh(((uint16_t)(((uint32_t)(x)) >> 16)))) | (((uint32_t)le16toh(((uint16_t)(x)))) << 16))

#           define htobe64(x) (x)
#           define htole64(x) (((uint64_t)htole32(((uint32_t)(((uint64_t)(x)) >> 32)))) | (((uint64_t)htole32(((uint32_t)(x)))) << 32))
#           define be64toh(x) (x)
#           define le64toh(x) (((uint64_t)le32toh(((uint32_t)(((uint64_t)(x)) >> 32)))) | (((uint64_t)le32toh(((uint32_t)(x)))) << 32))
#       else
#           error Byte Order not supported or not defined.
#       endif
#   endif

#elif defined(__APPLE__)

#   include <libkern/OSByteOrder.h>

#   define htobe16(x) OSSwapHostToBigInt16(x)
#   define htole16(x) OSSwapHostToLittleInt16(x)
#   define be16toh(x) OSSwapBigToHostInt16(x)
#   define le16toh(x) OSSwapLittleToHostInt16(x)

#   define htobe32(x) OSSwapHostToBigInt32(x)
#   define htole32(x) OSSwapHostToLittleInt32(x)
#   define be32toh(x) OSSwapBigToHostInt32(x)
#   define le32toh(x) OSSwapLittleToHostInt32(x)

#   define htobe64(x) OSSwapHostToBigInt64(x)
#   define htole64(x) OSSwapHostToLittleInt64(x)
#   define be64toh(x) OSSwapBigToHostInt64(x)
#   define le64toh(x) OSSwapLittleToHostInt64(x)

#   define __BYTE_ORDER    BYTE_ORDER
#   define __BIG_ENDIAN    BIG_ENDIAN
#   define __LITTLE_ENDIAN LITTLE_ENDIAN
#   define __PDP_ENDIAN    PDP_ENDIAN

#elif defined(__OpenBSD__)

#   include <sys/endian.h>

#elif defined(__HAIKU__)

#   include <endian.h>

#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)

#   include <sys/endian.h>

#   if !defined(be16toh)
    #   define be16toh(x) betoh16(x)
    #   define le16toh(x) letoh16(x)
#   endif

#   if !defined(be32toh)
    #   define be32toh(x) betoh32(x)
    #   define le32toh(x) letoh32(x)
#   endif

#   if !defined(be64toh)
    #   define be64toh(x) betoh64(x)
    #   define le64toh(x) letoh64(x)
#   endif

#elif defined(__WINDOWS__)

#   if BYTE_ORDER == LITTLE_ENDIAN

#       define htobe16(x) _byteswap_ushort(x)
#       define htole16(x) (x)
#       define be16toh(x) _byteswap_ushort(x)
#       define le16toh(x) (x)

#       define htobe32(x) _byteswap_ulong(x)
#       define htole32(x) (x)
#       define be32toh(x) _byteswap_ulong(x)
#       define le32toh(x) (x)

#       define htobe64(x) _byteswap_uint64(x)
#       define be64toh(x) _byteswap_uint64(x)
#       define htole64(x) (x)
#       define le64toh(x) (x)

#   elif BYTE_ORDER == BIG_ENDIAN

        /* that would be xbox 360 */
#       define htobe16(x) (x)
#       define htole16(x) __builtin_bswap16(x)
#       define be16toh(x) (x)
#       define le16toh(x) __builtin_bswap16(x)

#       define htobe32(x) (x)
#       define htole32(x) __builtin_bswap32(x)
#       define be32toh(x) (x)
#       define le32toh(x) __builtin_bswap32(x)

#       define htobe64(x) (x)
#       define htole64(x) __builtin_bswap64(x)
#       define be64toh(x) (x)
#       define le64toh(x) __builtin_bswap64(x)

#   else

#       error byte order not supported

#   endif

#   define __BYTE_ORDER    BYTE_ORDER
#   define __BIG_ENDIAN    BIG_ENDIAN
#   define __LITTLE_ENDIAN LITTLE_ENDIAN
#   define __PDP_ENDIAN    PDP_ENDIAN

#elif defined(__sun)

#   include <sys/byteorder.h>

#   define htobe16(x) BE_16(x)
#   define htole16(x) LE_16(x)
#   define be16toh(x) BE_16(x)
#   define le16toh(x) LE_16(x)

#   define htobe32(x) BE_32(x)
#   define htole32(x) LE_32(x)
#   define be32toh(x) BE_32(x)
#   define le32toh(x) LE_32(x)

#   define htobe64(x) BE_64(x)
#   define htole64(x) LE_64(x)
#   define be64toh(x) BE_64(x)
#   define le64toh(x) LE_64(x)

#elif defined _AIX      /* AIX is always big endian */
#       define be64toh(x) (x)
#       define be32toh(x) (x)
#       define be16toh(x) (x)
#       define le32toh(x)                              \
         ((((x) & 0xff) << 24) |                 \
           (((x) & 0xff00) << 8) |                \
           (((x) & 0xff0000) >> 8) |              \
           (((x) & 0xff000000) >> 24))
#       define   le64toh(x)                               \
         ((((x) & 0x00000000000000ffL) << 56) |   \
          (((x) & 0x000000000000ff00L) << 40) |   \
          (((x) & 0x0000000000ff0000L) << 24) |   \
          (((x) & 0x00000000ff000000L) << 8)  |   \
          (((x) & 0x000000ff00000000L) >> 8)  |   \
          (((x) & 0x0000ff0000000000L) >> 24) |   \
          (((x) & 0x00ff000000000000L) >> 40) |   \
          (((x) & 0xff00000000000000L) >> 56))
#       ifndef htobe64
#               define htobe64(x) be64toh(x)
#       endif
#       ifndef htobe32
#               define htobe32(x) be32toh(x)
#       endif
#       ifndef htobe16
#               define htobe16(x) be16toh(x)
#       endif


#else

#   error platform not supported

#endif

#endif

    /* 84: hashlib.h */
#ifndef dictu_hashlib_h
#define dictu_hashlib_h


ObjModule *createHashlibModule(DictuVM *vm);

#endif //dictu_hashlib_h


#ifndef DISABLE_HTTP

    /* 85: http.h */
#ifndef dictu_http_h
#define dictu_http_h

#ifndef DISABLE_HTTP
#endif


typedef struct response {
    DictuVM *vm;
    ObjList *headers;
    char *res;
    size_t len;
    long statusCode;
} Response;

ObjModule *createHTTPModule(DictuVM *vm);

#endif //dictu_http_h
#endif /* DISABLE_HTTP */

    /* 86: json/jsonParseLib.h */
/* vim: set et ts=3 sw=3 sts=3 ft=c:
 *
 * Copyright (C) 2012, 2013, 2014 James McLaughlin et al.  All rights reserved.
 * https://github.com/udp/json-parser
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _JSON_H
#define _JSON_H

#ifndef json_char
#define json_char char
#endif

#ifndef json_int_t
#ifndef _MSC_VER
#define json_int_t int64_t
#else
#define json_int_t __int64
#endif
#endif


#ifdef __cplusplus


   extern "C"
   {

#endif

typedef struct
{
    unsigned long max_memory;
    int settings;

    /* Custom allocator support (leave null to use malloc/free)
     */

    void * (* mem_alloc) (size_t, int zero, void * user_data);
    void (* mem_free) (void *, void * user_data);

    void * user_data;  /* will be passed to mem_alloc and mem_free */

    size_t value_extra;  /* how much extra space to allocate for values? */

} json_settings;

#define json_enable_comments  0x01

typedef enum
{
    json_none,
    json_object,
    json_array,
    json_integer,
    json_double,
    json_string,
    json_boolean,
    json_null

} json_type;

extern const struct _json_value json_value_none;

typedef struct _json_object_entry
{
    json_char * name;
    unsigned int name_length;

    struct _json_value * value;

} json_object_entry;

typedef struct _json_value
{
    struct _json_value * parent;

    json_type type;

    union
    {
        int boolean;
        json_int_t integer;
        double dbl;

        struct
        {
            unsigned int length;
            json_char * ptr; /* null terminated */

        } string;

        struct
        {
            unsigned int length;

            json_object_entry * values;

#if defined(__cplusplus) && __cplusplus >= 201103L
            decltype(values) begin () const
         {  return values;
         }
         decltype(values) end () const
         {  return values + length;
         }
#endif

        } object;

        struct
        {
            unsigned int length;
            struct _json_value ** values;

#if defined(__cplusplus) && __cplusplus >= 201103L
            decltype(values) begin () const
         {  return values;
         }
         decltype(values) end () const
         {  return values + length;
         }
#endif

        } array;

    } u;

    union
    {
        struct _json_value * next_alloc;
        void * object_mem;

    } _reserved;

#ifdef JSON_TRACK_SOURCE

    /* Location of the value in the source JSON
       */
      unsigned int line, col;

#endif


    /* Some C++ operator sugar */

#ifdef __cplusplus

    public:

         inline _json_value ()
         {  memset (this, 0, sizeof (_json_value));
         }

         inline const struct _json_value &operator [] (int index) const
         {
            if (type != json_array || index < 0
                     || ((unsigned int) index) >= u.array.length)
            {
               return json_value_none;
            }

            return *u.array.values [index];
         }

         inline const struct _json_value &operator [] (const char * index) const
         {
            if (type != json_object)
               return json_value_none;

            for (unsigned int i = 0; i < u.object.length; ++ i)
               if (!strcmp (u.object.values [i].name, index))
                  return *u.object.values [i].value;

            return json_value_none;
         }

         inline operator const char * () const
         {
            switch (type)
            {
               case json_string:
                  return u.string.ptr;

               default:
                  return "";
            };
         }

         inline operator json_int_t () const
         {
            switch (type)
            {
               case json_integer:
                  return u.integer;

               case json_double:
                  return (json_int_t) u.dbl;

               default:
                  return 0;
            };
         }

         inline operator bool () const
         {
            if (type != json_boolean)
               return false;

            return u.boolean != 0;
         }

         inline operator double () const
         {
            switch (type)
            {
               case json_integer:
                  return (double) u.integer;

               case json_double:
                  return u.dbl;

               default:
                  return 0;
            };
         }

#endif

} json_value;

json_value * json_parse (const json_char * json,
                         size_t length);

#define json_error_max 128
json_value * json_parse_ex (json_settings * settings,
                            const json_char * json,
                            size_t length,
                            char * error);

void json_value_free (json_value *);


/* Not usually necessary, unless you used a custom mem_alloc and now want to
 * use a custom mem_free.
 */
void json_value_free_ex (json_settings * settings,
                         json_value *);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif


    /* 87: json/jsonBuilderLib.h */

/* vim: set et ts=3 sw=3 sts=3 ft=c:
 *
 * Copyright (C) 2014 James McLaughlin.  All rights reserved.
 * https://github.com/udp/json-builder
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _JSON_BUILDER_H
#define _JSON_BUILDER_H

/* Requires json.h from json-parser
 * https://github.com/udp/json-parser
 */

#ifdef __cplusplus
extern "C"
{
#endif

/* IMPORTANT NOTE:  If you want to use json-builder functions with values
 * allocated by json-parser as part of the parsing process, you must pass
 * json_builder_extra as the value_extra setting in json_settings when
 * parsing.  Otherwise there will not be room for the extra state and
 * json-builder WILL invoke undefined behaviour.
 *
 * Also note that unlike json-parser, json-builder does not currently support
 * custom allocators (for no particular reason other than that it doesn't have
 * any settings or global state.)
 */
extern const size_t json_builder_extra;


/*** Arrays
 ***
 * Note that all of these length arguments are just a hint to allow for
 * pre-allocation - passing 0 is fine.
 */
json_value * json_array_new (size_t length);
json_value * json_array_push (json_value * array, json_value *);


/*** Objects
 ***/
json_value * json_object_new (size_t length);

json_value * json_object_push (json_value * object,
                               const json_char * name,
                               json_value *);

/* Same as json_object_push, but doesn't call strlen() for you.
 */
json_value * json_object_push_length (json_value * object,
                                      unsigned int name_length, const json_char * name,
                                      json_value *);

/* Same as json_object_push_length, but doesn't copy the name buffer before
 * storing it in the value.  Use this micro-optimisation at your own risk.
 */
json_value * json_object_push_nocopy (json_value * object,
                                      unsigned int name_length, json_char * name,
                                      json_value *);

/* Merges all entries from objectB into objectA and destroys objectB.
 */
json_value * json_object_merge (json_value * objectA, json_value * objectB);

/* Sort the entries of an object based on the order in a prototype object.
 * Helpful when reading JSON and writing it again to preserve user order.
 */
void json_object_sort (json_value * object, json_value * proto);



/*** Strings
 ***/
json_value * json_string_new (const json_char *);
json_value * json_string_new_length (unsigned int length, const json_char *);
json_value * json_string_new_nocopy (unsigned int length, json_char *);


/*** Everything else
 ***/
json_value * json_integer_new (json_int_t);
json_value * json_double_new (double);
json_value * json_boolean_new (int);
json_value * json_null_new (void);


/*** Serializing
 ***/
#define json_serialize_mode_multiline     0
#define json_serialize_mode_single_line   1
#define json_serialize_mode_packed        2

#define json_serialize_opt_CRLF                    (1 << 1)
#define json_serialize_opt_pack_brackets           (1 << 2)
#define json_serialize_opt_no_space_after_comma    (1 << 3)
#define json_serialize_opt_no_space_after_colon    (1 << 4)
#define json_serialize_opt_use_tabs                (1 << 5)

typedef struct json_serialize_opts
{
    int mode;
    int opts;
    int indent_size;

} json_serialize_opts;


/* Returns a length in characters that is at least large enough to hold the
 * value in its serialized form, including a null terminator.
 */
size_t json_measure (json_value *);
size_t json_measure_ex (json_value *, json_serialize_opts);


/* Serializes a JSON value into the buffer given (which must already be
 * allocated with a length of at least json_measure(value, opts))
 */
void json_serialize (json_char * buf, json_value *);
void json_serialize_ex (json_char * buf, json_value *, json_serialize_opts);


/*** Cleaning up
 ***/
void json_builder_free (json_value *);

#ifdef __cplusplus
}
#endif

#endif
    /* 88: json.h */
#ifndef dictu_json_h
#define dictu_json_h


ObjModule *createJSONModule(DictuVM *vm);

#endif //dictu_json_h

    /* 89: math.h */
#ifndef dictu_math_h
#define dictu_math_h



ObjModule *createMathsModule(DictuVM *vm);

#endif //dictu_math_h

    /* 90: path.h */
#ifndef dictu_path_h
#define dictu_path_h



#ifdef _WIN32
#else
#endif

ObjModule *createPathModule(DictuVM *vm);

#endif //dictu_path_h

    /* 91: process.h */
#ifndef dictu_process_h
#define dictu_process_h

#ifndef _WIN32
#endif // !_WIN32


ObjModule *createProcessModule(DictuVM *vm);

#endif //dictu_process_h

    /* 92: random.h */
#ifndef dictu_random_h
#define dictu_random_h



ObjModule *createRandomModule(DictuVM *vm);

#endif //dictu_random_h

    /* 93: socket.h */
#ifndef dictu_socket_h
#define dictu_socket_h


#ifdef __FreeBSD__
#endif

ObjModule *createSocketModule(DictuVM *vm);

#endif //dictu_socket_h

#ifndef DISABLE_SQLITE

    /* 94: sqlite.h */
#ifndef dictu_sqlite_h
#define dictu_sqlite_h

#ifdef INCLUDE_SQLITE_LIB
#else
#endif



ObjModule *createSqliteModule(DictuVM *vm);

#endif //dictu_sqlite_h

#endif /* DISABLE_SQLITE */

    /* 95: system.h */
#ifndef dictu_system_h
#define dictu_system_h


#ifdef _WIN32
#define REMOVE remove
#define MKDIR(d, m) ((void)m, _mkdir(d))
#else
#define HAS_ACCESS
#define REMOVE unlink
#define MKDIR(d, m) mkdir(d, m)
#endif


void createSystemModule(DictuVM *vm, int argc, char *argv[]);

#endif //dictu_system_h
