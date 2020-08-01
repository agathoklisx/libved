
    /* 33: common.h */
#ifndef dictu_common_h
#define dictu_common_h


#define NAN_TAGGING
#define DEBUG_PRINT_CODE
#define DEBUG_TRACE_EXECUTION
#define DEBUG_TRACE_GC
#define DEBUG_TRACE_MEM

#define COMPUTED_GOTO

#undef DEBUG_PRINT_CODE
#undef DEBUG_TRACE_EXECUTION
#undef DEBUG_TRACE_GC
#undef DEBUG_TRACE_MEM

// #define DEBUG_STRESS_GC
// #define DEBUG_FINAL_MEM

#define UINT8_COUNT (UINT8_MAX + 1)

typedef struct _vm VM;

#endif

    /* 34: value.h */
#ifndef dictu_value_h
#define dictu_value_h


typedef struct sObj Obj;
typedef struct sObjString ObjString;
typedef struct sObjList ObjList;
typedef struct sObjDict ObjDict;
typedef struct sObjSet  ObjSet;
typedef struct sObjFile ObjFile;

#ifdef NAN_TAGGING

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

#define IS_BOOL(v)    (((v) & FALSE_VAL) == FALSE_VAL)
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

#else

typedef enum {
  VAL_BOOL,
  VAL_NIL, // [user-types]
  VAL_NUMBER,
  VAL_OBJ
} ValueType;

typedef struct {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as; // [as]
} Value;

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)

#define AS_OBJ(value)     ((value).as.obj)
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)

#define BOOL_VAL(value)   ((Value){ VAL_BOOL, { .boolean = value } })
#define NIL_VAL           ((Value){ VAL_NIL, { .number = 0 } })
#define NUMBER_VAL(value) ((Value){ VAL_NUMBER, { .number = value } })
#define OBJ_VAL(object)   ((Value){ VAL_OBJ, { .obj = (Obj*)object } })

#endif

typedef struct {
    int capacity;
    int count;
    Value *values;
} ValueArray;

bool valuesEqual(Value a, Value b);

void initValueArray(ValueArray *array);

void writeValueArray(VM *vm, ValueArray *array, Value value);

void freeValueArray(VM *vm, ValueArray *array);

void grayDict(VM *vm, ObjDict *dict);

bool dictSet(VM *vm, ObjDict *dict, Value key, Value value);

bool dictGet(ObjDict *dict, Value key, Value *value);

bool dictDelete(VM *vm, ObjDict *dict, Value key);

bool setGet(ObjSet *set, Value value);

bool setInsert(VM *vm, ObjSet *set, Value value);

bool setDelete(VM *vm, ObjSet *set, Value value);

void graySet(VM *vm, ObjSet *set);

char *valueToString(Value value);

void printValue(Value value);

#endif

    /* 35: chunk.h */
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

void initChunk(VM *vm, Chunk *chunk);

void freeChunk(VM *vm, Chunk *chunk);

void writeChunk(VM *vm, Chunk *chunk, uint8_t byte, int line);

int addConstant(VM *vm, Chunk *chunk, Value value);

#endif

    /* 36: table.h */
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

void freeTable(VM *vm, Table *table);

bool tableGet(Table *table, ObjString *key, Value *value);

bool tableSet(VM *vm, Table *table, ObjString *key, Value value);

bool tableDelete(VM *vm, Table *table, ObjString *key);

void tableAddAll(VM *vm, Table *from, Table *to);

ObjString *tableFindString(Table *table, const char *chars, int length,
                           uint32_t hash);

void tableRemoveWhite(VM *vm, Table *table);

void grayTable(VM *vm, Table *table);

#endif

    /* 37: object.h */
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
} ObjFunction;

typedef Value (*NativeFn)(VM *vm, int argCount, Value *args);

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

ObjModule *newModule(VM *vm, ObjString *name);

ObjBoundMethod *newBoundMethod(VM *vm, Value receiver, ObjClosure *method);

ObjClass *newClass(VM *vm, ObjString *name, ObjClass *superclass, ClassType type);

ObjClosure *newClosure(VM *vm, ObjFunction *function);

ObjFunction *newFunction(VM *vm, ObjModule *module, FunctionType type);

ObjInstance *newInstance(VM *vm, ObjClass *klass);

ObjNative *newNative(VM *vm, NativeFn function);

ObjString *takeString(VM *vm, char *chars, int length);

ObjString *copyString(VM *vm, const char *chars, int length);

ObjList *initList(VM *vm);

ObjDict *initDict(VM *vm);

ObjSet *initSet(VM *vm);

ObjFile *initFile(VM *vm);

ObjUpvalue *newUpvalue(VM *vm, Value *slot);

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

    /* 38: scanner.h */
#ifndef dictu_scanner_h
#define dictu_scanner_h

typedef enum {
    // Single-character tokens.
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
    TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS,

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
    TOKEN_WITH, TOKEN_EOF, TOKEN_IMPORT,
    TOKEN_ERROR

} TokenType;

typedef struct {
    TokenType type;
    const char *start;
    int length;
    int line;
} Token;

void initScanner(const char *source);

void backTrack();

Token scanToken();

#endif

    /* 39: compiler.h */
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
    VM *vm;
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

typedef void (*ParseFn)(Compiler *compiler, bool canAssign);

typedef struct {
    ParseFn prefix;
    ParseFn infix;
    Precedence precedence;
} ParseRule;

ObjFunction *compile(VM *vm, ObjModule *module, const char *source);

void grayCompilerRoots(VM *vm);

#endif

    /* 40: vm.h */
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
    const char **scriptNames;
    int scriptNameCount;
    int scriptNameCapacity;
    CallFrame *frames;
    int frameCount;
    int frameCapacity;
    ObjModule *lastModule;
    Table modules;
    Table globals;
    Table constants;
    Table strings;
    Table imports;
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

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

// extern VM vm;

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

#endif

    /* 41: natives.h */
#ifndef dictu_natives_h
#define dictu_natives_h


void defineAllNatives(VM *vm);

#endif //dictu_natives_h

    /* 42: memory.h */
#ifndef dictu_memory_h
#define dictu_memory_h


#define ALLOCATE(vm, type, count) \
    (type*)reallocate(vm, NULL, 0, sizeof(type) * (count))

#define FREE(vm, type, pointer) \
    reallocate(vm, pointer, sizeof(type), 0)

#define GROW_CAPACITY(capacity) \
    ((capacity) < 8 ? 8 : (capacity) * 2)

#define SHRINK_CAPACITY(capacity) \
    ((capacity) < 16 ? 7 : (capacity) / 2)

#define GROW_ARRAY(vm, previous, type, oldCount, count) \
    (type*)reallocate(vm, previous, sizeof(type) * (oldCount), \
        sizeof(type) * (count))

#define FREE_ARRAY(vm, type, pointer, oldCount) \
    reallocate(vm, pointer, sizeof(type) * (oldCount), 0)

void *reallocate(VM *vm, void *previous, size_t oldSize, size_t newSize);

void grayObject(VM *vm, Obj *object);

void grayValue(VM *vm, Value value);

void collectGarbage(VM *vm);

void freeObjects(VM *vm);

void freeObject(VM *vm, Obj *object);

#endif

    /* 43: util.h */
#ifndef dictu_util_h
#define dictu_util_h


char *readFile(const char *path);

void defineNative(VM *vm, Table *table, const char *name, NativeFn function);

void defineNativeProperty(VM *vm, Table *table, const char *name, Value value);

bool isValidKey(Value value);

Value boolNative(VM *vm, int argCount, Value *args);

#endif //dictu_util_h

    /* 44: bool.h */
#ifndef dictu_bool_h
#define dictu_bool_h


void declareBoolMethods(VM *vm);

#endif //dictu_bool_h

    /* 45: class.h */
#ifndef dictu_class_h
#define dictu_class_h



void declareClassMethods(VM *vm);

#endif //dictu_class_h

    /* 46: copy.h */
#ifndef dictu_copy_h
#define dictu_copy_h



ObjList *copyList(VM *vm, ObjList *oldList, bool shallow);

ObjDict *copyDict(VM *vm, ObjDict *oldDict, bool shallow);

ObjInstance *copyInstance(VM *vm, ObjInstance *oldInstance, bool shallow);

#endif //dictu_copy_h

    /* 47: dicts.h */
#ifndef dictu_dicts_h
#define dictu_dicts_h



void declareDictMethods(VM *vm);

#endif //dictu_dicts_h

    /* 48: files.h */
#ifndef dictu_files_h
#define dictu_files_h



void declareFileMethods(VM *vm);

#endif //dictu_files_h

    /* 49: instance.h */
#ifndef dictu_instance_h
#define dictu_instance_h



void declareInstanceMethods(VM *vm);

#endif //dictu_instance_h

    /* 50: lists.h */
#ifndef dictu_lists_h
#define dictu_lists_h



void declareListMethods(VM *vm);

#endif //dictu_lists_h

    /* 51: nil.h */
#ifndef dictu_nil_h
#define dictu_nil_h


void declareNilMethods(VM *vm);

#endif //dictu_nil_h

    /* 52: number.h */
#ifndef dictu_number_h
#define dictu_number_h


void declareNumberMethods(VM *vm);

#endif //dictu_number_h

    /* 53: sets.h */
#ifndef dictu_sets_h
#define dictu_sets_h



void declareSetMethods(VM *vm);

#endif //dictu_sets_h

    /* 54: strings.h */
#ifndef dictu_strings_h
#define dictu_strings_h



void declareStringMethods(VM *vm);

#endif //dictu_strings_h

    /* 55: optionals.h */
#ifndef dictu_optionals_h
#define dictu_optionals_h


#define GET_SELF_CLASS \
  AS_MODULE(args[-1])

#define SET_ERRNO(module_)                                              \
  defineNativeProperty(vm, &module_->values, "errno", NUMBER_VAL(errno))

#define GET_ERRNO(module_)({                         \
  Value errno_value = 0;                             \
  ObjString *name = copyString(vm, "errno", 5);      \
  tableGet(&module_->values, name, &errno_value);    \
  errno_value;                                       \
})

typedef ObjModule *(*BuiltinModule)(VM *vm);

typedef struct {
    char *name;
    BuiltinModule module;
} BuiltinModules;

ObjModule *importBuiltinModule(VM *vm, int index);

int findBuiltinModule(char *name, int length);

#endif //dictu_optionals_h

    /* 56: c.h */
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


#ifdef __APPLE__
#define LAST_ERROR 106
#else
#define LAST_ERROR EHWPOISON
#endif


void createCClass(VM *vm);

#define MAX_ERROR_LEN 256
Value strerrorGeneric(VM *, int);
Value strerrorNative(VM *, int, Value *);

#endif //dictu_c_h

    /* 57: env.h */
#ifndef dictu_env_h
#define dictu_env_h



ObjModule *createEnvClass(VM *vm);

#endif //dictu_env_h
#ifndef DISABLE_HTTP


    /* 58: http.h */
#ifndef dictu_http_h
#define dictu_http_h

#ifndef DISABLE_HTTP
#endif


typedef struct response {
    VM *vm;
    char *res;
    ObjList *headers;
    size_t len;
    size_t headerLen;
    long statusCode;
} Response;

ObjModule *createHTTPClass(VM *vm);

#endif //dictu_http_h
#endif /* DISABLE_HTTP */

    /* 59: jsonParseLib.h */
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


    /* 60: jsonBuilderLib.h */

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
    /* 61: json.h */
#ifndef dictu_json_h
#define dictu_json_h


ObjModule *createJSONClass(VM *vm);

#endif //dictu_json_h

    /* 62: math.h */
#ifndef dictu_math_h
#define dictu_math_h



ObjModule *createMathsClass(VM *vm);

#endif //dictu_math_h

    /* 63: path.h */
#ifndef dictu_path_h
#define dictu_path_h


#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
#define DIR_SEPARATOR '\\'
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

#define IS_DIR_SEPARATOR(c) (c == DIR_SEPARATOR)


ObjModule *createPathClass(VM *vm);

#endif //dictu_path_h

    /* 64: system.h */
#ifndef dictu_system_h
#define dictu_system_h


#ifdef _WIN32
#define REMOVE remove
#define MKDIR(d, m) mkdir(d)
#else
#define REMOVE unlink
#define MKDIR(d, m) mkdir(d, m)
#endif


void createSystemClass(VM *vm, int argc, const char *argv[]);

#endif //dictu_system_h

    /* 65: datetime.h */
#ifndef dictu_datetime_h
#define dictu_datetime_h




ObjModule *createDatetimeClass(VM *vm);

#endif //dictu_datetime_h
