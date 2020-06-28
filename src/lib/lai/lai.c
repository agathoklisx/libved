#define _XOPEN_SOURCE 700

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <curl/curl.h>
#include <errno.h>
#include <assert.h>

#include "__lai.h"

    /* 1: value.c */


#define TABLE_MAX_LOAD 0.75
#define TABLE_MIN_LOAD 0.35

void initValueArray(ValueArray *array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(VM *vm, ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(vm, array->values, Value,
                                   oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(VM *vm, ValueArray *array) {
    FREE_ARRAY(vm, Value, array->values, array->capacity);
    initValueArray(array);
}

static inline uint32_t hashBits(uint64_t hash) {
    // From v8's ComputeLongHash() which in turn cites:
    // Thomas Wang, Integer Hash Functions.
    // http://www.concentric.net/~Ttwang/tech/inthash.htm
    hash = ~hash + (hash << 18);  // hash = (hash << 18) - hash - 1;
    hash = hash ^ (hash >> 31);
    hash = hash * 21;  // hash = (hash + (hash << 2)) + (hash << 4);
    hash = hash ^ (hash >> 11);
    hash = hash + (hash << 6);
    hash = hash ^ (hash >> 22);
    return (uint32_t) (hash & 0x3fffffff);
}

static uint32_t hashObject(Obj *object) {
    switch (object->type) {
        case OBJ_STRING: {
            return ((ObjString *) object)->hash;
        }

            // Should never get here
        default: {
#ifdef DEBUG_PRINT_CODE
            printf("Object: ");
            printValue(OBJ_VAL(object));
            printf(" not hashable!\n");
            exit(1);
#endif
            return -1;
        }
    }
}

static uint32_t hashValue(Value value) {
    if (IS_OBJ(value)) {
        return hashObject(AS_OBJ(value));
    }

    return hashBits(value);
}

bool dictGet(ObjDict *dict, Value key, Value *value) {
    if (dict->count == 0) return false;

    DictItem *entry;
    uint32_t index = hashValue(key) & dict->capacityMask;
    uint32_t psl = 0;

    for (;;) {
        entry = &dict->entries[index];

        if (IS_EMPTY(entry->key) || psl > entry->psl) {
            return false;
        }

        if (valuesEqual(key, entry->key)) {
            break;
        }

        index = (index + 1) & dict->capacityMask;
        psl++;
    }

    *value = entry->value;
    return true;
}

static void adjustDictCapacity(VM *vm, ObjDict *dict, int capacityMask) {
    DictItem *entries = ALLOCATE(vm, DictItem, capacityMask + 1);
    for (int i = 0; i <= capacityMask; i++) {
        entries[i].key = EMPTY_VAL;
        entries[i].value = NIL_VAL;
        entries[i].psl = 0;
    }

    DictItem *oldEntries = dict->entries;
    int oldMask = dict->capacityMask;

    dict->count = 0;
    dict->entries = entries;
    dict->capacityMask = capacityMask;

    for (int i = 0; i <= oldMask; i++) {
        DictItem *entry = &oldEntries[i];
        if (IS_EMPTY(entry->key)) continue;

        dictSet(vm, dict, entry->key, entry->value);
    }

    FREE_ARRAY(vm, DictItem, oldEntries, oldMask + 1);
}

bool dictSet(VM *vm, ObjDict *dict, Value key, Value value) {
    if (dict->count + 1 > (dict->capacityMask + 1) * TABLE_MAX_LOAD) {
        // Figure out the new table size.
        int capacityMask = GROW_CAPACITY(dict->capacityMask + 1) - 1;
        adjustDictCapacity(vm, dict, capacityMask);
    }

    uint32_t index = hashValue(key) & dict->capacityMask;
    DictItem *bucket;
    bool isNewKey = false;

    DictItem entry;
    entry.key = key;
    entry.value = value;
    entry.psl = 0;

    for (;;) {
        bucket = &dict->entries[index];

        if (IS_EMPTY(bucket->key)) {
            isNewKey = true;
            break;
        } else {
            if (valuesEqual(key, bucket->key)) {
                break;
            }

            if (entry.psl > bucket->psl) {
                isNewKey = true;
                DictItem tmp = entry;
                entry = *bucket;
                *bucket = tmp;
            }
        }

        index = (index + 1) & dict->capacityMask;
        entry.psl++;
    }

    *bucket = entry;
    if (isNewKey) dict->count++;
    return isNewKey;
}

bool dictDelete(VM *vm, ObjDict *dict, Value key) {
    if (dict->count == 0) return false;

    int capacityMask = dict->capacityMask;
    uint32_t index = hashValue(key) & capacityMask;
    uint32_t psl = 0;
    DictItem *entry;

    for (;;) {
        entry = &dict->entries[index];

        if (IS_EMPTY(entry->key) || psl > entry->psl) {
            return false;
        }

        if (valuesEqual(key, entry->key)) {
            break;
        }

        index = (index + 1) & capacityMask;
        psl++;
    }

    dict->count--;

    for (;;) {
        DictItem *nextEntry;
        entry->key = EMPTY_VAL;
        entry->value = EMPTY_VAL;

        index = (index + 1) & capacityMask;
        nextEntry = &dict->entries[index];

        /*
         * Stop if we reach an empty bucket or hit a key which
         * is in its base (original) location.
         */
        if (IS_EMPTY(nextEntry->key) || nextEntry->psl == 0) {
            break;
        }

        nextEntry->psl--;
        *entry = *nextEntry;
        entry = nextEntry;
    }

    if (dict->count - 1 < dict->capacityMask * TABLE_MIN_LOAD) {
        // Figure out the new table size.
        capacityMask = SHRINK_CAPACITY(dict->capacityMask);
        adjustDictCapacity(vm, dict, capacityMask);
    }

    return true;
}

void grayDict(VM *vm, ObjDict *dict) {
    for (int i = 0; i <= dict->capacityMask; i++) {
        DictItem *entry = &dict->entries[i];
        grayValue(vm, entry->key);
        grayValue(vm, entry->value);
    }
}


static SetItem *findSetEntry(SetItem *entries, int capacityMask,
                             Value value) {
    uint32_t index = hashValue(value) & capacityMask;
    SetItem *tombstone = NULL;

    for (;;) {
        SetItem *entry = &entries[index];

        if (IS_EMPTY(entry->value)) {
            if (!entry->deleted) {
                // Empty entry.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // We found a tombstone.
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (valuesEqual(value, entry->value)) {
            // We found the key.
            return entry;
        }

        index = (index + 1) & capacityMask;
    }
}

bool setGet(ObjSet *set, Value value) {
    if (set->count == 0) return false;

    SetItem *entry = findSetEntry(set->entries, set->capacityMask, value);
    if (IS_EMPTY(entry->value) || entry->deleted) return false;

    return true;
}

static void adjustSetCapacity(VM *vm, ObjSet *set, int capacityMask) {
    SetItem *entries = ALLOCATE(vm, SetItem, capacityMask + 1);
    for (int i = 0; i <= capacityMask; i++) {
        entries[i].value = EMPTY_VAL;
        entries[i].deleted = false;
    }

    set->count = 0;

    for (int i = 0; i <= set->capacityMask; i++) {
        SetItem *entry = &set->entries[i];
        if (IS_EMPTY(entry->value) || entry->deleted) continue;

        SetItem *dest = findSetEntry(entries, capacityMask, entry->value);
        dest->value = entry->value;
        set->count++;
    }

    FREE_ARRAY(vm, SetItem, set->entries, set->capacityMask + 1);
    set->entries = entries;
    set->capacityMask = capacityMask;
}

bool setInsert(VM *vm, ObjSet *set, Value value) {
    if (set->count + 1 > (set->capacityMask + 1) * TABLE_MAX_LOAD) {
        // Figure out the new table size.
        int capacityMask = GROW_CAPACITY(set->capacityMask + 1) - 1;
        adjustSetCapacity(vm, set, capacityMask);
    }

    SetItem *entry = findSetEntry(set->entries, set->capacityMask, value);
    bool isNewKey = IS_EMPTY(entry->value) || entry->deleted;
    entry->value = value;
    entry->deleted = false;

    if (isNewKey) set->count++;

    return isNewKey;
}

bool setDelete(VM *vm, ObjSet *set, Value value) {
    if (set->count == 0) return false;

    SetItem *entry = findSetEntry(set->entries, set->capacityMask, value);
    if (IS_EMPTY(entry->value)) return false;

    // Place a tombstone in the entry.
    set->count--;
    entry->deleted = true;

    if (set->count - 1 < set->capacityMask * TABLE_MIN_LOAD) {
        // Figure out the new table size.
        int capacityMask = SHRINK_CAPACITY(set->capacityMask);
        adjustSetCapacity(vm, set, capacityMask);
    }

    return true;
}

void graySet(VM *vm, ObjSet *set) {
    for (int i = 0; i <= set->capacityMask; i++) {
        SetItem *entry = &set->entries[i];
        grayValue(vm, entry->value);
    }
}

// Calling function needs to free memory
char *valueToString(Value value) {
    if (IS_BOOL(value)) {
        char *str = AS_BOOL(value) ? "true" : "false";
        char *boolString = malloc(sizeof(char) * (strlen(str) + 1));
        snprintf(boolString, strlen(str) + 1, "%s", str);
        return boolString;
    } else if (IS_NIL(value)) {
        char *nilString = malloc(sizeof(char) * 4);
        snprintf(nilString, 4, "%s", "nil");
        return nilString;
    } else if (IS_NUMBER(value)) {
        double number = AS_NUMBER(value);
        int numberStringLength = snprintf(NULL, 0, "%.15g", number) + 1;
        char *numberString = malloc(sizeof(char) * numberStringLength);
        snprintf(numberString, numberStringLength, "%.15g", number);
        return numberString;
    } else if (IS_OBJ(value)) {
        return objectToString(value);
    }

    char *unknown = malloc(sizeof(char) * 8);
    snprintf(unknown, 8, "%s", "unknown");
    return unknown;
}

void printValue(Value value) {
    char *output = valueToString(value);
    printf("%s", output);
    free(output);
}

static bool listComparison(Value a, Value b) {
    ObjList *list = AS_LIST(a);
    ObjList *listB = AS_LIST(b);

    if (list->values.count != listB->values.count)
        return false;

    for (int i = 0; i < list->values.count; ++i) {
        if (!valuesEqual(list->values.values[i], listB->values.values[i]))
            return false;
    }

    return true;
}

static bool dictComparison(Value a, Value b) {
    ObjDict *dict = AS_DICT(a);
    ObjDict *dictB = AS_DICT(b);

    // Different lengths, not the same
    if (dict->count != dictB->count)
        return false;

    // Lengths are the same, and dict 1 has 0 length
    // therefore both are empty
    if (dict->count == 0)
        return true;

    for (int i = 0; i <= dict->capacityMask; ++i) {
        DictItem *item = &dict->entries[i];

        if (IS_EMPTY(item->key))
            continue;

        Value value;
        // Check if key from dict A is in dict B
        if (!dictGet(dictB, item->key, &value)) {
            // Key doesn't exist
            return false;
        }

        // Key exists
        if (!valuesEqual(item->value, value)) {
            // Values don't equal
            return false;
        }
    }

    return true;
}

static bool setComparison(Value a, Value b) {
    ObjSet *set = AS_SET(a);
    ObjSet *setB = AS_SET(b);

    // Different lengths, not the same
    if (set->count != setB->count)
        return false;

    // Lengths are the same, and dict 1 has 0 length
    // therefore both are empty
    if (set->count == 0)
        return true;

    for (int i = 0; i <= set->capacityMask; ++i) {
        SetItem *item = &set->entries[i];

        if (IS_EMPTY(item->value) || item->deleted)
            continue;

        // Check if key from dict A is in dict B
        if (!setGet(setB, item->value)) {
            // Key doesn't exist
            return false;
        }
    }

    return true;
}

bool valuesEqual(Value a, Value b) {
#ifdef NAN_TAGGING

    if (IS_OBJ(a) && IS_OBJ(b)) {
        if (AS_OBJ(a)->type != AS_OBJ(b)->type) return false;

        switch (AS_OBJ(a)->type) {
            case OBJ_LIST: {
                return listComparison(a, b);
            }

            case OBJ_DICT: {
                return dictComparison(a, b);
            }

            case OBJ_SET: {
                return setComparison(a, b);
            }

                // Pass through
            default:
                break;
        }
    }

    return a == b;
#else
    if (a.type != b.type) return false;

    switch (a.type) {
      case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
      case VAL_NIL:    return true;
      case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
      case VAL_OBJ:
        return AS_OBJ(a) == AS_OBJ(b);
    }
#endif
}
    /* 2: chunk.c */

void initChunk(VM *vm, Chunk *chunk) {
    UNUSED(vm);

    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(VM *vm, Chunk *chunk) {
    FREE_ARRAY(vm, uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(vm, int, chunk->lines, chunk->capacity);
    freeValueArray(vm, &chunk->constants);
    initChunk(vm, chunk);
}

void writeChunk(VM *vm, Chunk *chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(vm, chunk->code, uint8_t,
                                 oldCapacity, chunk->capacity);
        chunk->lines = GROW_ARRAY(vm, chunk->lines, int,
                                  oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

int addConstant(VM *vm, Chunk *chunk, Value value) {
    push(vm, value);
    writeValueArray(vm, &chunk->constants, value);
    pop(vm);
    return chunk->constants.count - 1;
}

    /* 3: table.c */


#define TABLE_MAX_LOAD 0.75

void initTable(Table *table) {
    table->count = 0;
    table->capacityMask = -1;
    table->entries = NULL;
}

void freeTable(VM *vm, Table *table) {
    FREE_ARRAY(vm, Entry, table->entries, table->capacityMask + 1);
    initTable(table);
}

bool tableGet(Table *table, ObjString *key, Value *value) {
    if (table->count == 0) return false;

    Entry *entry;
    uint32_t index = key->hash & table->capacityMask;
    uint32_t psl = 0;

    for (;;) {
        entry = &table->entries[index];

        if (entry->key == NULL || psl > entry->psl) {
            return false;
        }

        if (entry->key == key) {
            break;
        }

        index = (index + 1) & table->capacityMask;
        psl++;
    }

    *value = entry->value;
    return true;
}

static void adjustCapacity(VM *vm, Table *table, int capacityMask) {
    Entry *entries = ALLOCATE(vm, Entry, capacityMask + 1);
    for (int i = 0; i <= capacityMask; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
        entries[i].psl = 0;
    }

    Entry *oldEntries = table->entries;
    int oldMask = table->capacityMask;

    table->count = 0;
    table->entries = entries;
    table->capacityMask = capacityMask;

    for (int i = 0; i <= oldMask; i++) {
        Entry *entry = &oldEntries[i];
        if (entry->key == NULL) continue;

        tableSet(vm, table, entry->key, entry->value);
    }

    FREE_ARRAY(vm, Entry, oldEntries, oldMask + 1);
}

bool tableSet(VM *vm, Table *table, ObjString *key, Value value) {
    if (table->count + 1 > (table->capacityMask + 1) * TABLE_MAX_LOAD) {
        // Figure out the new table size.
        int capacityMask = GROW_CAPACITY(table->capacityMask + 1) - 1;
        adjustCapacity(vm, table, capacityMask);
    }

    uint32_t index = key->hash & table->capacityMask;
    Entry *bucket;
    bool isNewKey = false;

    Entry entry;
    entry.key = key;
    entry.value = value;
    entry.psl = 0;

    for (;;) {
        bucket = &table->entries[index];

        if (bucket->key == NULL) {
            isNewKey = true;
            break;
        } else {
            if (bucket->key == key) {
                break;
            }

            if (entry.psl > bucket->psl) {
                isNewKey = true;
                Entry tmp = entry;
                entry = *bucket;
                *bucket = tmp;
            }
        }

        index = (index + 1) & table->capacityMask;
        entry.psl++;
    }

    *bucket = entry;
    if (isNewKey) table->count++;
    return isNewKey;
}

bool tableDelete(VM *vm, Table *table, ObjString *key) {
    if (table->count == 0) return false;

    int capacityMask = table->capacityMask;
    uint32_t index = key->hash & table->capacityMask;
    uint32_t psl = 0;
    Entry *entry;

    for (;;) {
        entry = &table->entries[index];

        if (entry->key == NULL || psl > entry->psl) {
            return false;
        }

        if (entry->key == key) {
            break;
        }

        index = (index + 1) & capacityMask;
        psl++;
    }

    table->count--;

    for (;;) {
        Entry *nextEntry;
        entry->key = NULL;
        entry->value = NIL_VAL;
        entry->psl = 0;

        index = (index + 1) & capacityMask;
        nextEntry = &table->entries[index];

        /*
         * Stop if we reach an empty bucket or hit a key which
         * is in its base (original) location.
         */
        if (nextEntry->key == NULL || nextEntry->psl == 0) {
            break;
        }

        nextEntry->psl--;
        *entry = *nextEntry;
        entry = nextEntry;
    }

    // TODO: Add constant for table load factor
    if (table->count - 1 < table->capacityMask * 0.35) {
        // Figure out the new table size.
        capacityMask = SHRINK_CAPACITY(table->capacityMask);
        adjustCapacity(vm, table, capacityMask);
    }

    return true;
}

void tableAddAll(VM *vm, Table *from, Table *to) {
    for (int i = 0; i <= from->capacityMask; i++) {
        Entry *entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(vm, to, entry->key, entry->value);
        }
    }
}

// TODO: Return entry here rather than string
ObjString *tableFindString(Table *table, const char *chars, int length,
                           uint32_t hash) {
    // If the table is empty, we definitely won't find it.
    if (table->count == 0) return NULL;

    // Figure out where to insert it in the table. Use open addressing and
    // basic linear probing.

    uint32_t index = hash & table->capacityMask;
    uint32_t psl = 0;

    for (;;) {
        Entry *entry = &table->entries[index];

        if (entry->key == NULL || psl > entry->psl) {
            return NULL;
        }

        if (entry->key->length == length &&
            entry->key->hash == hash &&
            memcmp(entry->key->chars, chars, length) == 0) {
            // We found it.
            return entry->key;
        }

        index = (index + 1) & table->capacityMask;
        psl++;
    }
}

void tableRemoveWhite(VM *vm, Table *table) {
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry *entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.isDark) {
            tableDelete(vm, table, entry->key);
        }
    }
}

void grayTable(VM *vm, Table *table) {
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry *entry = &table->entries[i];
        grayObject(vm, (Obj *) entry->key);
        grayValue(vm, entry->value);
    }
}

    /* 4: object.c */


#define ALLOCATE_OBJ(vm, type, objectType) \
    (type*)allocateObject(vm, sizeof(type), objectType)

static Obj *allocateObject(VM *vm, size_t size, ObjType type) {
    Obj *object;
    object = (Obj *) reallocate(vm, NULL, 0, size);
    object->type = type;
    object->isDark = false;
    object->next = vm->objects;
    vm->objects = object;

#ifdef DEBUG_TRACE_GC
    printf("%p allocate %zd for %d\n", (void *)object, size, type);
#endif

    return object;
}

ObjModule *newModule(VM *vm, ObjString *name) {
    Value moduleVal;
    if (tableGet(&vm->modules, name, &moduleVal)) {
        return AS_MODULE(moduleVal);
    }

    ObjModule *module = ALLOCATE_OBJ(vm, ObjModule, OBJ_MODULE);
    initTable(&module->values);
    module->name = name;

    push(vm, OBJ_VAL(module));
    tableSet(vm, &vm->modules, name, OBJ_VAL(module));
    pop(vm);

    return module;
}

ObjBoundMethod *newBoundMethod(VM *vm, Value receiver, ObjClosure *method) {
    ObjBoundMethod *bound = ALLOCATE_OBJ(vm, ObjBoundMethod,
                                         OBJ_BOUND_METHOD);

    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass *newClass(VM *vm, ObjString *name, ObjClass *superclass) {
    ObjClass *klass = ALLOCATE_OBJ(vm, ObjClass, OBJ_CLASS);
    klass->name = name;
    klass->superclass = superclass;
    initTable(&klass->methods);
    return klass;
}

ObjClassNative *newClassNative(VM *vm, ObjString *name) {
    ObjClassNative *klass = ALLOCATE_OBJ(vm, ObjClassNative, OBJ_NATIVE_CLASS);
    klass->name = name;
    initTable(&klass->methods);
    initTable(&klass->properties);
    return klass;
}

ObjTrait *newTrait(VM *vm, ObjString *name) {
    ObjTrait *trait = ALLOCATE_OBJ(vm, ObjTrait, OBJ_TRAIT);
    trait->name = name;
    initTable(&trait->methods);
    return trait;
}

ObjClosure *newClosure(VM *vm, ObjFunction *function) {
    ObjUpvalue **upvalues = ALLOCATE(vm, ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) {
        upvalues[i] = NULL;
    }

    ObjClosure *closure = ALLOCATE_OBJ(vm, ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjFunction *newFunction(VM *vm, ObjModule *module, bool isStatic) {
    ObjFunction *function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->arityOptional = 0;
    function->upvalueCount = 0;
    function->name = NULL;
    function->staticMethod = isStatic;
    function->module = module;
    initChunk(vm, &function->chunk);
    return function;
}

ObjInstance *newInstance(VM *vm, ObjClass *klass) {
    ObjInstance *instance = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}

ObjNative *newNative(VM *vm, NativeFn function) {
    ObjNative *native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

static ObjString *allocateString(VM *vm, char *chars, int length,
                                 uint32_t hash) {
    ObjString *string = ALLOCATE_OBJ(vm, ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;
    string->hash = hash;
    push(vm, OBJ_VAL(string));
    tableSet(vm, &vm->strings, string, NIL_VAL);
    pop(vm);
    return string;
}

ObjList *initList(VM *vm) {
    ObjList *list = ALLOCATE_OBJ(vm, ObjList, OBJ_LIST);
    initValueArray(&list->values);
    return list;
}

ObjDict *initDict(VM *vm) {
    ObjDict *dict = ALLOCATE_OBJ(vm, ObjDict, OBJ_DICT);
    dict->count = 0;
    dict->capacityMask = -1;
    dict->entries = NULL;
    return dict;
}

ObjSet *initSet(VM *vm) {
    ObjSet *set = ALLOCATE_OBJ(vm, ObjSet, OBJ_SET);
    set->count = 0;
    set->capacityMask = -1;
    set->entries = NULL;
    return set;
}

ObjFile *initFile(VM *vm) {
    ObjFile *file = ALLOCATE_OBJ(vm, ObjFile, OBJ_FILE);
    return file;
}

static uint32_t hashString(const char *key, int length) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < length; i++) {
        hash ^= key[i];
        hash *= 16777619;
    }

    return hash;
}

ObjString *takeString(VM *vm, char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm->strings, chars, length,
                                          hash);
    if (interned != NULL) {
        FREE_ARRAY(vm, char, chars, length + 1);
        return interned;
    }

    return allocateString(vm, chars, length, hash);
}

ObjString *copyString(VM *vm, const char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm->strings, chars, length,
                                          hash);
    if (interned != NULL) return interned;

    char *heapChars = ALLOCATE(vm, char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(vm, heapChars, length, hash);
}

ObjUpvalue *newUpvalue(VM *vm, Value *slot) {
    ObjUpvalue *upvalue = ALLOCATE_OBJ(vm, ObjUpvalue, OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->value = slot;
    upvalue->next = NULL;

    return upvalue;
}

char *listToString(Value value) {
    int size = 50;
    ObjList *list = AS_LIST(value);
    char *listString = malloc(sizeof(char) * size);
    memcpy(listString, "[", 1);
    int listStringLength = 1;

    for (int i = 0; i < list->values.count; ++i) {
        Value listValue = list->values.values[i];

        char *element;
        int elementSize;

        if (IS_STRING(listValue)) {
            ObjString *s = AS_STRING(listValue);
            element = s->chars;
            elementSize = s->length;
        } else {
            element = valueToString(listValue);
            elementSize = strlen(element);
        }

        if (elementSize > (size - listStringLength - 3)) {
            if (elementSize > size * 2) {
                size += elementSize * 2 + 3;
            } else {
                size = size * 2 + 3;
            }

            char *newB = realloc(listString, sizeof(char) * size);

            if (newB == NULL) {
                printf("Unable to allocate memory\n");
                exit(71);
            }

            listString = newB;
        }

        if (IS_STRING(listValue)) {
            memcpy(listString + listStringLength, "\"", 1);
            memcpy(listString + listStringLength + 1, element, elementSize);
            memcpy(listString + listStringLength + 1 + elementSize, "\"", 1);
            listStringLength += elementSize + 2;
        } else {
            memcpy(listString + listStringLength, element, elementSize);
            listStringLength += elementSize;
            free(element);
        }

        if (i != list->values.count - 1) {
            memcpy(listString + listStringLength, ", ", 2);
            listStringLength += 2;
        }
    }

    memcpy(listString + listStringLength, "]", 1);
    listString[listStringLength + 1] = '\0';

    return listString;
}

char *dictToString(Value value) {
   int count = 0;
   int size = 50;
   ObjDict *dict = AS_DICT(value);
   char *dictString = malloc(sizeof(char) * size);
   memcpy(dictString, "{", 1);
   int dictStringLength = 1;

   for (int i = 0; i <= dict->capacityMask; ++i) {
       DictItem *item = &dict->entries[i];
       if (IS_EMPTY(item->key)) {
           continue;
       }

       count++;

       char *key;
       int keySize;

       if (IS_STRING(item->key)) {
           ObjString *s = AS_STRING(item->key);
           key = s->chars;
           keySize = s->length;
       } else {
           key = valueToString(item->key);
           keySize = strlen(key);
       }

       if (keySize > (size - dictStringLength - 1)) {
           if (keySize > size * 2) {
               size += keySize * 2;
           } else {
               size *= 2;
           }

           char *newB = realloc(dictString, sizeof(char) * size);

           if (newB == NULL) {
               printf("Unable to allocate memory\n");
               exit(71);
           }

           dictString = newB;
       }

       if (IS_STRING(item->key)) {
           memcpy(dictString + dictStringLength, "\"", 1);
           memcpy(dictString + dictStringLength + 1, key, keySize);
           memcpy(dictString + dictStringLength + 1 + keySize, "\": ", 3);
           dictStringLength += 4 + keySize;
       } else {
           memcpy(dictString + dictStringLength, key, keySize);
           memcpy(dictString + dictStringLength + keySize, ": ", 2);
           dictStringLength += 2 + keySize;
           free(key);
       }

       char *element;
       int elementSize;

       if (IS_STRING(item->value)) {
           ObjString *s = AS_STRING(item->value);
           element = s->chars;
           elementSize = s->length;
       } else {
           element = valueToString(item->value);
           elementSize = strlen(element);
       }

       if (elementSize > (size - dictStringLength - 3)) {
           if (elementSize > size * 2) {
               size += elementSize * 2 + 3;
           } else {
               size = size * 2 + 3;
           }

           char *newB = realloc(dictString, sizeof(char) * size);

           if (newB == NULL) {
               printf("Unable to allocate memory\n");
               exit(71);
           }

           dictString = newB;
       }

       if (IS_STRING(item->value)) {
           memcpy(dictString + dictStringLength, "\"", 1);
           memcpy(dictString + dictStringLength + 1, element, elementSize);
           memcpy(dictString + dictStringLength + 1 + elementSize, "\"", 1);
           dictStringLength += 2 + elementSize;
       } else {
           memcpy(dictString + dictStringLength, element, elementSize);
           dictStringLength += elementSize;
           free(element);
       }

       if (count != dict->count) {
           memcpy(dictString + dictStringLength, ", ", 2);
           dictStringLength += 2;
       }
   }

   memcpy(dictString + dictStringLength, "}", 1);
   dictString[dictStringLength + 1] = '\0';

   return dictString;
}

char *setToString(Value value) {
    int count = 0;
    int size = 50;
    ObjSet *set = AS_SET(value);
    char *setString = malloc(sizeof(char) * size);
    memcpy(setString, "{", 1);
    int setStringLength = 1;

    for (int i = 0; i <= set->capacityMask; ++i) {
        SetItem *item = &set->entries[i];
        if (IS_EMPTY(item->value) || item->deleted)
            continue;

        count++;

        char *element;
        int elementSize;

        if (IS_STRING(item->value)) {
            ObjString *s = AS_STRING(item->value);
            element = s->chars;
            elementSize = s->length;
        } else {
            element = valueToString(item->value);
            elementSize = strlen(element);
        }

        if (elementSize > (size - setStringLength - 5)) {
            if (elementSize > size * 2) {
                size += elementSize * 2 + 5;
            } else {
                size = size * 2 + 5;
            }

            char *newB = realloc(setString, sizeof(char) * size);

            if (newB == NULL) {
                printf("Unable to allocate memory\n");
                exit(71);
            }

            setString = newB;
        }


        if (IS_STRING(item->value)) {
            memcpy(setString + setStringLength, "\"", 1);
            memcpy(setString + setStringLength + 1, element, elementSize);
            memcpy(setString + setStringLength + 1 + elementSize, "\"", 1);
            setStringLength += 2 + elementSize;
        } else {
            memcpy(setString + setStringLength, element, elementSize);
            setStringLength += elementSize;
            free(element);
        }

        if (count != set->count) {
            memcpy(setString + setStringLength, ", ", 2);
            setStringLength += 2;
        }
    }

    memcpy(setString + setStringLength, "}", 1);
    setString[setStringLength + 1] = '\0';

    return setString;
}

char *classToString(Value value) {
    ObjClass *klass = AS_CLASS(value);
    char *classString = malloc(sizeof(char) * (klass->name->length + 7));
    memcpy(classString, "<cls ", 5);
    memcpy(classString + 5, klass->name->chars, klass->name->length);
    memcpy(classString + 5 + klass->name->length, ">", 1);
    classString[klass->name->length + 6] = '\0';
    return classString;
}

char *instanceToString(Value value) {
    ObjInstance *instance = AS_INSTANCE(value);
    char *instanceString = malloc(sizeof(char) * (instance->klass->name->length + 12));
    memcpy(instanceString, "<", 1);
    memcpy(instanceString + 1, instance->klass->name->chars, instance->klass->name->length);
    memcpy(instanceString + 1 + instance->klass->name->length, " instance>", 10);
    instanceString[instance->klass->name->length + 11] = '\0';
    return instanceString;
}

char *objectToString(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_MODULE: {
            ObjModule *module = AS_MODULE(value);
            char *moduleString = malloc(sizeof(char) * (module->name->length + 11));
            snprintf(moduleString, (module->name->length + 10), "<module %s>", module->name->chars);
            return moduleString;
        }

        case OBJ_NATIVE_CLASS:
        case OBJ_CLASS: {
            return classToString(value);
        }

        case OBJ_TRAIT: {
            ObjTrait *trait = AS_TRAIT(value);
            char *traitString = malloc(sizeof(char) * (trait->name->length + 10));
            snprintf(traitString, trait->name->length + 9, "<trait %s>", trait->name->chars);
            return traitString;
        }

        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *method = AS_BOUND_METHOD(value);
            char *methodString;

            if (method->method->function->name != NULL) {
                methodString = malloc(sizeof(char) * (method->method->function->name->length + 17));
                char *methodType = method->method->function->staticMethod ? "<static method %s>" : "<bound method %s>";
                snprintf(methodString, method->method->function->name->length + 17, methodType, method->method->function->name->chars);
            } else {
                methodString = malloc(sizeof(char) * 16);
                char *methodType = method->method->function->staticMethod ? "<static method>" : "<bound method>";
                snprintf(methodString, 16, "%s", methodType);
            }

            return methodString;
        }

        case OBJ_CLOSURE: {
            ObjClosure *closure = AS_CLOSURE(value);
            char *closureString;

            if (closure->function->name != NULL) {
                closureString = malloc(sizeof(char) * (closure->function->name->length + 6));
                snprintf(closureString, closure->function->name->length + 6, "<fn %s>", closure->function->name->chars);
            } else {
                closureString = malloc(sizeof(char) * 9);
                snprintf(closureString, 9, "%s", "<script>");
            }

            return closureString;
        }

        case OBJ_FUNCTION: {
            ObjFunction *function = AS_FUNCTION(value);
            char *functionString;

            if (function->name != NULL) {
                functionString = malloc(sizeof(char) * (function->name->length + 6));
                snprintf(functionString, function->name->length + 6, "<fn %s>", function->name->chars);
            } else {
                functionString = malloc(sizeof(char) * 5);
                snprintf(functionString, 5, "%s", "<fn>");
            }

            return functionString;
        }

        case OBJ_INSTANCE: {
            return instanceToString(value);
        }

        case OBJ_NATIVE: {
            char *nativeString = malloc(sizeof(char) * 12);
            snprintf(nativeString, 12, "%s", "<native fn>");
            return nativeString;
        }

        case OBJ_STRING: {
            ObjString *stringObj = AS_STRING(value);
            char *string = malloc(sizeof(char) * stringObj->length + 3);
            snprintf(string, stringObj->length + 3, "'%s'", stringObj->chars);
            return string;
        }

        case OBJ_FILE: {
            ObjFile *file = AS_FILE(value);
            char *fileString = malloc(sizeof(char) * (strlen(file->path) + 8));
            snprintf(fileString, strlen(file->path) + 8, "<file %s>", file->path);
            return fileString;
        }

        case OBJ_LIST: {
            return listToString(value);
        }

        case OBJ_DICT: {
            return dictToString(value);
        }

        case OBJ_SET: {
            return setToString(value);
        }

        case OBJ_UPVALUE: {
            char *nativeString = malloc(sizeof(char) * 8);
            snprintf(nativeString, 8, "%s", "upvalue");
            return nativeString;
        }
    }

    char *unknown = malloc(sizeof(char) * 9);
    snprintf(unknown, 8, "%s", "unknown");
    return unknown;
}

    /* 5: scanner.c */


typedef struct {
    const char *start;
    const char *current;
    int line;
    bool rawString;
} Scanner;

Scanner scanner;

void initScanner(const char *source) {
    scanner.start = source;
    scanner.current = source;
    scanner.line = 1;
    scanner.rawString = false;
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAtEnd() {
    return *scanner.current == '\0';
}

static char scan_advance() {
    scanner.current++;
    return scanner.current[-1];
}

static char scan_peek() {
    return *scanner.current;
}

static char scan_peekNext() {
    if (isAtEnd()) return '\0';
    return scanner.current[1];
}

static bool scan_match(char expected) {
    if (isAtEnd()) return false;
    if (*scanner.current != expected) return false;

    scanner.current++;
    return true;
}

static Token makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner.start;
    token.length = (int) (scanner.current - scanner.start);
    token.line = scanner.line;

    return token;
}

static Token errorToken(const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int) strlen(message);
    token.line = scanner.line;

    return token;
}

static void skipWhitespace() {
    for (;;) {
        char c = scan_peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                scan_advance();
                break;

            case '\n':
                scanner.line++;
                scan_advance();
                break;

            case '/':
                if (scan_peekNext() == '*') {
                    // Multiline comments
                    scan_advance();
                    scan_advance();
                    while (true) {
                        while (scan_peek() != '*' && !isAtEnd()) {
                            if ((c = scan_advance()) == '\n') {
                                scanner.line++;
                            }
                        }

                        if (isAtEnd())
                            return;

                        if (scan_peekNext() == '/') {
                            break;
                        }
                        scan_advance();
                    }
                    scan_advance();
                    scan_advance();
                } else if (scan_peekNext() == '/') {
                    // A comment goes until the end of the line.
                    while (scan_peek() != '\n' && !isAtEnd()) scan_advance();
                } else {
                    return;
                }
                break;

            default:
                return;
        }
    }
}

static TokenType checkKeyword(int start, int length,
                              const char *rest, TokenType type) {
    if (scanner.current - scanner.start == start + length &&
        memcmp(scanner.start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
    switch (scanner.start[0]) {
        case 'a':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'n': {
                        return checkKeyword(2, 1, "d", TOKEN_AND);
                    }

                    case 's': {
                        return checkKeyword(2, 0, "", TOKEN_AS);
                    }
                }
            }
            break;
        case 'b':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'e':
                        return checkKeyword(2, 1, "g", TOKEN_LEFT_BRACE);
                    case 'r':
                        return checkKeyword(2, 3, "eak", TOKEN_BREAK);
                 }
             }
             break;
        case 'c':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'l':
                        return checkKeyword(2, 3, "ass", TOKEN_CLASS);
                    case 'o':
                        // Skip second char
                        // Skip third char
                        if (scanner.current - scanner.start > 3) {
                            switch (scanner.start[3]) {
                                case 't':
                                    return checkKeyword(4, 4, "inue", TOKEN_CONTINUE);
                                case 's':
                                    return checkKeyword(4, 1, "t", TOKEN_CONST);
                            }
                        }

                }
            }
            break;
        case 'd':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'e':
                        return checkKeyword(2, 1, "f", TOKEN_DEF);
                    case 'o':
                        return checkKeyword(1, 1, "o", TOKEN_LEFT_BRACE);
                }
            }
            break;
        case 'e':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'n':
                        return checkKeyword(2, 1, "d", TOKEN_RIGHT_BRACE);
                    case 'l':
                        return checkKeyword(2, 2, "se", TOKEN_ELSE);
                }
            }
            break;
        case 'f':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'a':
                        return checkKeyword(2, 3, "lse", TOKEN_FALSE);
                    case 'o':
                        if (scanner.current - scanner.start == 3)
                            return checkKeyword(2, 1, "r", TOKEN_FOR);

                        if (TOKEN_IDENTIFIER == checkKeyword(2, 5, "rever", 0))
                            return TOKEN_IDENTIFIER;

                        char *modified = (char *) scanner.start;
                        char replaced[] = "while (1) {";
                        for (int i = 0; i < 11; i++)
                            modified[i] = replaced[i];
                        scanner.start   = (const char *) modified;
                        scanner.current = scanner.start + 5;
                        return TOKEN_WHILE;
                }
            }
            break;
        case 'i':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'f':
                        return checkKeyword(2, 0, "", TOKEN_IF);
                    case 'm':
                        return checkKeyword(2, 4, "port", TOKEN_IMPORT);
                    case 's':
                      if (scanner.current - (scanner.start + 1) > 1)
                          return checkKeyword(2, 3, "not", TOKEN_BANG_EQUAL);
                      return checkKeyword(1, 1, "s", TOKEN_EQUAL_EQUAL);
                 }
            }
            break;
        case 'n':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'o':
                        return checkKeyword(2, 1, "t", TOKEN_BANG);
                    case 'i':
                        return checkKeyword(2, 1, "l", TOKEN_NIL);
                }
            }
            break;
        case 'o':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'r':
                        if (scanner.current - (scanner.start + 1) > 1) {
                            if (TOKEN_ELSE != checkKeyword(2, 4, "else", TOKEN_ELSE))
                                return TOKEN_IDENTIFIER;

                            char *modified = (char *) scanner.start;
                            modified[0] = '}'; modified[1] = ' ';
                            scanner.start   = (const char *) modified;
                            scanner.current = scanner.start + 1;
                            return TOKEN_RIGHT_BRACE;
                        }
                    return checkKeyword(1, 1, "r", TOKEN_OR);
                }
             }
             break;
        case 'r':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'e':
                        return checkKeyword(2, 4, "turn", TOKEN_RETURN);
                }
            } else {
                if (scanner.start[1] == '"' || scanner.start[1] == '\'') {
                    scanner.rawString = true;
                    return TOKEN_R;
                }
            }
            break;
        case 's':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'u':
                        return checkKeyword(2, 3, "per", TOKEN_SUPER);
                    case 't':
                        return checkKeyword(2, 4, "atic", TOKEN_STATIC);
                }
            }
            break;
        case 't':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h':
                  	  if (scanner.current - scanner.start > 1) {
                            switch (scanner.start[2]) {
                                case 'e':
                                    return checkKeyword(3, 1, "n", TOKEN_LEFT_BRACE);
                                case 'i':
                                    return checkKeyword(3, 1, "s", TOKEN_THIS);
                            }
                        }
                        break;
                    case 'r':
                        if (scanner.current - scanner.start > 1) {
                            switch (scanner.start[2]) {
                                case 'u':
                                    return checkKeyword(3, 1, "e", TOKEN_TRUE);
                                case 'a':
                                    return checkKeyword(3, 2, "it", TOKEN_TRAIT);
                            }
                        }
                    break;
                }
            }
            break;
        case 'u':
            return checkKeyword(1, 2, "se", TOKEN_USE);
        case 'v':
            return checkKeyword(1, 2, "ar", TOKEN_VAR);
        case 'w':
            if (scanner.current - scanner.start > 1) {
                switch (scanner.start[1]) {
                    case 'h':
                        return checkKeyword(2, 3, "ile", TOKEN_WHILE);
                    case 'i':
                        return checkKeyword(2, 2, "th", TOKEN_WITH);
                }
            }
            break;
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier() {
    while (isAlpha(scan_peek()) || isDigit(scan_peek())) scan_advance();

    return makeToken(identifierType());
}

static Token number() {
    while (isDigit(scan_peek())) scan_advance();

    // Look for a fractional part.
    if (scan_peek() == '.' && isDigit(scan_peekNext())) {
        // Consume the "."
        scan_advance();

        while (isDigit(scan_peek())) scan_advance();
    }

    return makeToken(TOKEN_NUMBER);
}


static Token string(char stringToken) {
    while (scan_peek() != stringToken && !isAtEnd()) {
        if (scan_peek() == '\n') {
            scanner.line++;
        } else if (scan_peek() == '\\' && !scanner.rawString) {
             scanner.current++;
        }
        scan_advance();
    }

    if (isAtEnd()) return errorToken("Unterminated string.");

    // The closing " or '.
    scan_advance();
    scanner.rawString = false;
    return makeToken(TOKEN_STRING);
}

void backTrack() {
    scanner.current--;
}

Token scanToken() {
    skipWhitespace();

    scanner.start = scanner.current;

    if (isAtEnd()) return makeToken(TOKEN_EOF);

    char c = scan_advance();

    if (isAlpha(c)) return identifier();
    if (isDigit(c)) return number();

    switch (c) {
        case '(':
            return makeToken(TOKEN_LEFT_PAREN);
        case ')':
            return makeToken(TOKEN_RIGHT_PAREN);
        case '{':
            return makeToken(TOKEN_LEFT_BRACE);
        case '}':
            return makeToken(TOKEN_RIGHT_BRACE);
        case '[':
            return makeToken(TOKEN_LEFT_BRACKET);
        case ']':
            return makeToken(TOKEN_RIGHT_BRACKET);
        case ';':
            return makeToken(TOKEN_SEMICOLON);
        case ':':
            return makeToken(TOKEN_COLON);
        case ',':
            return makeToken(TOKEN_COMMA);
        case '.':
            return makeToken(TOKEN_DOT);
        case '/': {
            if (scan_match('=')) {
                return makeToken(TOKEN_DIVIDE_EQUALS);
            } else {
                return makeToken(TOKEN_SLASH);
            }
        }
        case '*': {
            if (scan_match('=')) {
                return makeToken(TOKEN_MULTIPLY_EQUALS);
            } else if (scan_match('*')) {
                return makeToken(TOKEN_STAR_STAR);
            } else {
                return makeToken(TOKEN_STAR);
            }
        }
        case '%':
            return makeToken(TOKEN_PERCENT);
        case '-': {
            if (scan_match('-')) {
                return makeToken(TOKEN_MINUS_MINUS);
            } else if (scan_match('=')) {
                return makeToken(TOKEN_MINUS_EQUALS);
            } else {
                return makeToken(TOKEN_MINUS);
            }
        }
        case '+': {
            if (scan_match('+')) {
                return makeToken(TOKEN_PLUS_PLUS);
            } else if (scan_match('=')) {
                return makeToken(TOKEN_PLUS_EQUALS);
            } else {
                return makeToken(TOKEN_PLUS);
            }
        }
        case '&':
            return makeToken(scan_match('=') ? TOKEN_AMPERSAND_EQUALS : TOKEN_AMPERSAND);
        case '^':
            return makeToken(scan_match('=') ? TOKEN_CARET_EQUALS : TOKEN_CARET);
        case '|':
            return makeToken(scan_match('=') ? TOKEN_PIPE_EQUALS : TOKEN_PIPE);
        case '!':
            return makeToken(scan_match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            if (scan_match('=')) {
                return makeToken(TOKEN_EQUAL_EQUAL);
            } else if (scan_match('>')) {
                return makeToken(TOKEN_ARROW);
            } else {
                return makeToken(TOKEN_EQUAL);
            }
        case '<':
            return makeToken(scan_match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(scan_match('=') ?
                             TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"':
            return string('"');
        case '\'':
            return string('\'');
    }

    return errorToken("Unexpected character.");
}

    /* 6: compiler.c */


#ifdef DEBUG_PRINT_CODE


#endif

static Chunk *currentChunk(Compiler *compiler) {
    return &compiler->function->chunk;
}

static void errorAt(Parser *parser, Token *token, const char *message) {
    if (parser->panicMode) return;
    parser->panicMode = true;

    fprintf(stderr, "[%s line %d] Error", parser->vm->scriptNames[parser->vm->scriptNameCount], token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser->hadError = true;
}

static void error(Parser *parser, const char *message) {
    errorAt(parser, &parser->previous, message);
}

static void errorAtCurrent(Parser *parser, const char *message) {
    errorAt(parser, &parser->current, message);
}

static void advance(Parser *parser) {
    parser->previous = parser->current;

    for (;;) {
        parser->current = scanToken();
        if (parser->current.type != TOKEN_ERROR) break;

        errorAtCurrent(parser, parser->current.start);
    }
}

static void consume(Compiler *compiler, TokenType type, const char *message) {
    if (compiler->parser->current.type == type) {
        advance(compiler->parser);
        return;
    }

    errorAtCurrent(compiler->parser, message);
}

static bool check(Compiler *compiler, TokenType type) {
    return compiler->parser->current.type == type;
}

static bool match(Compiler *compiler, TokenType type) {
    if (!check(compiler, type)) return false;
    advance(compiler->parser);
    return true;
}

static void emitByte(Compiler *compiler, uint8_t byte) {
    writeChunk(compiler->parser->vm, currentChunk(compiler), byte, compiler->parser->previous.line);
}

static void emitBytes(Compiler *compiler, uint8_t byte1, uint8_t byte2) {
    emitByte(compiler, byte1);
    emitByte(compiler, byte2);
}

static void emitLoop(Compiler *compiler, int loopStart) {
    emitByte(compiler, OP_LOOP);

    int offset = currentChunk(compiler)->count - loopStart + 2;
    if (offset > UINT16_MAX) error(compiler->parser, "Loop body too large.");

    emitByte(compiler, (offset >> 8) & 0xff);
    emitByte(compiler, offset & 0xff);
}

// Emits [instruction] followed by a placeholder for a jump offset. The
// placeholder can be patched by calling [jumpPatch]. Returns the index
// of the placeholder.
static int emitJump(Compiler *compiler, uint8_t instruction) {
    emitByte(compiler, instruction);
    emitByte(compiler, 0xff);
    emitByte(compiler, 0xff);
    return currentChunk(compiler)->count - 2;
}

static void emitReturn(Compiler *compiler) {
    // An initializer automatically returns "this".
    if (compiler->type == TYPE_INITIALIZER) {
        emitBytes(compiler, OP_GET_LOCAL, 0);
    } else {
        emitByte(compiler, OP_NIL);
    }

    emitByte(compiler, OP_RETURN);
}

static uint8_t makeConstant(Compiler *compiler, Value value) {
    int constant = addConstant(compiler->parser->vm, currentChunk(compiler), value);
    if (constant > UINT8_MAX) {
        error(compiler->parser, "Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t) constant;
}

static void emitConstant(Compiler *compiler, Value value) {
    emitBytes(compiler, OP_CONSTANT, makeConstant(compiler, value));
}

// Replaces the placeholder argument for a previous CODE_JUMP or
// CODE_JUMP_IF instruction with an offset that jumps to the current
// end of bytecode.
static void patchJump(Compiler *compiler, int offset) {
    // -2 to adjust for the bytecode for the jump offset itself.
    int jump = currentChunk(compiler)->count - offset - 2;

    if (jump > UINT16_MAX) {
        error(compiler->parser, "Too much code to jump over.");
    }

    currentChunk(compiler)->code[offset] = (jump >> 8) & 0xff;
    currentChunk(compiler)->code[offset + 1] = jump & 0xff;
}

static void initCompiler(Parser *parser, Compiler *compiler, Compiler *parent, FunctionType type) {
    compiler->parser = parser;
    compiler->enclosing = parent;
    initTable(&compiler->stringConstants);
    compiler->function = NULL;
    compiler->class = NULL;
    compiler->loop = NULL;

    if (parent != NULL) {
        compiler->class = parent->class;
        compiler->loop = parent->loop;
    }

    compiler->type = type;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;

    parser->vm->compiler = compiler;

    compiler->function = newFunction(parser->vm, parser->module, type == TYPE_STATIC);

    switch (type) {
        case TYPE_INITIALIZER:
        case TYPE_METHOD:
        case TYPE_STATIC:
        case TYPE_FUNCTION:
        case TYPE_ARROW_FUNCTION: {
            compiler->function->name = copyString(
                    parser->vm,
                    parser->previous.start,
                    parser->previous.length
            );
            break;
        }
        case TYPE_TOP_LEVEL: {
            compiler->function->name = NULL;
            break;
        }
    }

    Local *local = &compiler->locals[compiler->localCount++];
    local->depth = compiler->scopeDepth;
    local->isUpvalue = false;
    if (type == TYPE_METHOD || type == TYPE_INITIALIZER) {
        // In a method, it holds the receiver, "this".
        local->name.start = "this";
        local->name.length = 4;
    } else {
        // In a function, it holds the function, but cannot be referenced,
        // so has no name.
        local->name.start = "";
        local->name.length = 0;
    }
}

static ObjFunction *endCompiler(Compiler *compiler) {
    emitReturn(compiler);

    ObjFunction *function = compiler->function;
#ifdef DEBUG_PRINT_CODE
    if (!compiler->parser->hadError) {

        disassembleChunk(currentChunk(compiler),
                         function->name != NULL ? function->name->chars
                                                : compiler->parser->vm->scriptNames[compiler->parser->vm->scriptNameCount]);
    }
#endif
    if (compiler->enclosing != NULL) {
        // Capture the upvalues in the new closure object.
        emitBytes(compiler->enclosing, OP_CLOSURE, makeConstant(compiler->enclosing, OBJ_VAL(function)));

        // Emit arguments for each upvalue to know whether to capture a local
        // or an upvalue.
        for (int i = 0; i < function->upvalueCount; i++) {
            emitByte(compiler->enclosing, compiler->upvalues[i].isLocal ? 1 : 0);
            emitByte(compiler->enclosing, compiler->upvalues[i].index);
        }
    }

    freeTable(compiler->parser->vm, &compiler->stringConstants);
    compiler->parser->vm->compiler = compiler->enclosing;
    return function;
}

static void beginScope(Compiler *compiler) {
    compiler->scopeDepth++;
}

static void endScope(Compiler *compiler) {
    compiler->scopeDepth--;

    while (compiler->localCount > 0 &&
           compiler->locals[compiler->localCount - 1].depth >
           compiler->scopeDepth) {

        if (compiler->locals[compiler->localCount - 1].isUpvalue) {
            emitByte(compiler, OP_CLOSE_UPVALUE);
        } else {
            emitByte(compiler, OP_POP);
        }
        compiler->localCount--;
    }
}

static void expression(Compiler *compiler);

static void statement(Compiler *compiler);

static void declaration(Compiler *compiler);

static ParseRule *getRule(TokenType type);

static void parsePrecedence(Compiler *compiler, Precedence precedence);

static uint8_t identifierConstant(Compiler *compiler, Token *name) {
    ObjString *string = copyString(compiler->parser->vm, name->start, name->length);
    Value indexValue;
    if (tableGet(&compiler->stringConstants, string, &indexValue)) {
        return (uint8_t) AS_NUMBER(indexValue);
    }

    uint8_t index = makeConstant(compiler, OBJ_VAL(string));
    tableSet(compiler->parser->vm, &compiler->stringConstants, string, NUMBER_VAL((double) index));
    return index;
}

static bool identifiersEqual(Token *a, Token *b) {
    if (a->length != b->length) return false;
    return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler *compiler, Token *name, bool inFunction) {
    // Look it up in the local scopes. Look in reverse order so that the
    // most nested variable is found first and shadows outer ones.
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (identifiersEqual(name, &local->name)) {
            if (!inFunction && local->depth == -1) {
                error(compiler->parser, "Cannot read local variable in its own initializer.");
            }
            return i;
        }
    }

    return -1;
}

// Adds an upvalue to [compiler]'s function with the given properties.
// Does not add one if an upvalue for that variable is already in the
// list. Returns the index of the upvalue.
static int addUpvalue(Compiler *compiler, uint8_t index, bool isLocal) {
    // Look for an existing one.
    int upvalueCount = compiler->function->upvalueCount;
    for (int i = 0; i < upvalueCount; i++) {
        Upvalue *upvalue = &compiler->upvalues[i];
        if (upvalue->index == index && upvalue->isLocal == isLocal) {
            return i;
        }
    }

    // If we got here, it's a new upvalue.
    if (upvalueCount == UINT8_COUNT) {
        error(compiler->parser, "Too many closure variables in function.");
        return 0;
    }

    compiler->upvalues[upvalueCount].isLocal = isLocal;
    compiler->upvalues[upvalueCount].index = index;
    return compiler->function->upvalueCount++;
}

// Attempts to look up [name] in the functions enclosing the one being
// compiled by [compiler]. If found, it adds an upvalue for it to this
// compiler's list of upvalues (unless it's already in there) and
// returns its index. If not found, returns -1.
//
// If the name is found outside of the immediately enclosing function,
// this will flatten the closure and add upvalues to all of the
// intermediate functions so that it gets walked down to this one.
static int resolveUpvalue(Compiler *compiler, Token *name) {
    // If we are at the top level, we didn't find it.
    if (compiler->enclosing == NULL) return -1;

    // See if it's a local variable in the immediately enclosing function.
    int local = resolveLocal(compiler->enclosing, name, true);
    if (local != -1) {
        // Mark the local as an upvalue so we know to close it when it goes
        // out of scope.
        compiler->enclosing->locals[local].isUpvalue = true;
        return addUpvalue(compiler, (uint8_t) local, true);
    }

    // See if it's an upvalue in the immediately enclosing function. In
    // other words, if it's a local variable in a non-immediately
    // enclosing function. This "flattens" closures automatically: it
    // adds upvalues to all of the intermediate functions to get from the
    // function where a local is declared all the way into the possibly
    // deeply nested function that is closing over it.
    int upvalue = resolveUpvalue(compiler->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(compiler, (uint8_t) upvalue, false);
    }

    // If we got here, we walked all the way up the parent chain and
    // couldn't find it.
    return -1;
}

static void addLocal(Compiler *compiler, Token name) {
    if (compiler->localCount == UINT8_COUNT) {
        error(compiler->parser, "Too many local variables in function.");
        return;
    }

    Local *local = &compiler->locals[compiler->localCount];
    local->name = name;

    // The local is declared but not yet defined.
    local->depth = -1;
    local->isUpvalue = false;
    local->constant = false;
    compiler->localCount++;
}

// Allocates a local slot for the value currently on the stack, if
// we're in a local scope.
static void declareVariable(Compiler *compiler) {
    // Global variables are implicitly declared.
    if (compiler->scopeDepth == 0) return;

    // See if a local variable with this name is already declared in this
    // scope.
    Token *name = &compiler->parser->previous;
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (local->depth != -1 && local->depth < compiler->scopeDepth) break;
        if (identifiersEqual(name, &local->name)) {
            error(compiler->parser, "Variable with this name already declared in this scope.");
        }
    }

    addLocal(compiler, *name);
}

static uint8_t parseVariable(Compiler *compiler, const char *errorMessage, bool constant) {
    UNUSED(constant);

    consume(compiler, TOKEN_IDENTIFIER, errorMessage);

    // If it's a global variable, create a string constant for it.
    if (compiler->scopeDepth == 0) {
        return identifierConstant(compiler, &compiler->parser->previous);
    }

    declareVariable(compiler);
    return 0;
}

static void defineVariable(Compiler *compiler, uint8_t global, bool constant) {
    if (compiler->scopeDepth == 0) {
        if (constant) {
            tableSet(compiler->parser->vm, &compiler->parser->vm->constants, AS_STRING(currentChunk(compiler)->constants.values[global]), NIL_VAL);
        } else {
            // If it's not constant, remove
            tableDelete(compiler->parser->vm, &compiler->parser->vm->constants, AS_STRING(currentChunk(compiler)->constants.values[global]));
        }

        emitBytes(compiler, OP_DEFINE_GLOBAL, global);
    } else {
        // Mark the local as defined now.
        compiler->locals[compiler->localCount - 1].depth = compiler->scopeDepth;
        compiler->locals[compiler->localCount - 1].constant = constant;
    }
}

static int argumentList(Compiler *compiler) {
    int argCount = 0;
    if (!check(compiler, TOKEN_RIGHT_PAREN)) {
        do {
            expression(compiler);
            argCount++;

            if (argCount > 255) {
                error(compiler->parser, "Cannot have more than 255 arguments.");
            }
        } while (match(compiler, TOKEN_COMMA));
    }

    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");

    return argCount;
}

static void and_(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    // left operand...
    // OP_JUMP_IF       ------.
    // OP_POP // left operand |
    // right operand...       |
    //   <--------------------'
    // ...

    // Short circuit if the left operand is false.
    int endJump = emitJump(compiler, OP_JUMP_IF_FALSE);

    // Compile the right operand.
    emitByte(compiler, OP_POP); // Left operand.
    parsePrecedence(compiler, PREC_AND);

    patchJump(compiler, endJump);
}

static void binary(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    TokenType operatorType = compiler->parser->previous.type;

    ParseRule *rule = getRule(operatorType);
    parsePrecedence(compiler, (Precedence) (rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:
            emitBytes(compiler, OP_EQUAL, OP_NOT);
            break;
        case TOKEN_EQUAL_EQUAL:
            emitByte(compiler, OP_EQUAL);
            break;
        case TOKEN_GREATER:
            emitByte(compiler, OP_GREATER);
            break;
        case TOKEN_GREATER_EQUAL:
            emitBytes(compiler, OP_LESS, OP_NOT);
            break;
        case TOKEN_LESS:
            emitByte(compiler, OP_LESS);
            break;
        case TOKEN_LESS_EQUAL:
            emitBytes(compiler, OP_GREATER, OP_NOT);
            break;
        case TOKEN_PLUS:
            emitByte(compiler, OP_ADD);
            break;
        case TOKEN_MINUS:
            emitBytes(compiler, OP_NEGATE, OP_ADD);
            break;
        case TOKEN_STAR:
            emitByte(compiler, OP_MULTIPLY);
            break;
        case TOKEN_STAR_STAR:
            emitByte(compiler, OP_POW);
            break;
        case TOKEN_SLASH:
            emitByte(compiler, OP_DIVIDE);
            break;
        case TOKEN_PERCENT:
            emitByte(compiler, OP_MOD);
            break;
        case TOKEN_AMPERSAND:
            emitByte(compiler, OP_BITWISE_AND);
            break;
        case TOKEN_CARET:
            emitByte(compiler, OP_BITWISE_XOR);
            break;
        case TOKEN_PIPE:
            emitByte(compiler, OP_BITWISE_OR);
            break;
        default:
            return;
    }
}

static void comp_call(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    int argCount = argumentList(compiler);
    emitBytes(compiler, OP_CALL, argCount);
}

static void dot(Compiler *compiler, bool canAssign) {
    consume(compiler, TOKEN_IDENTIFIER, "Expect property name after '.'.");
    uint8_t name = identifierConstant(compiler, &compiler->parser->previous);

    if (canAssign && match(compiler, TOKEN_EQUAL)) {
        expression(compiler);
        emitBytes(compiler, OP_SET_PROPERTY, name);
    } else if (match(compiler, TOKEN_LEFT_PAREN)) {
        int argCount = argumentList(compiler);
        emitBytes(compiler, OP_INVOKE, argCount);
        emitByte(compiler, name);
    } else if (canAssign && match(compiler, TOKEN_PLUS_EQUALS)) {
        emitBytes(compiler, OP_GET_PROPERTY_NO_POP, name);
        expression(compiler);
        emitByte(compiler, OP_ADD);
        emitBytes(compiler, OP_SET_PROPERTY, name);
    } else if (canAssign && match(compiler, TOKEN_MINUS_EQUALS)) {
        emitBytes(compiler, OP_GET_PROPERTY_NO_POP, name);
        expression(compiler);
        emitBytes(compiler, OP_NEGATE, OP_ADD);
        emitBytes(compiler, OP_SET_PROPERTY, name);
    } else if (canAssign && match(compiler, TOKEN_MULTIPLY_EQUALS)) {
        emitBytes(compiler, OP_GET_PROPERTY_NO_POP, name);
        expression(compiler);
        emitByte(compiler, OP_MULTIPLY);
        emitBytes(compiler, OP_SET_PROPERTY, name);
    } else if (canAssign && match(compiler, TOKEN_DIVIDE_EQUALS)) {
        emitBytes(compiler, OP_GET_PROPERTY_NO_POP, name);
        expression(compiler);
        emitByte(compiler, OP_DIVIDE);
        emitBytes(compiler, OP_SET_PROPERTY, name);
    } else if (canAssign && match(compiler, TOKEN_AMPERSAND_EQUALS)) {
        emitBytes(compiler, OP_GET_PROPERTY_NO_POP, name);
        expression(compiler);
        emitByte(compiler, OP_BITWISE_AND);
        emitBytes(compiler, OP_SET_PROPERTY, name);
    } else if (canAssign && match(compiler, TOKEN_CARET_EQUALS)) {
        emitBytes(compiler, OP_GET_PROPERTY_NO_POP, name);
        expression(compiler);
        emitByte(compiler, OP_BITWISE_XOR);
        emitBytes(compiler, OP_SET_PROPERTY, name);
    } else if (canAssign && match(compiler, TOKEN_PIPE_EQUALS)) {
        emitBytes(compiler, OP_GET_PROPERTY_NO_POP, name);
        expression(compiler);
        emitByte(compiler, OP_BITWISE_OR);
        emitBytes(compiler, OP_SET_PROPERTY, name);
    } else {
        emitBytes(compiler, OP_GET_PROPERTY, name);
    }
}

static void literal(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    switch (compiler->parser->previous.type) {
        case TOKEN_FALSE:
            emitByte(compiler, OP_FALSE);
            break;
        case TOKEN_NIL:
            emitByte(compiler, OP_NIL);
            break;
        case TOKEN_TRUE:
            emitByte(compiler, OP_TRUE);
            break;
        default:
            return; // Unreachable.
    }
}

static void block(Compiler *compiler) {
    while (!check(compiler, TOKEN_RIGHT_BRACE) && !check(compiler, TOKEN_EOF)) {
        declaration(compiler);
    }

    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void beginFunction(Compiler *compiler, Compiler *fnCompiler, FunctionType type) {
    initCompiler(compiler->parser, fnCompiler, compiler, type);
    beginScope(fnCompiler);

    // Compile the parameter list.
    consume(fnCompiler, TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    if (!check(fnCompiler, TOKEN_RIGHT_PAREN)) {
        bool optional = false;
        do {
            uint8_t paramConstant = parseVariable(fnCompiler, "Expect parameter name.", false);
            defineVariable(fnCompiler, paramConstant, false);

            if (match(fnCompiler, TOKEN_EQUAL)) {
                fnCompiler->function->arityOptional++;
                optional = true;
                expression(fnCompiler);
            } else {
                fnCompiler->function->arity++;

                if (optional) {
                    error(fnCompiler->parser, "Cannot have non-optional parameter after optional.");
                }
            }

            if (fnCompiler->function->arity + fnCompiler->function->arityOptional > 255) {
                error(fnCompiler->parser, "Cannot have more than 255 parameters.");
            }
        } while (match(fnCompiler, TOKEN_COMMA));

        if (fnCompiler->function->arityOptional > 0) {
            emitByte(fnCompiler, OP_DEFINE_OPTIONAL);
        }
    }

    consume(fnCompiler, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
}

static void arrow(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    Compiler fnCompiler;

    // Setup function and parse parameters
    beginFunction(compiler, &fnCompiler, TYPE_ARROW_FUNCTION);

    consume(&fnCompiler, TOKEN_ARROW, "Expect '=>' after function arguments.");

    if (match(&fnCompiler, TOKEN_LEFT_BRACE)) {
        // Brace so expend function body
        block(&fnCompiler);
    } else {
        // No brace so expect single expression
        expression(&fnCompiler);
        emitByte(&fnCompiler, OP_RETURN);
    }
    endCompiler(&fnCompiler);
}

static void grouping(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    expression(compiler);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void comp_number(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    double value = strtod(compiler->parser->previous.start, NULL);
    emitConstant(compiler, NUMBER_VAL(value));
}

static void or_(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    // left operand...
    // OP_JUMP_IF       ---.
    // OP_JUMP          ---+--.
    //   <-----------------'  |
    // OP_POP // left operand |
    // right operand...       |
    //   <--------------------'
    // ...

    // If the operand is *true* we want to keep it, so when it's false,
    // jump to the code to evaluate the right operand.
    int elseJump = emitJump(compiler, OP_JUMP_IF_FALSE);

    // If we get here, the operand is true, so jump to the end to keep it.
    int endJump = emitJump(compiler, OP_JUMP);

    // Compile the right operand.
    patchJump(compiler, elseJump);
    emitByte(compiler, OP_POP); // Left operand.

    parsePrecedence(compiler, PREC_OR);
    patchJump(compiler, endJump);
}

int parseString(char *string, int length) {
    for (int i = 0; i < length - 1; i++) {
        if (string[i] == '\\') {
            switch (string[i + 1]) {
                case 'n': {
                    string[i + 1] = '\n';
                    break;
                }
                case 't': {
                    string[i + 1] = '\t';
                    break;
                }
                case 'r': {
                    string[i + 1] = '\r';
                    break;
                }
                case 'v': {
                    string[i + 1] = '\v';
                    break;
                }
                case '\\': {
                    string[i + 1] = '\\';
                    break;
                }
                case '\'':
                case '"': {
                    break;
                }
                default: {
                    continue;
                }
            }
            memmove(&string[i], &string[i + 1], length - i);
            length -= 1;
        }
    }

    return length;
}

static void rString(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    if (match(compiler, TOKEN_STRING)) {
        Parser *parser = compiler->parser;
        emitConstant(compiler, OBJ_VAL(copyString(parser->vm, parser->previous.start + 1,
                                                  parser->previous.length - 2)));

        return;
    }

    consume(compiler, TOKEN_STRING, "Expected string after r delimiter");
}

static void comp_string(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    Parser *parser = compiler->parser;

    char *string = malloc(sizeof(char) * parser->previous.length - 1);
    memcpy(string, parser->previous.start + 1, parser->previous.length - 2);
    int length = parseString(string, parser->previous.length - 2);
    string[length] = '\0';

    emitConstant(compiler, OBJ_VAL(copyString(parser->vm, string, length)));
    free(string);
}

static void list(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    emitByte(compiler, OP_NEW_LIST);

    do {
        if (check(compiler, TOKEN_RIGHT_BRACKET))
            break;

        expression(compiler);
        emitByte(compiler, OP_ADD_LIST);
    } while (match(compiler, TOKEN_COMMA));

    consume(compiler, TOKEN_RIGHT_BRACKET, "Expected closing ']'");
}

static void dict(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    emitByte(compiler, OP_NEW_DICT);

    do {
        if (check(compiler, TOKEN_RIGHT_BRACE))
            break;

        expression(compiler);
        consume(compiler, TOKEN_COLON, "Expected ':'");
        expression(compiler);
        emitByte(compiler, OP_ADD_DICT);
    } while (match(compiler, TOKEN_COMMA));

    consume(compiler, TOKEN_RIGHT_BRACE, "Expected closing '}'");
}

static void subscript(Compiler *compiler, bool canAssign) {
    // slice with no initial index [1, 2, 3][:100]
    if (match(compiler, TOKEN_COLON)) {
        emitByte(compiler, OP_EMPTY);
        expression(compiler);
        emitByte(compiler, OP_SLICE);
        consume(compiler, TOKEN_RIGHT_BRACKET, "Expected closing ']'");
        return;
    }

    expression(compiler);

    if (match(compiler, TOKEN_COLON)) {
        // If we slice with no "ending" push EMPTY so we know
        // To go to the end of the iterable
        // i.e [1, 2, 3][1:]
        if (check(compiler, TOKEN_RIGHT_BRACKET)) {
            emitByte(compiler, OP_EMPTY);
        } else {
            expression(compiler);
        }
        emitByte(compiler, OP_SLICE);
        consume(compiler, TOKEN_RIGHT_BRACKET, "Expected closing ']'");
        return;
    }

    consume(compiler, TOKEN_RIGHT_BRACKET, "Expected closing ']'");

    if (canAssign && match(compiler, TOKEN_EQUAL)) {
        expression(compiler);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_PLUS_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_PUSH, OP_ADD);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_MINUS_EQUALS)) {
        expression(compiler);
        emitByte(compiler, OP_PUSH);
        emitBytes(compiler, OP_NEGATE, OP_ADD);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_MULTIPLY_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_PUSH, OP_MULTIPLY);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_DIVIDE_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_PUSH, OP_DIVIDE);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_AMPERSAND_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_PUSH, OP_BITWISE_AND);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_CARET_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_PUSH, OP_BITWISE_XOR);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_PIPE_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_PUSH, OP_BITWISE_OR);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else {
        emitByte(compiler, OP_SUBSCRIPT);
    }
}

static void checkConst(Compiler *compiler, uint8_t setOp, int arg) {
    if (setOp == OP_SET_LOCAL) {
        if (compiler->locals[arg].constant) {
            error(compiler->parser, "Cannot assign to a constant.");
        }
    } else if (setOp == OP_SET_GLOBAL) {
        Value _;
        if (tableGet(&compiler->parser->vm->constants, AS_STRING(currentChunk(compiler)->constants.values[arg]), &_)) {
            error(compiler->parser, "Cannot assign to a constant.");
        }
    }
}

static void namedVariable(Compiler *compiler, Token name, bool canAssign) {
    uint8_t getOp, setOp;
    int arg = resolveLocal(compiler, &name, false);
    if (arg != -1) {
        getOp = OP_GET_LOCAL;
        setOp = OP_SET_LOCAL;
    } else if ((arg = resolveUpvalue(compiler, &name)) != -1) {
        getOp = OP_GET_UPVALUE;
        setOp = OP_SET_UPVALUE;
    } else {
        arg = identifierConstant(compiler, &name);
        getOp = OP_GET_GLOBAL;
        setOp = OP_SET_GLOBAL;
    }

    if (canAssign && match(compiler, TOKEN_EQUAL)) {
        checkConst(compiler, setOp, arg);
        expression(compiler);
        emitBytes(compiler, setOp, (uint8_t) arg);
    } else if (canAssign && match(compiler, TOKEN_PLUS_EQUALS)) {
        checkConst(compiler, setOp, arg);
        namedVariable(compiler, name, false);
        expression(compiler);
        emitByte(compiler, OP_ADD);
        emitBytes(compiler, setOp, (uint8_t) arg);
    } else if (canAssign && match(compiler, TOKEN_MINUS_EQUALS)) {
        checkConst(compiler, setOp, arg);
        namedVariable(compiler, name, false);
        expression(compiler);
        emitBytes(compiler, OP_NEGATE, OP_ADD);
        emitBytes(compiler, setOp, (uint8_t) arg);
    } else if (canAssign && match(compiler, TOKEN_MULTIPLY_EQUALS)) {
        checkConst(compiler, setOp, arg);
        namedVariable(compiler, name, false);
        expression(compiler);
        emitByte(compiler, OP_MULTIPLY);
        emitBytes(compiler, setOp, (uint8_t) arg);
    } else if (canAssign && match(compiler, TOKEN_DIVIDE_EQUALS)) {
        checkConst(compiler, setOp, arg);
        namedVariable(compiler, name, false);
        expression(compiler);
        emitByte(compiler, OP_DIVIDE);
        emitBytes(compiler, setOp, (uint8_t) arg);
    } else if (canAssign && match(compiler, TOKEN_AMPERSAND_EQUALS)) {
        checkConst(compiler, setOp, arg);
        namedVariable(compiler, name, false);
        expression(compiler);
        emitByte(compiler, OP_BITWISE_AND);
        emitBytes(compiler, setOp, (uint8_t) arg);
    } else if (canAssign && match(compiler, TOKEN_CARET_EQUALS)) {
        checkConst(compiler, setOp, arg);
        namedVariable(compiler, name, false);
        expression(compiler);
        emitByte(compiler, OP_BITWISE_XOR);
        emitBytes(compiler, setOp, (uint8_t) arg);
    } else if (canAssign && match(compiler, TOKEN_PIPE_EQUALS)) {
        checkConst(compiler, setOp, arg);
        namedVariable(compiler, name, false);
        expression(compiler);
        emitByte(compiler, OP_BITWISE_OR);
        emitBytes(compiler, setOp, (uint8_t) arg);
    } else {
        emitBytes(compiler, getOp, (uint8_t) arg);
    }
}

static void variable(Compiler *compiler, bool canAssign) {
    namedVariable(compiler, compiler->parser->previous, canAssign);
}

static Token syntheticToken(const char *text) {
    Token token;
    token.start = text;
    token.length = (int) strlen(text);
    return token;
}

static void pushSuperclass(Compiler *compiler) {
    if (compiler->class == NULL) return;
    namedVariable(compiler, syntheticToken("super"), false);
}

static void super_(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    if (compiler->class == NULL) {
        error(compiler->parser, "Cannot utilise 'super' outside of a class.");
    } else if (!compiler->class->hasSuperclass) {
        error(compiler->parser, "Cannot utilise 'super' in a class with no superclass.");
    }

    consume(compiler, TOKEN_DOT, "Expect '.' after 'super'.");
    consume(compiler, TOKEN_IDENTIFIER, "Expect superclass method name.");
    uint8_t name = identifierConstant(compiler, &compiler->parser->previous);

    // Push the receiver.
    namedVariable(compiler, syntheticToken("this"), false);

    if (match(compiler, TOKEN_LEFT_PAREN)) {
        int argCount = argumentList(compiler);

        pushSuperclass(compiler);
        emitBytes(compiler, OP_SUPER, argCount);
        emitByte(compiler, name);
    } else {
        pushSuperclass(compiler);
        emitBytes(compiler, OP_GET_SUPER, name);
    }
}

static void this_(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    if (compiler->class == NULL) {
        error(compiler->parser, "Cannot utilise 'this' outside of a class.");
    } else if (compiler->class->staticMethod) {
        error(compiler->parser, "Cannot utilise 'this' inside a static method.");
    } else {
        variable(compiler, false);
    }
}

static void static_(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    if (compiler->class == NULL) {
        error(compiler->parser, "Cannot utilise 'static' outside of a class.");
    }
}

static void useStatement(Compiler *compiler) {
    if (compiler->class == NULL) {
        error(compiler->parser, "Cannot utilise 'use' outside of a class.");
    }

    do {
        consume(compiler, TOKEN_IDENTIFIER, "Expect trait name after use statement.");
        namedVariable(compiler, compiler->parser->previous, false);
        emitByte(compiler, OP_USE);
    } while (match(compiler, TOKEN_COMMA));

    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after use statement.");
}

static void unary(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    TokenType operatorType = compiler->parser->previous.type;

    parsePrecedence(compiler, PREC_UNARY);

    switch (operatorType) {
        case TOKEN_BANG:
            emitByte(compiler, OP_NOT);
            break;
        case TOKEN_MINUS:
            emitByte(compiler, OP_NEGATE);
            break;
        default:
            return;
    }
}

static void prefix(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    TokenType operatorType = compiler->parser->previous.type;
    Token cur = compiler->parser->current;
    consume(compiler, TOKEN_IDENTIFIER, "Expected variable");
    namedVariable(compiler, compiler->parser->previous, true);

    int arg;
    bool instance = false;

    if (match(compiler, TOKEN_DOT)) {
        consume(compiler, TOKEN_IDENTIFIER, "Expect property name after '.'.");
        arg = identifierConstant(compiler, &compiler->parser->previous);
        emitBytes(compiler, OP_GET_PROPERTY_NO_POP, arg);
        instance = true;
    }

    switch (operatorType) {
        case TOKEN_PLUS_PLUS: {
            emitByte(compiler, OP_INCREMENT);
            break;
        }
        case TOKEN_MINUS_MINUS:
            emitByte(compiler, OP_DECREMENT);
            break;
        default:
            return;
    }

    if (instance) {
        emitBytes(compiler, OP_SET_PROPERTY, arg);
    } else {
        uint8_t setOp;
        arg = resolveLocal(compiler, &cur, false);
        if (arg != -1) {
            setOp = OP_SET_LOCAL;
        } else if ((arg = resolveUpvalue(compiler, &cur)) != -1) {
            setOp = OP_SET_UPVALUE;
        } else {
            arg = identifierConstant(compiler, &cur);
            setOp = OP_SET_GLOBAL;
        }

        checkConst(compiler, setOp, arg);
        emitBytes(compiler, setOp, (uint8_t) arg);
    }
}

ParseRule rules[] = {
        {grouping, comp_call,      PREC_CALL},               // TOKEN_LEFT_PAREN
        {NULL,     NULL,      PREC_NONE},               // TOKEN_RIGHT_PAREN
        {dict,     NULL,      PREC_NONE},               // TOKEN_LEFT_BRACE [big]
        {NULL,     NULL,      PREC_NONE},               // TOKEN_RIGHT_BRACE
        {list,     subscript, PREC_CALL},               // TOKEN_LEFT_BRACKET
        {NULL,     NULL,      PREC_NONE},               // TOKEN_RIGHT_BRACKET
        {NULL,     NULL,      PREC_NONE},               // TOKEN_COMMA
        {NULL,     dot,       PREC_CALL},               // TOKEN_DOT
        {unary,    binary,    PREC_TERM},               // TOKEN_MINUS
        {NULL,     binary,    PREC_TERM},               // TOKEN_PLUS
        {prefix,   NULL,      PREC_NONE},               // TOKEN_PLUS_PLUS
        {prefix,   NULL,      PREC_NONE},               // TOKEN_MINUS_MINUS
        {NULL,     NULL,      PREC_NONE},               // TOKEN_PLUS_EQUALS
        {NULL,     NULL,      PREC_NONE},               // TOKEN_MINUS_EQUALS
        {NULL,     NULL,      PREC_NONE},               // TOKEN_MULTIPLY_EQUALS
        {NULL,     NULL,      PREC_NONE},               // TOKEN_DIVIDE_EQUALS
        {NULL,     NULL,      PREC_NONE},               // TOKEN_SEMICOLON
        {NULL,     NULL,      PREC_NONE},               // TOKEN_COLON
        {NULL,     binary,    PREC_FACTOR},             // TOKEN_SLASH
        {NULL,     binary,    PREC_FACTOR},             // TOKEN_STAR
        {NULL,     binary,    PREC_INDICES},            // TOKEN_STAR_STAR
        {NULL,     binary,    PREC_FACTOR},             // TOKEN_PERCENT
        {NULL,     binary,    PREC_BITWISE_AND},        // TOKEN_AMPERSAND
        {NULL,     binary,    PREC_BITWISE_XOR},        // TOKEN_CARET
        {NULL,     binary,    PREC_BITWISE_OR},         // TOKEN_PIPE
        {NULL,     NULL,      PREC_NONE},               // TOKEN_AMPERSAND_EQUALS
        {NULL,     NULL,      PREC_NONE},               // TOKEN_CARET_EQUALS
        {NULL,     NULL,      PREC_NONE},               // TOKEN_PIPE_EQUALS
        {unary,    NULL,      PREC_NONE},               // TOKEN_BANG
        {NULL,     binary,    PREC_EQUALITY},           // TOKEN_BANG_EQUAL
        {NULL,     NULL,      PREC_NONE},               // TOKEN_EQUAL
        {NULL,     binary,    PREC_EQUALITY},           // TOKEN_EQUAL_EQUAL
        {NULL,     binary,    PREC_COMPARISON},         // TOKEN_GREATER
        {NULL,     binary,    PREC_COMPARISON},         // TOKEN_GREATER_EQUAL
        {NULL,     binary,    PREC_COMPARISON},         // TOKEN_LESS
        {NULL,     binary,    PREC_COMPARISON},         // TOKEN_LESS_EQUAL
        {rString,  NULL,      PREC_NONE},               // TOKEN_R
        {NULL,     NULL,      PREC_NONE},               // TOKEN_ARROW
        {variable, NULL,      PREC_NONE},               // TOKEN_IDENTIFIER
        {comp_string,   NULL,      PREC_NONE},               // TOKEN_STRING
        {comp_number,   NULL,      PREC_NONE},               // TOKEN_NUMBER
        {NULL,     NULL,      PREC_NONE},               // TOKEN_CLASS
        {NULL,     NULL,      PREC_NONE},               // TOKEN_TRAIT
        {NULL,     NULL,      PREC_NONE},               // TOKEN_USE
        {static_,  NULL,      PREC_NONE},               // TOKEN_STATIC
        {this_,    NULL,      PREC_NONE},               // TOKEN_THIS
        {super_,   NULL,      PREC_NONE},               // TOKEN_SUPER
        {arrow,    NULL,      PREC_NONE},               // TOKEN_DEF
        {NULL,     NULL,      PREC_NONE},               // TOKEN_AS
        {NULL,     NULL,      PREC_NONE},               // TOKEN_IF
        {NULL,     and_,      PREC_AND},                // TOKEN_AND
        {NULL,     NULL,      PREC_NONE},               // TOKEN_ELSE
        {NULL,     or_,       PREC_OR},                 // TOKEN_OR
        {NULL,     NULL,      PREC_NONE},               // TOKEN_VAR
        {NULL,     NULL,      PREC_NONE},               // TOKEN_CONST
        {literal,  NULL,      PREC_NONE},               // TOKEN_TRUE
        {literal,  NULL,      PREC_NONE},               // TOKEN_FALSE
        {literal,  NULL,      PREC_NONE},               // TOKEN_NIL
        {NULL,     NULL,      PREC_NONE},               // TOKEN_FOR
        {NULL,     NULL,      PREC_NONE},               // TOKEN_WHILE
        {NULL,     NULL,      PREC_NONE},               // TOKEN_BREAK
        {NULL,     NULL,      PREC_NONE},               // TOKEN_RETURN
        {NULL,     NULL,      PREC_NONE},               // TOKEN_CONTINUE
        {NULL,     NULL,      PREC_NONE},               // TOKEN_WITH
        {NULL,     NULL,      PREC_NONE},               // TOKEN_EOF
        {NULL,     NULL,      PREC_NONE},               // TOKEN_IMPORT
        {NULL,     NULL,      PREC_NONE},               // TOKEN_ERROR
};

static void parsePrecedence(Compiler *compiler, Precedence precedence) {
    Parser *parser = compiler->parser;
    advance(parser);
    ParseFn prefixRule = getRule(parser->previous.type)->prefix;
    if (prefixRule == NULL) {
        error(parser, "Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(compiler, canAssign);

    while (precedence <= getRule(parser->current.type)->precedence) {
        advance(parser);
        ParseFn infixRule = getRule(parser->previous.type)->infix;
        infixRule(compiler, canAssign);
    }

    if (canAssign && match(compiler, TOKEN_EQUAL)) {
        // If we get here, we didn't parse the "=" even though we could
        // have, so the LHS must not be a valid lvalue.
        error(parser, "Invalid assignment target.");
    }
}

static ParseRule *getRule(TokenType type) {
    return &rules[type];
}

void expression(Compiler *compiler) {
    parsePrecedence(compiler, PREC_ASSIGNMENT);
}

static void comp_function(Compiler *compiler, FunctionType type) {
    Compiler fnCompiler;

    // Setup function and parse parameters
    beginFunction(compiler, &fnCompiler, type);

    // The body.
    consume(&fnCompiler, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block(&fnCompiler);
    /**
     * No need to explicitly reduce the scope here as endCompiler does
     * it for us.
     **/
    endCompiler(&fnCompiler);
}

static void method(Compiler *compiler, bool trait) {
    FunctionType type;

    if (check(compiler, TOKEN_STATIC)) {
        type = TYPE_STATIC;
        consume(compiler, TOKEN_STATIC, "Expect static.");
        compiler->class->staticMethod = true;
    } else {
        type = TYPE_METHOD;
        compiler->class->staticMethod = false;
    }

    consume(compiler, TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifierConstant(compiler, &compiler->parser->previous);

    // If the method is named "init", it's an initializer.
    if (compiler->parser->previous.length == 4 &&
        memcmp(compiler->parser->previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }

    comp_function(compiler, type);

    if (trait) {
        emitBytes(compiler, OP_TRAIT_METHOD, constant);
    } else {
        emitBytes(compiler, OP_METHOD, constant);
    }
}

static void classDeclaration(Compiler *compiler) {
    consume(compiler, TOKEN_IDENTIFIER, "Expect class name.");
    uint8_t nameConstant = identifierConstant(compiler, &compiler->parser->previous);
    declareVariable(compiler);

    ClassCompiler classCompiler;
    classCompiler.name = compiler->parser->previous;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = compiler->class;
    classCompiler.staticMethod = false;
    compiler->class = &classCompiler;

    if (match(compiler, TOKEN_LESS)) {
        consume(compiler, TOKEN_IDENTIFIER, "Expect superclass name.");
        classCompiler.hasSuperclass = true;

        beginScope(compiler);

        // Store the superclass in a local variable named "super".
        variable(compiler, false);
        addLocal(compiler, syntheticToken("super"));

        emitBytes(compiler, OP_SUBCLASS, nameConstant);
    } else {
        emitBytes(compiler, OP_CLASS, nameConstant);
    }

    consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    while (!check(compiler, TOKEN_RIGHT_BRACE) && !check(compiler, TOKEN_EOF)) {
        if (match(compiler, TOKEN_USE)) {
            useStatement(compiler);
        } else {
            method(compiler, false);
        }
    }
    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

    if (classCompiler.hasSuperclass) {
        endScope(compiler);
    }

    defineVariable(compiler, nameConstant, false);

    compiler->class = compiler->class->enclosing;
}

static void traitDeclaration(Compiler *compiler) {
    consume(compiler, TOKEN_IDENTIFIER, "Expect trait name.");
    uint8_t nameConstant = identifierConstant(compiler, &compiler->parser->previous);
    declareVariable(compiler);

    ClassCompiler classCompiler;
    classCompiler.name = compiler->parser->previous;
    classCompiler.hasSuperclass = false;
    classCompiler.enclosing = compiler->class;
    classCompiler.staticMethod = false;
    compiler->class = &classCompiler;

    emitBytes(compiler, OP_TRAIT, nameConstant);

    consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' before trait body.");
    while (!check(compiler, TOKEN_RIGHT_BRACE) && !check(compiler, TOKEN_EOF)) {
        method(compiler, true);
    }
    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after trait body.");

    defineVariable(compiler, nameConstant, false);

    compiler->class = compiler->class->enclosing;
}

static void funDeclaration(Compiler *compiler) {
    uint8_t global = parseVariable(compiler, "Expect function name.", false);
    comp_function(compiler, TYPE_FUNCTION);
    defineVariable(compiler, global, false);
}

static void varDeclaration(Compiler *compiler, bool constant) {
    do {
        uint8_t global = parseVariable(compiler, "Expect variable name.", constant);

        if (match(compiler, TOKEN_EQUAL) || constant) {
            // Compile the initializer.
            expression(compiler);
        } else {
            // Default to nil.
            emitByte(compiler, OP_NIL);
        }

        defineVariable(compiler, global, constant);
    } while (match(compiler, TOKEN_COMMA));

    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
}

static void expressionStatement(Compiler *compiler) {
    expression(compiler);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after expression.");
    if (compiler->parser->vm->repl) {
        emitByte(compiler, OP_POP_REPL);
    } else {
        emitByte(compiler, OP_POP);
    }
}

static void endLoop(Compiler *compiler) {
    if (compiler->loop->end != -1) {
        patchJump(compiler, compiler->loop->end);
        emitByte(compiler, OP_POP); // Condition.
    }

    int i = compiler->loop->body;
    while (i < compiler->function->chunk.count) {
        if (compiler->function->chunk.code[i] == OP_BREAK) {
            compiler->function->chunk.code[i] = OP_JUMP;
            patchJump(compiler, i + 1);
            i += 3;
        } else {
            i++;
        }
    }

    compiler->loop = compiler->loop->enclosing;
}

static void forStatement(Compiler *compiler) {
    // for (var i = 0; i < 10; i = i + 1) print i;
    //
    //   var i = 0;
    // start:                      <--.
    //   if (i < 10) goto exit;  --.  |
    //   goto body;  -----------.  |  |
    // increment:            <--+--+--+--.
    //   i = i + 1;             |  |  |  |
    //   goto start;  ----------+--+--'  |
    // body:                 <--'  |     |
    //   print i;                  |     |
    //   goto increment;  ---------+-----'
    // exit:                    <--'

    // Create a scope for the loop variable.
    beginScope(compiler);

    // The initialization clause.
    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
    if (match(compiler, TOKEN_VAR)) {
        varDeclaration(compiler, false);
    } else if (match(compiler, TOKEN_SEMICOLON)) {
        // No initializer.
    } else {
        expressionStatement(compiler);
    }

    Loop loop;
    loop.start = currentChunk(compiler)->count;
    loop.scopeDepth = compiler->scopeDepth;
    loop.enclosing = compiler->loop;
    compiler->loop = &loop;

    // The exit condition.
    compiler->loop->end = -1;

    if (!match(compiler, TOKEN_SEMICOLON)) {
        expression(compiler);
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after loop condition.");

        // Jump out of the loop if the condition is false.
        compiler->loop->end = emitJump(compiler, OP_JUMP_IF_FALSE);
        emitByte(compiler, OP_POP); // Condition.
    }

    // Increment step.
    if (!match(compiler, TOKEN_RIGHT_PAREN)) {
        // We don't want to execute the increment before the body, so jump
        // over it.
        int bodyJump = emitJump(compiler, OP_JUMP);

        int incrementStart = currentChunk(compiler)->count;
        expression(compiler);
        emitByte(compiler, OP_POP);
        consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

        emitLoop(compiler, compiler->loop->start);
        compiler->loop->start = incrementStart;

        patchJump(compiler, bodyJump);
    }

    // Compile the body.
    compiler->loop->body = compiler->function->chunk.count;
    statement(compiler);

    // Jump back to the beginning (or the increment).
    emitLoop(compiler, compiler->loop->start);

    endLoop(compiler);
    endScope(compiler); // Loop variable.
}

static void breakStatement(Compiler *compiler) {
    if (compiler->loop == NULL) {
        error(compiler->parser, "Cannot utilise 'break' outside of a loop.");
        return;
    }

    consume(compiler, TOKEN_SEMICOLON, "Expected semicolon after break");

    // Discard any locals created inside the loop.
    for (int i = compiler->localCount - 1;
         i >= 0 && compiler->locals[i].depth > compiler->loop->scopeDepth;
         i--) {
        emitByte(compiler, OP_POP);
    }

    emitJump(compiler, OP_BREAK);
}

static void continueStatement(Compiler *compiler) {
    if (compiler->loop == NULL) {
        error(compiler->parser, "Cannot utilise 'continue' outside of a loop.");
    }

    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after 'continue'.");

    // Discard any locals created inside the loop.
    for (int i = compiler->localCount - 1;
         i >= 0 && compiler->locals[i].depth > compiler->loop->scopeDepth;
         i--) {
        emitByte(compiler, OP_POP);
    }

    // Jump to top of current innermost loop.
    emitLoop(compiler, compiler->loop->start);
}

static void ifStatement(Compiler *compiler) {
    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    expression(compiler);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    // Jump to the else branch if the condition is false.
    int elseJump = emitJump(compiler, OP_JUMP_IF_FALSE);

    // Compile the then branch.
    emitByte(compiler, OP_POP); // Condition.
    statement(compiler);

    // Jump over the else branch when the if branch is taken.
    int endJump = emitJump(compiler, OP_JUMP);

    // Compile the else branch.
    patchJump(compiler, elseJump);
    emitByte(compiler, OP_POP); // Condition.

    if (match(compiler, TOKEN_ELSE)) statement(compiler);

    patchJump(compiler, endJump);
}

static void withStatement(Compiler *compiler) {
    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after 'with'.");
    expression(compiler);
    consume(compiler, TOKEN_COMMA, "Expect comma");
    expression(compiler);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after 'with'.");

    beginScope(compiler);

    Local *local = &compiler->locals[compiler->localCount++];
    local->depth = compiler->scopeDepth;
    local->isUpvalue = false;
    local->name = syntheticToken("file");
    local->constant = true;

    emitByte(compiler, OP_OPEN_FILE);
    statement(compiler);
    emitByte(compiler, OP_CLOSE_FILE);
    endScope(compiler);
}

static void returnStatement(Compiler *compiler) {
    if (compiler->type == TYPE_TOP_LEVEL) {
        error(compiler->parser, "Cannot return from top-level code.");
    }

    if (match(compiler, TOKEN_SEMICOLON)) {
        emitReturn(compiler);
    } else {
        if (compiler->type == TYPE_INITIALIZER) {
            error(compiler->parser, "Cannot return a value from an initializer.");
        }

        expression(compiler);
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after return value.");
        emitByte(compiler, OP_RETURN);
    }
}

static void importStatement(Compiler *compiler) {
    consume(compiler, TOKEN_STRING, "Expect string after import.");

    int importConstant = makeConstant(compiler, OBJ_VAL(copyString(
            compiler->parser->vm,
            compiler->parser->previous.start + 1,
            compiler->parser->previous.length - 2)));

    emitBytes(compiler, OP_IMPORT, importConstant);

    if (match(compiler, TOKEN_AS)) {
        uint8_t importName = parseVariable(compiler, "Expect import alias.", false);
        emitByte(compiler, OP_IMPORT_VARIABLE);
        defineVariable(compiler, importName, false);
    }

    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after import.");
    emitByte(compiler, OP_IMPORT_END);
}

static void whileStatement(Compiler *compiler) {
    Loop loop;
    loop.start = currentChunk(compiler)->count;
    loop.scopeDepth = compiler->scopeDepth;
    loop.enclosing = compiler->loop;
    compiler->loop = &loop;

    if (check(compiler, TOKEN_LEFT_BRACE)) {
        emitByte(compiler, OP_TRUE);
    } else {
        consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
        expression(compiler);
        consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    }

    // Jump out of the loop if the condition is false.
    compiler->loop->end = emitJump(compiler, OP_JUMP_IF_FALSE);

    // Compile the body.
    emitByte(compiler, OP_POP); // Condition.
    compiler->loop->body = compiler->function->chunk.count;
    statement(compiler);

    // Loop back to the start.
    emitLoop(compiler, loop.start);
    endLoop(compiler);
}

static void synchronize(Parser *parser) {
    parser->panicMode = false;

    while (parser->current.type != TOKEN_EOF) {
        if (parser->previous.type == TOKEN_SEMICOLON) return;

        switch (parser->current.type) {
            case TOKEN_CLASS:
            case TOKEN_TRAIT:
            case TOKEN_DEF:
            case TOKEN_STATIC:
            case TOKEN_VAR:
            case TOKEN_CONST:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_BREAK:
            case TOKEN_RETURN:
            case TOKEN_IMPORT:
            case TOKEN_WITH:
                return;

            default:
                // Do nothing.
                ;
        }

        advance(parser);
    }
}

static void declaration(Compiler *compiler) {
    if (match(compiler, TOKEN_CLASS)) {
        classDeclaration(compiler);
    } else if (match(compiler, TOKEN_TRAIT)) {
        traitDeclaration(compiler);
    } else if (match(compiler, TOKEN_DEF)) {
        funDeclaration(compiler);
    } else if (match(compiler, TOKEN_VAR)) {
        varDeclaration(compiler, false);
    } else if (match(compiler, TOKEN_CONST)) {
        varDeclaration(compiler, true);
    } else {
        statement(compiler);
    }

    if (compiler->parser->panicMode) synchronize(compiler->parser);
}

static void statement(Compiler *compiler) {
    if (match(compiler, TOKEN_FOR)) {
        forStatement(compiler);
    } else if (match(compiler, TOKEN_IF)) {
        ifStatement(compiler);
    } else if (match(compiler, TOKEN_RETURN)) {
        returnStatement(compiler);
    } else if (match(compiler, TOKEN_WITH)) {
        withStatement(compiler);
    } else if (match(compiler, TOKEN_IMPORT)) {
        importStatement(compiler);
    } else if (match(compiler, TOKEN_BREAK)) {
        breakStatement(compiler);
    } else if (match(compiler, TOKEN_WHILE)) {
        whileStatement(compiler);
    } else if (match(compiler, TOKEN_LEFT_BRACE)) {
        Parser *parser = compiler->parser;
        Token previous = parser->previous;
        Token curtok = parser->current;

        // Advance the parser to the next token
        advance(parser);

        if (check(compiler, TOKEN_RIGHT_BRACE)) {
            if (check(compiler, TOKEN_SEMICOLON)) {
                backTrack();
                backTrack();
                parser->current = previous;
                expressionStatement(compiler);
                return;
            }
        }

        if (check(compiler, TOKEN_COLON)) {
            for (int i = 0; i < parser->current.length + parser->previous.length; ++i) {
                backTrack();
            }

            parser->current = previous;
            expressionStatement(compiler);
            return;
        }

        // Reset the scanner to the previous position
        for (int i = 0; i < parser->current.length; ++i) {
            backTrack();
        }

        // Reset the parser
        parser->previous = previous;
        parser->current = curtok;

        beginScope(compiler);
        block(compiler);
        endScope(compiler);
    } else if (match(compiler, TOKEN_CONTINUE)) {
        continueStatement(compiler);
    } else {
        expressionStatement(compiler);
    }
}

ObjFunction *compile(VM *vm, ObjModule *module, const char *source) {
    Parser parser;
    parser.vm = vm;
    parser.hadError = false;
    parser.panicMode = false;
    parser.module = module;

    initScanner(source);
    Compiler compiler;
    initCompiler(&parser, &compiler, NULL, TYPE_TOP_LEVEL);

    advance(compiler.parser);

    if (!match(&compiler, TOKEN_EOF)) {
        do {
            declaration(&compiler);
        } while (!match(&compiler, TOKEN_EOF));
    }

    ObjFunction *function = endCompiler(&compiler);

    // If there was a compile error, the code is not valid, so don't
    // create a function.
    return parser.hadError ? NULL : function;
}

void grayCompilerRoots(VM *vm) {
    Compiler *compiler = vm->compiler;

    while (compiler != NULL) {
        grayObject(vm, (Obj *) compiler->function);
        grayTable(vm, &compiler->stringConstants);
        compiler = compiler->enclosing;
    }
}

    /* 7: vm.c */


static void resetStack(VM *vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
    vm->compiler = NULL;
}

void runtimeError(VM *vm, const char *format, ...) {
    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm->frames[i];

        ObjFunction *function = frame->closure->function;

        // -1 because the IP is sitting on the next instruction to be
        // executed.
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ",
                function->chunk.lines[instruction]);

        if (function->name == NULL) {
            fprintf(stderr, "%s: ", vm->scriptNames[vm->scriptNameCount]);
            i = -1;
        } else {
            fprintf(stderr, "%s(): ", function->name->chars);
        }

        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        fputs("\n", stderr);
        va_end(args);
    }

    resetStack(vm);
}

void setupFilenameStack(VM *vm, const char *scriptName) {
    vm->scriptNameCapacity = 8;
    vm->scriptNames = ALLOCATE(vm, const char*, vm->scriptNameCapacity);
    vm->scriptNameCount = 0;
    vm->scriptNames[vm->scriptNameCount] = scriptName;
}

void setcurrentFile(VM *vm, const char *scriptname, int len) {
    ObjString *name = copyString(vm, scriptname, len);
    push(vm, OBJ_VAL(name));
    ObjString *__file__ = copyString(vm, "__file__", 8);
    push(vm, OBJ_VAL(__file__));
    tableSet(vm, &vm->globals, __file__, OBJ_VAL(name));
    pop(vm);
    pop(vm);
}

VM *initVM(bool repl, const char *scriptName, int argc, const char *argv[]) {
    VM *vm = malloc(sizeof(*vm));

    if (vm == NULL) {
        printf("Unable to allocate memory\n");
        exit(71);
    }

    memset(vm, '\0', sizeof(VM));

    resetStack(vm);
    vm->objects = NULL;
    vm->repl = repl;
    vm->frameCapacity = 4;
    vm->frames = NULL;
    vm->initString = NULL;
    vm->replVar = NULL;
    vm->bytesAllocated = 0;
    vm->nextGC = 1024 * 1024;
    vm->grayCount = 0;
    vm->grayCapacity = 0;
    vm->grayStack = NULL;
    vm->lastModule = NULL;
    initTable(&vm->modules);
    initTable(&vm->globals);
    initTable(&vm->constants);
    initTable(&vm->strings);
    initTable(&vm->imports);

    initTable(&vm->numberMethods);
    initTable(&vm->boolMethods);
    initTable(&vm->nilMethods);
    initTable(&vm->stringMethods);
    initTable(&vm->listMethods);
    initTable(&vm->dictMethods);
    initTable(&vm->setMethods);
    initTable(&vm->fileMethods);
    initTable(&vm->classMethods);
    initTable(&vm->instanceMethods);

    setupFilenameStack(vm, scriptName);
    if (scriptName == NULL) {
        setcurrentFile(vm, "", 0);
    } else {
        setcurrentFile(vm, scriptName, (int) strlen(scriptName));
    }

    vm->frames = ALLOCATE(vm, CallFrame, vm->frameCapacity);
    vm->initString = copyString(vm, "init", 4);
    vm->replVar = copyString(vm, "_", 1);

    // Native methods
    declareNumberMethods(vm);
    declareBoolMethods(vm);
    declareNilMethods(vm);
    declareStringMethods(vm);
    declareListMethods(vm);
    declareDictMethods(vm);
    declareSetMethods(vm);
    declareFileMethods(vm);
    declareClassMethods(vm);
    declareInstanceMethods(vm);

    // Native functions
    defineAllNatives(vm);

    // Native classes
    createMathsClass(vm);
    createEnvClass(vm);
    createSystemClass(vm, argc, argv);
    createJSONClass(vm);
    createPathClass(vm);
    createCClass(vm);
    createDatetimeClass(vm);
#ifndef DISABLE_HTTP
    createHTTPClass(vm);
#endif
    return vm;
}

void freeVM(VM *vm) {
    freeTable(vm, &vm->modules);
    freeTable(vm, &vm->globals);
    freeTable(vm, &vm->constants);
    freeTable(vm, &vm->strings);
    freeTable(vm, &vm->imports);
    freeTable(vm, &vm->numberMethods);
    freeTable(vm, &vm->boolMethods);
    freeTable(vm, &vm->nilMethods);
    freeTable(vm, &vm->stringMethods);
    freeTable(vm, &vm->listMethods);
    freeTable(vm, &vm->dictMethods);
    freeTable(vm, &vm->setMethods);
    freeTable(vm, &vm->fileMethods);
    freeTable(vm, &vm->classMethods);
    freeTable(vm, &vm->instanceMethods);
    FREE_ARRAY(vm, CallFrame, vm->frames, vm->frameCapacity);
    FREE_ARRAY(vm, const char*, vm->scriptNames, vm->scriptNameCapacity);
    vm->initString = NULL;
    vm->replVar = NULL;
    freeObjects(vm);

#if defined(DEBUG_TRACE_MEM) || defined(DEBUG_FINAL_MEM)
    printf("Total memory usage: %zu\n", vm->bytesAllocated);
#endif

    free(vm);
}

void push(VM *vm, Value value) {
    *vm->stackTop = value;
    vm->stackTop++;
}

Value pop(VM *vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

Value peek(VM *vm, int distance) {
    return vm->stackTop[-1 - distance];
}

static bool call(VM *vm, ObjClosure *closure, int argCount) {
    if (argCount < closure->function->arity || argCount > closure->function->arity + closure->function->arityOptional) {
        runtimeError(vm, "Function '%s' expected %d arguments but got %d.",
                     closure->function->name->chars,
                     closure->function->arity + closure->function->arityOptional,
                     argCount
        );

        return false;
    }
    if (vm->frameCount == vm->frameCapacity) {
        int oldCapacity = vm->frameCapacity;
        vm->frameCapacity = GROW_CAPACITY(vm->frameCapacity);
        vm->frames = GROW_ARRAY(vm, vm->frames, CallFrame,
                                   oldCapacity, vm->frameCapacity);
    }

    CallFrame *frame = &vm->frames[vm->frameCount++];
    frame->closure = closure;
    frame->ip = closure->function->chunk.code;

    frame->slots = vm->stackTop - argCount - 1;

    return true;
}

static bool callValue(VM *vm, Value callee, int argCount) {
    if (IS_OBJ(callee)) {
        switch (OBJ_TYPE(callee)) {
            case OBJ_BOUND_METHOD: {
                ObjBoundMethod *bound = AS_BOUND_METHOD(callee);

                // Replace the bound method with the receiver so it's in the
                // right slot when the method is called.
                vm->stackTop[-argCount - 1] = bound->receiver;
                return call(vm, bound->method, argCount);
            }

            case OBJ_CLASS: {
                ObjClass *klass = AS_CLASS(callee);

                // Create the instance.
                vm->stackTop[-argCount - 1] = OBJ_VAL(newInstance(vm, klass));

                // Call the initializer, if there is one.
                Value initializer;
                if (tableGet(&klass->methods, vm->initString, &initializer)) {
                    return call(vm, AS_CLOSURE(initializer), argCount);
                } else if (argCount != 0) {
                    runtimeError(vm, "Expected 0 arguments but got %d.", argCount);
                    return false;
                }

                return true;
            }

            case OBJ_CLOSURE:
                return call(vm, AS_CLOSURE(callee), argCount);

            case OBJ_NATIVE: {
                NativeFn native = AS_NATIVE(callee);
                Value result = native(vm, argCount, vm->stackTop - argCount);

                if (IS_EMPTY(result))
                    return false;

                vm->stackTop -= argCount + 1;
                push(vm, result);
                return true;
            }

            default:
                // Do nothing.
                break;
        }
    }

    runtimeError(vm, "Can only call functions and classes.");
    return false;
}

static bool callNativeMethod(VM *vm, Value method, int argCount) {
    NativeFn native = AS_NATIVE(method);

    Value result = native(vm, argCount, vm->stackTop - argCount - 1);

    if (IS_EMPTY(result))
        return false;

    vm->stackTop -= argCount + 1;
    push(vm, result);
    return true;
}

static bool invokeFromClass(VM *vm, ObjClass *klass, ObjString *name,
                            int argCount) {
    // Look for the method.
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError(vm, "Undefined property '%s'.", name->chars);
        return false;
    }

    return call(vm, AS_CLOSURE(method), argCount);
}

static bool invoke(VM *vm, ObjString *name, int argCount) {
    Value receiver = peek(vm, argCount);

    if (!IS_OBJ(receiver)) {
        if (IS_NUMBER(receiver)) {
            Value value;
            if (tableGet(&vm->numberMethods, name, &value)) {
                return callNativeMethod(vm, value, argCount);
            }

            runtimeError(vm, "Number has no method %s().", name->chars);
            return false;
        } else if (IS_BOOL(receiver)) {
            Value value;
            if (tableGet(&vm->boolMethods, name, &value)) {
                return callNativeMethod(vm, value, argCount);
            }

            runtimeError(vm, "Bool has no method %s().", name->chars);
            return false;
        } else if (IS_NIL(receiver)) {
            Value value;
            if (tableGet(&vm->nilMethods, name, &value)) {
                return callNativeMethod(vm, value, argCount);
            }

            runtimeError(vm, "Nil has no method %s().", name->chars);
            return false;
        }
    } else {
        switch (getObjType(receiver)) {
            case OBJ_MODULE: {
                ObjModule *module = AS_MODULE(receiver);

                Value value;
                if (tableGet(&module->values, name, &value)) {
                    vm->stackTop[-argCount - 1] = value;
                    return callValue(vm, value, argCount);
                }
                break;
            }
            case OBJ_NATIVE_CLASS: {
                ObjClassNative *instance = AS_CLASS_NATIVE(receiver);
                Value function;
                if (!tableGet(&instance->methods, name, &function)) {
                    runtimeError(vm, "Undefined property '%s'.", name->chars);
                    return false;
                }

                return callValue(vm, function, argCount);
            }

            case OBJ_CLASS: {
                ObjClass *instance = AS_CLASS(receiver);
                Value method;
                if (tableGet(&instance->methods, name, &method)) {
                    if (!AS_CLOSURE(method)->function->staticMethod) {
                        if (tableGet(&vm->classMethods, name, &method)) {
                            return callNativeMethod(vm, method, argCount);
                        }

                        runtimeError(vm, "'%s' is not static. Only static methods can be invoked directly from a class.",
                                     name->chars);
                        return false;
                    }

                    return callValue(vm, method, argCount);
                }

                if (tableGet(&vm->classMethods, name, &method)) {
                    return callNativeMethod(vm, method, argCount);
                }

                runtimeError(vm, "Undefined property '%s'.", name->chars);
                return false;
            }

            case OBJ_INSTANCE: {
                ObjInstance *instance = AS_INSTANCE(receiver);

                Value value;
                // First look for a field which may shadow a method.
                if (tableGet(&instance->fields, name, &value)) {
                    vm->stackTop[-argCount - 1] = value;
                    return callValue(vm, value, argCount);
                }

                // Look for the method.
                if (tableGet(&instance->klass->methods, name, &value)) {
                    return call(vm, AS_CLOSURE(value), argCount);
                }

                // Check for instance methods.
                if (tableGet(&vm->instanceMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "Undefined property '%s'.", name->chars);
                return false;
            }

            case OBJ_STRING: {
                Value value;
                if (tableGet(&vm->stringMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "String has no method %s().", name->chars);
                return false;
            }

            case OBJ_LIST: {
                Value value;
                if (tableGet(&vm->listMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "List has no method %s().", name->chars);
                return false;
            }

            case OBJ_DICT: {
                Value value;
                if (tableGet(&vm->dictMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "Dict has no method %s().", name->chars);
                return false;
            }

            case OBJ_SET: {
                Value value;
                if (tableGet(&vm->setMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "Set has no method %s().", name->chars);
                return false;
            }

            case OBJ_FILE: {
                Value value;
                if (tableGet(&vm->fileMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "File has no method %s().", name->chars);
                return false;
            }

            default:
                break;
        }
    }

    runtimeError(vm, "Only instances have methods.");
    return false;
}

static bool bindMethod(VM *vm, ObjClass *klass, ObjString *name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError(vm, "Undefined property '%s'.", name->chars);
        return false;
    }

    ObjBoundMethod *bound = newBoundMethod(vm, peek(vm, 0), AS_CLOSURE(method));
    pop(vm); // Instance.
    push(vm, OBJ_VAL(bound));
    return true;
}

// Captures the local variable [local] into an [Upvalue]. If that local
// is already in an upvalue, the existing one is used. (This is
// important to ensure that multiple closures closing over the same
// variable actually see the same variable.) Otherwise, it creates a
// new open upvalue and adds it to the VM's list of upvalues.
static ObjUpvalue *captureUpvalue(VM *vm, Value *local) {
    // If there are no open upvalues at all, we must need a new one.
    if (vm->openUpvalues == NULL) {
        vm->openUpvalues = newUpvalue(vm, local);
        return vm->openUpvalues;
    }

    ObjUpvalue *prevUpvalue = NULL;
    ObjUpvalue *upvalue = vm->openUpvalues;

    // Walk towards the bottom of the stack until we find a previously
    // existing upvalue or reach where it should be.
    while (upvalue != NULL && upvalue->value > local) {
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    // If we found it, reuse it.
    if (upvalue != NULL && upvalue->value == local) return upvalue;

    // We walked past the local on the stack, so there must not be an
    // upvalue for it already. Make a new one and link it in in the right
    // place to keep the list sorted.
    ObjUpvalue *createdUpvalue = newUpvalue(vm, local);
    createdUpvalue->next = upvalue;

    if (prevUpvalue == NULL) {
        // The new one is the first one in the list.
        vm->openUpvalues = createdUpvalue;
    } else {
        prevUpvalue->next = createdUpvalue;
    }

    return createdUpvalue;
}

static void closeUpvalues(VM *vm, Value *last) {
    while (vm->openUpvalues != NULL &&
           vm->openUpvalues->value >= last) {
        ObjUpvalue *upvalue = vm->openUpvalues;

        // Move the value into the upvalue itself and point the upvalue to
        // it.
        upvalue->closed = *upvalue->value;
        upvalue->value = &upvalue->closed;

        // Pop it off the open upvalue list.
        vm->openUpvalues = upvalue->next;
    }
}

static void defineMethod(VM *vm, ObjString *name) {
    Value method = peek(vm, 0);
    ObjClass *klass = AS_CLASS(peek(vm, 1));
    tableSet(vm, &klass->methods, name, method);
    pop(vm);
}

static void defineTraitMethod(VM *vm, ObjString *name) {
    Value method = peek(vm, 0);
    ObjTrait *trait = AS_TRAIT(peek(vm, 1));
    tableSet(vm, &trait->methods, name, method);
    pop(vm);
}

static void createClass(VM *vm, ObjString *name, ObjClass *superclass) {
    ObjClass *klass = newClass(vm, name, superclass);
    push(vm, OBJ_VAL(klass));

    // Inherit methods.
    if (superclass != NULL) {
        tableAddAll(vm, &superclass->methods, &klass->methods);
    }
}

bool isFalsey(Value value) {
    return IS_NIL(value) ||
           (IS_BOOL(value) && !AS_BOOL(value)) ||
           (IS_NUMBER(value) && AS_NUMBER(value) == 0) ||
           (IS_STRING(value) && AS_CSTRING(value)[0] == '\0') ||
           (IS_LIST(value) && AS_LIST(value)->values.count == 0) ||
           (IS_DICT(value) && AS_DICT(value)->count == 0) ||
           (IS_SET(value) && AS_SET(value)->count == 0);
}

static void concatenate(VM *vm) {
    ObjString *b = AS_STRING(peek(vm, 0));
    ObjString *a = AS_STRING(peek(vm, 1));

    int length = a->length + b->length;
    char *chars = ALLOCATE(vm, char, length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString *result = takeString(vm, chars, length);

    pop(vm);
    pop(vm);

    push(vm, OBJ_VAL(result));
}

static void setReplVar(VM *vm, Value value) {
    tableSet(vm, &vm->globals, vm->replVar, value);
}

static InterpretResult run(VM *vm) {

    CallFrame *frame = &vm->frames[vm->frameCount - 1];
    register uint8_t* ip = frame->ip;

    #define READ_BYTE() (*ip++)
    #define READ_SHORT() \
        (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))

    #define READ_CONSTANT() \
                (frame->closure->function->chunk.constants.values[READ_BYTE()])

    #define READ_STRING() AS_STRING(READ_CONSTANT())

    #define BINARY_OP(valueType, op, type) \
        do { \
          if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) { \
            frame->ip = ip; \
            runtimeError(vm, "Operands must be numbers."); \
            return INTERPRET_RUNTIME_ERROR; \
          } \
          \
          type b = AS_NUMBER(pop(vm)); \
          type a = AS_NUMBER(pop(vm)); \
          push(vm, valueType(a op b)); \
        } while (false)

    #ifdef COMPUTED_GOTO

    static void* dispatchTable[] = {
        #define OPCODE(name) &&op_##name,
        #include "opcodes.h"
        #undef OPCODE
    };

    #define INTERPRET_LOOP    DISPATCH();
    #define CASE_CODE(name)   op_##name

    #ifdef DEBUG_TRACE_EXECUTION
        #define DISPATCH()                                                                        \
            do                                                                                    \
            {                                                                                     \
                printf("          ");                                                             \
                for (Value *stackValue = vm->stack; stackValue < vm->stackTop; stackValue++) {    \
                    printf("[ ");                                                                 \
                    printValue(*stackValue);                                                      \
                    printf(" ]");                                                                 \
                }                                                                                 \
                printf("\n");                                                                     \
                disassembleInstruction(&frame->closure->function->chunk,                          \
                        (int) (frame->ip - frame->closure->function->chunk.code));                \
                goto *dispatchTable[instruction = READ_BYTE()];                                   \
            }                                                                                     \
            while (false)
    #else
        #define DISPATCH()                                            \
            do                                                        \
            {                                                         \
                goto *dispatchTable[instruction = READ_BYTE()];       \
            }                                                         \
            while (false)
    #endif

    #else

    #define INTERPRET_LOOP                                        \
            loop:                                                 \
                switch (instruction = READ_BYTE())

    #define DISPATCH() goto loop

    #define CASE_CODE(name) case OP_##name

    #endif

    uint8_t instruction;
    INTERPRET_LOOP
    {
        CASE_CODE(CONSTANT): {
            Value constant = READ_CONSTANT();
            push(vm, constant);
            DISPATCH();
        }

        CASE_CODE(NIL):
            push(vm, NIL_VAL);
            DISPATCH();

        CASE_CODE(EMPTY):
            push(vm, EMPTY_VAL);
            DISPATCH();

        CASE_CODE(TRUE):
            push(vm, BOOL_VAL(true));
            DISPATCH();

        CASE_CODE(FALSE):
            push(vm, BOOL_VAL(false));
            DISPATCH();

        CASE_CODE(POP_REPL): {
            Value v = pop(vm);
            if (!IS_NIL(v)) {
                setReplVar(vm, v);
                printValue(v);
                printf("\n");
            }
            DISPATCH();
        }

        CASE_CODE(POP): {
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(GET_LOCAL): {
            uint8_t slot = READ_BYTE();
            push(vm, frame->slots[slot]);
            DISPATCH();
        }

        CASE_CODE(SET_LOCAL): {
            uint8_t slot = READ_BYTE();
            frame->slots[slot] = peek(vm, 0);
            DISPATCH();
        }

        CASE_CODE(GET_GLOBAL): {
            ObjString *name = READ_STRING();
            Value value;
            if (!tableGet(&frame->closure->function->module->values, name, &value)) {
                if (!tableGet(&vm->globals, name, &value)) {
                    frame->ip = ip;
                    runtimeError(vm, "Undefined variable '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
            }
            push(vm, value);
            DISPATCH();
        }

        CASE_CODE(DEFINE_GLOBAL): {
            ObjString *name = READ_STRING();
            tableSet(vm, &frame->closure->function->module->values, name, peek(vm, 0));
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(SET_GLOBAL): {
        ObjString *name = READ_STRING();
        if (tableSet(vm, &frame->closure->function->module->values, name, peek(vm, 0))) {
            tableDelete(vm, &frame->closure->function->module->values, name);
            frame->ip = ip;
            runtimeError(vm, "Undefined variable '%s'.", name->chars);
            return INTERPRET_RUNTIME_ERROR;
        }
        DISPATCH();
    }

        CASE_CODE(DEFINE_OPTIONAL): {
            // Temp array while we shuffle the stack.
            // Can not have more than 255 args to a function, so
            // we can define this with a constant limit
            Value values[255];
            int index = 0;

            values[index] = pop(vm);

            // Pop all args and default values a function has
            while (!IS_CLOSURE(values[index])) {
                values[++index] = pop(vm);
            }

            ObjClosure *closure = AS_CLOSURE(values[index--]);
            ObjFunction *function = closure->function;

            int argCount = index - function->arityOptional + 1;

            // Push the function back onto the stack
            push(vm, OBJ_VAL(closure));

            // Push all user given options
            for (int i = 0; i < argCount; i++) {
                push(vm, values[index - i]);
            }

            // Calculate how many "default" values are required
            int remaining = function->arity + function->arityOptional - argCount;

            // Push any "default" values back onto the stack
            for (int i = remaining; i > 0; i--) {
                push(vm, values[i - 1]);
            }

            DISPATCH();
        }

        CASE_CODE(GET_UPVALUE): {
            uint8_t slot = READ_BYTE();
            push(vm, *frame->closure->upvalues[slot]->value);
            DISPATCH();
        }

        CASE_CODE(SET_UPVALUE): {
            uint8_t slot = READ_BYTE();
            *frame->closure->upvalues[slot]->value = peek(vm, 0);
            DISPATCH();
        }

        CASE_CODE(GET_PROPERTY): {
            if (IS_INSTANCE(peek(vm, 0))) {
                ObjInstance *instance = AS_INSTANCE(peek(vm, 0));
                ObjString *name = READ_STRING();
                Value value;
                if (tableGet(&instance->fields, name, &value)) {
                    pop(vm); // Instance.
                    push(vm, value);
                    DISPATCH();
                }

                if (!bindMethod(vm, instance->klass, name)) {
                    return INTERPRET_RUNTIME_ERROR;
                }

                DISPATCH();
            } else if (IS_NATIVE_CLASS(peek(vm, 0))) {
                ObjClassNative *klass = AS_CLASS_NATIVE(peek(vm, 0));
                ObjString *name = READ_STRING();
                Value value;
                if (tableGet(&klass->properties, name, &value)) {
                    pop(vm); // Class.
                    push(vm, value);
                    DISPATCH();
                }
            } else if (IS_MODULE(peek(vm, 0))) {
                ObjModule *module = AS_MODULE(peek(vm, 0));
                ObjString *name = READ_STRING();
                Value value;
                if (tableGet(&module->values, name, &value)) {
                    pop(vm); // Module.
                    push(vm, value);
                    DISPATCH();
                }
            }

            frame->ip = ip;
            runtimeError(vm, "Only instances have properties.");
            return INTERPRET_RUNTIME_ERROR;
        }

        CASE_CODE(GET_PROPERTY_NO_POP): {
            if (!IS_INSTANCE(peek(vm, 0))) {
                frame->ip = ip;
                runtimeError(vm, "Only instances have properties.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(vm, 0));
            ObjString *name = READ_STRING();
            Value value;
            if (tableGet(&instance->fields, name, &value)) {
                push(vm, value);
                DISPATCH();
            }

            if (!bindMethod(vm, instance->klass, name)) {
                return INTERPRET_RUNTIME_ERROR;
            }

            DISPATCH();
        }

        CASE_CODE(SET_PROPERTY): {
            if (!IS_INSTANCE(peek(vm, 1))) {
                frame->ip = ip;
                runtimeError(vm, "Only instances have fields.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjInstance *instance = AS_INSTANCE(peek(vm, 1));
            tableSet(vm, &instance->fields, READ_STRING(), peek(vm, 0));
            pop(vm);
            pop(vm);
            push(vm, NIL_VAL);
            DISPATCH();
        }

        CASE_CODE(GET_SUPER): {
            ObjString *name = READ_STRING();
            ObjClass *superclass = AS_CLASS(pop(vm));
            if (!bindMethod(vm, superclass, name)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            DISPATCH();
        }

        CASE_CODE(EQUAL): {
            Value b = pop(vm);
            Value a = pop(vm);
            push(vm, BOOL_VAL(valuesEqual(a, b)));
            DISPATCH();
        }

        CASE_CODE(GREATER):
            BINARY_OP(BOOL_VAL, >, double);
            DISPATCH();

        CASE_CODE(LESS):
            BINARY_OP(BOOL_VAL, <, double);
            DISPATCH();

        CASE_CODE(ADD): {
            if (IS_STRING(peek(vm, 0)) && IS_STRING(peek(vm, 1))) {
                concatenate(vm);
            } else if (IS_NUMBER(peek(vm, 0)) && IS_NUMBER(peek(vm, 1))) {
                double b = AS_NUMBER(pop(vm));
                double a = AS_NUMBER(pop(vm));
                push(vm, NUMBER_VAL(a + b));
            } else if (IS_LIST(peek(vm, 0)) && IS_LIST(peek(vm, 1))) {
                ObjList *listOne = AS_LIST(peek(vm, 1));
                ObjList *listTwo = AS_LIST(peek(vm, 0));

                ObjList *finalList = initList(vm);
                push(vm, OBJ_VAL(finalList));

                for (int i = 0; i < listOne->values.count; ++i) {
                    writeValueArray(vm, &finalList->values, listOne->values.values[i]);
                }

                for (int i = 0; i < listTwo->values.count; ++i) {
                    writeValueArray(vm, &finalList->values, listTwo->values.values[i]);
                }

                pop(vm);

                pop(vm);
                pop(vm);

                push(vm, OBJ_VAL(finalList));
            } else {
                frame->ip = ip;
                runtimeError(vm, "Unsupported operand types.");
                return INTERPRET_RUNTIME_ERROR;
            }
            DISPATCH();
        }

        CASE_CODE(INCREMENT): {
            if (!IS_NUMBER(peek(vm, 0))) {
                frame->ip = ip;
                runtimeError(vm, "Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }

            push(vm, NUMBER_VAL(AS_NUMBER(pop(vm)) + 1));
            DISPATCH();
        }

        CASE_CODE(DECREMENT): {
            if (!IS_NUMBER(peek(vm, 0))) {
                frame->ip = ip;
                runtimeError(vm, "Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }

            push(vm, NUMBER_VAL(AS_NUMBER(pop(vm)) - 1));
            DISPATCH();
        }

        CASE_CODE(MULTIPLY):
            BINARY_OP(NUMBER_VAL, *, double);
            DISPATCH();

        CASE_CODE(DIVIDE):
            BINARY_OP(NUMBER_VAL, /, double);
            DISPATCH();

        CASE_CODE(POW): {
            if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) {
                frame->ip = ip;
                runtimeError(vm, "Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }

            double b = AS_NUMBER(pop(vm));
            double a = AS_NUMBER(pop(vm));

            push(vm, NUMBER_VAL(powf(a, b)));
            DISPATCH();
        }

        CASE_CODE(MOD): {
            if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) {
                frame->ip = ip;
                runtimeError(vm, "Operands must be numbers.");
                return INTERPRET_RUNTIME_ERROR;
            }

            double b = AS_NUMBER(pop(vm));
            double a = AS_NUMBER(pop(vm));

            push(vm, NUMBER_VAL(fmod(a, b)));
            DISPATCH();
        }

        CASE_CODE(BITWISE_AND):
            BINARY_OP(NUMBER_VAL, &, int);
            DISPATCH();

        CASE_CODE(BITWISE_XOR):
            BINARY_OP(NUMBER_VAL, ^, int);
            DISPATCH();

        CASE_CODE(BITWISE_OR):
            BINARY_OP(NUMBER_VAL, |, int);
            DISPATCH();

        CASE_CODE(NOT):
            push(vm, BOOL_VAL(isFalsey(pop(vm))));
            DISPATCH();

        CASE_CODE(NEGATE):
            if (!IS_NUMBER(peek(vm, 0))) {
                frame->ip = ip;
                runtimeError(vm, "Operand must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }

            push(vm, NUMBER_VAL(-AS_NUMBER(pop(vm))));
            DISPATCH();

        CASE_CODE(JUMP): {
            uint16_t offset = READ_SHORT();
            ip += offset;
            DISPATCH();
        }

        CASE_CODE(JUMP_IF_FALSE): {
            uint16_t offset = READ_SHORT();
            if (isFalsey(peek(vm, 0))) ip += offset;
            DISPATCH();
        }

        CASE_CODE(LOOP): {
            uint16_t offset = READ_SHORT();
            ip -= offset;
            DISPATCH();
        }

        CASE_CODE(BREAK): {
            DISPATCH();
        }

        CASE_CODE(IMPORT): {
            ObjString *fileName = READ_STRING();
            Value moduleVal;

            // If we have imported this file already, skip.
            if (tableGet(&vm->modules, fileName, &moduleVal)) {
                ++vm->scriptNameCount;
                vm->lastModule = AS_MODULE(moduleVal);
                push(vm, OBJ_VAL(vm->lastModule));
                DISPATCH();
            }

            char *s = readFile(fileName->chars);

            if (vm->scriptNameCapacity < vm->scriptNameCount + 2) {
                int oldCapacity = vm->scriptNameCapacity;
                vm->scriptNameCapacity = GROW_CAPACITY(oldCapacity);
                vm->scriptNames = GROW_ARRAY(vm, vm->scriptNames, const char*,
                                           oldCapacity, vm->scriptNameCapacity);
            }

            vm->scriptNames[++vm->scriptNameCount] = fileName->chars;
            setcurrentFile(vm, fileName->chars, fileName->length);

            ObjModule *module = newModule(vm, fileName);
            vm->lastModule = module;

            push(vm, OBJ_VAL(module));
            ObjFunction *function = compile(vm, module, s);
            pop(vm);
            free(s);

            if (function == NULL) return INTERPRET_COMPILE_ERROR;
            push(vm, OBJ_VAL(function));
            ObjClosure *closure = newClosure(vm, function);
            pop(vm);

            frame->ip = ip;
            call(vm, closure, 0);
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;

            DISPATCH();
        }

        CASE_CODE(IMPORT_VARIABLE): {
            push(vm, OBJ_VAL( vm->lastModule));
            DISPATCH();
        }

        CASE_CODE(IMPORT_END): {
            vm->scriptNameCount--;
            if (vm->scriptNameCount >= 0) {
                setcurrentFile(vm, vm->scriptNames[vm->scriptNameCount],
                     (int) strlen(vm->scriptNames[vm->scriptNameCount]));
            } else {
                setcurrentFile(vm, "", 0);
            }
            DISPATCH();
        }

        CASE_CODE(NEW_LIST): {
            ObjList *list = initList(vm);
            push(vm, OBJ_VAL(list));
            DISPATCH();
        }

        CASE_CODE(ADD_LIST): {
            Value addValue = peek(vm, 0);
            Value listValue = peek(vm, 1);

            ObjList *list = AS_LIST(listValue);
            writeValueArray(vm, &list->values, addValue);

            pop(vm);
            pop(vm);

            push(vm, OBJ_VAL(list));
            DISPATCH();
        }

        CASE_CODE(NEW_DICT): {
            ObjDict *dict = initDict(vm);
            push(vm, OBJ_VAL(dict));
            DISPATCH();
        }

        CASE_CODE(ADD_DICT): {
            Value value = peek(vm, 0);
            Value key = peek(vm, 1);

            if (!isValidKey(key)) {
                frame->ip = ip;
                runtimeError(vm, "Dictionary key must be an immutable type.");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjDict *dict = AS_DICT(peek(vm, 2));
            dictSet(vm, dict, key, value);

            pop(vm);
            pop(vm);
            pop(vm);

            push(vm, OBJ_VAL(dict));
            DISPATCH();
        }

        CASE_CODE(SUBSCRIPT): {
            Value indexValue = pop(vm);
            Value subscriptValue = pop(vm);

            if (!IS_OBJ(subscriptValue)) {
                frame->ip = ip;
                runtimeError(vm, "Can only subscript on lists, strings or dictionaries.");
                return INTERPRET_RUNTIME_ERROR;
            }

            switch (getObjType(subscriptValue)) {
                case OBJ_LIST: {
                    if (!IS_NUMBER(indexValue)) {
                        frame->ip = ip;
                        runtimeError(vm, "List index must be a number.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjList *list = AS_LIST(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    // Allow negative indexes
                    if (index < 0)
                        index = list->values.count + index;

                    if (index >= 0 && index < list->values.count) {
                        push(vm, list->values.values[index]);
                        DISPATCH();
                    }

                    frame->ip = ip;
                    runtimeError(vm, "List index out of bounds.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                case OBJ_STRING: {
                    ObjString *string = AS_STRING(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    // Allow negative indexes
                    if (index < 0)
                        index = string->length + index;

                    if (index >= 0 && index < string->length) {
                        push(vm, OBJ_VAL(copyString(vm, &string->chars[index], 1)));
                        DISPATCH();
                    }

                    frame->ip = ip;
                    runtimeError(vm, "String index out of bounds.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                case OBJ_DICT: {
                    ObjDict *dict = AS_DICT(subscriptValue);
                    if (!isValidKey(indexValue)) {
                        frame->ip = ip;
                        runtimeError(vm, "Dictionary key must be an immutable type.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    Value v;
                    if (dictGet(dict, indexValue, &v)) {
                        push(vm, v);
                    } else {
                        push(vm, NIL_VAL);
                    }

                    DISPATCH();
                }

                default: {
                    frame->ip = ip;
                    runtimeError(vm, "Can only subscript on lists, strings or dictionaries.");
                    return INTERPRET_RUNTIME_ERROR;
                }
            }
        }

        CASE_CODE(SUBSCRIPT_ASSIGN): {
            // We are free to pop here as this is *not* adding a new entry, but simply replacing
            // An old value, so a GC should never be triggered.
            Value assignValue = pop(vm);
            Value indexValue = pop(vm);
            Value subscriptValue = pop(vm);

            if (!IS_OBJ(subscriptValue)) {
                frame->ip = ip;
                runtimeError(vm, "Can only subscript on lists, strings or dictionaries.");
                return INTERPRET_RUNTIME_ERROR;
            }

            switch (getObjType(subscriptValue)) {
                case OBJ_LIST: {
                    if (!IS_NUMBER(indexValue)) {
                        frame->ip = ip;
                        runtimeError(vm, "List index must be a number.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjList *list = AS_LIST(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    if (index < 0)
                        index = list->values.count + index;

                    if (index >= 0 && index < list->values.count) {
                        list->values.values[index] = assignValue;
                        push(vm, NIL_VAL);
                        DISPATCH();
                    }

                    frame->ip = ip;
                    runtimeError(vm, "List index out of bounds.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                case OBJ_DICT: {
                    ObjDict *dict = AS_DICT(subscriptValue);
                    if (!isValidKey(indexValue)) {
                        frame->ip = ip;
                        runtimeError(vm, "Dictionary key must be an immutable type.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    dictSet(vm, dict, indexValue, assignValue);

                    push(vm, NIL_VAL);
                    DISPATCH();
                }

                default: {
                    frame->ip = ip;
                    runtimeError(vm, "Only lists and dictionaries support subscript assignment.");
                    return INTERPRET_RUNTIME_ERROR;
                }
            }
        }

        CASE_CODE(SLICE): {
            Value sliceEndIndex = peek(vm, 0);
            Value sliceStartIndex = peek(vm, 1);
            Value objectValue = peek(vm, 2);

            if (!IS_OBJ(objectValue)) {
                frame->ip = ip;
                runtimeError(vm, "Can only slice on lists and strings.");
                return INTERPRET_RUNTIME_ERROR;
            }

            if ((!IS_NUMBER(sliceStartIndex) && !IS_EMPTY(sliceStartIndex)) || (!IS_NUMBER(sliceEndIndex) && !IS_EMPTY(sliceEndIndex))) {
                frame->ip = ip;
                runtimeError(vm, "Slice index must be a number.");
                return INTERPRET_RUNTIME_ERROR;
            }

            int indexStart;
            int indexEnd;
            Value returnVal;

            if (IS_EMPTY(sliceStartIndex)) {
                indexStart = 0;
            } else {
                indexStart = AS_NUMBER(sliceStartIndex);

                if (indexStart < 0) {
                    indexStart = 0;
                }
            }

            switch (getObjType(objectValue)) {
                case OBJ_LIST: {
                    ObjList *newList = initList(vm);
                    push(vm, OBJ_VAL(newList));
                    ObjList *list = AS_LIST(objectValue);

                    if (IS_EMPTY(sliceEndIndex)) {
                        indexEnd = list->values.count;
                    } else {
                        indexEnd = AS_NUMBER(sliceEndIndex);

                        if (indexEnd > list->values.count) {
                            indexEnd = list->values.count;
                        }
                    }

                    for (int i = indexStart; i < indexEnd; i++) {
                        writeValueArray(vm, &newList->values, list->values.values[i]);
                    }

                    pop(vm);
                    returnVal = OBJ_VAL(newList);

                    break;
                }

                case OBJ_STRING: {
                    ObjString *string = AS_STRING(objectValue);

                    if (IS_EMPTY(sliceEndIndex)) {
                        indexEnd = string->length;
                    } else {
                        indexEnd = AS_NUMBER(sliceEndIndex);

                        if (indexEnd > string->length) {
                            indexEnd = string->length;
                        }
                    }

                    // Ensure the start index is below the end index
                    if (indexStart > indexEnd) {
                        returnVal = OBJ_VAL(copyString(vm, "", 0));
                    } else {
                        returnVal = OBJ_VAL(copyString(vm, string->chars + indexStart, indexEnd - indexStart));
                    }
                    break;
                }

                default: {
                    frame->ip = ip;
                    runtimeError(vm, "Can only slice on lists and strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
            }

            pop(vm);
            pop(vm);
            pop(vm);

            push(vm, returnVal);
            DISPATCH();
        }

        CASE_CODE(PUSH): {
            Value value = peek(vm, 0);
            Value indexValue = peek(vm, 1);
            Value subscriptValue = peek(vm, 2);

            if (!IS_OBJ(subscriptValue)) {
                frame->ip = ip;
                runtimeError(vm, "Can only subscript on lists, strings or dictionaries.");
                return INTERPRET_RUNTIME_ERROR;
            }

            switch (getObjType(subscriptValue)) {
                case OBJ_LIST: {
                    if (!IS_NUMBER(indexValue)) {
                        frame->ip = ip;
                        runtimeError(vm, "List index must be a number.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    ObjList *list = AS_LIST(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    // Allow negative indexes
                    if (index < 0)
                        index = list->values.count + index;

                    if (index >= 0 && index < list->values.count) {
                        vm->stackTop[-1] = list->values.values[index];
                        push(vm, value);
                        DISPATCH();
                    }

                    frame->ip = ip;
                    runtimeError(vm, "List index out of bounds.");
                    return INTERPRET_RUNTIME_ERROR;
                }

                case OBJ_DICT: {
                    ObjDict *dict = AS_DICT(subscriptValue);
                    if (!isValidKey(indexValue)) {
                        frame->ip = ip;
                        runtimeError(vm, "Dictionary key must be an immutable type.");
                        return INTERPRET_RUNTIME_ERROR;
                    }

                    Value dictValue;
                    if (!dictGet(dict, indexValue, &dictValue)) {
                        dictValue = NIL_VAL;
                    }

                    vm->stackTop[-1] = dictValue;
                    push(vm, value);

                    DISPATCH();
                }

                default: {
                    frame->ip = ip;
                    runtimeError(vm, "Only lists and dictionaries support subscript assignment.");
                    return INTERPRET_RUNTIME_ERROR;
                }
            }
            DISPATCH();
        }

        CASE_CODE(CALL): {
            int argCount = READ_BYTE();
            frame->ip = ip;
            if (!callValue(vm, peek(vm, argCount), argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;
            DISPATCH();
        }

        CASE_CODE(INVOKE): {
            int argCount = READ_BYTE();
            ObjString *method = READ_STRING();
            frame->ip = ip;
            if (!invoke(vm, method, argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;
            DISPATCH();
        }

        CASE_CODE(SUPER): {
            int argCount = READ_BYTE();
            ObjString *method = READ_STRING();
            frame->ip = ip;
            ObjClass *superclass = AS_CLASS(pop(vm));
            if (!invokeFromClass(vm, superclass, method, argCount)) {
                return INTERPRET_RUNTIME_ERROR;
            }
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;
            DISPATCH();
        }

        CASE_CODE(CLOSURE): {
            ObjFunction *function = AS_FUNCTION(READ_CONSTANT());

            // Create the closure and push it on the stack before creating
            // upvalues so that it doesn't get collected.
            ObjClosure *closure = newClosure(vm, function);
            push(vm, OBJ_VAL(closure));

            // Capture upvalues.
            for (int i = 0; i < closure->upvalueCount; i++) {
                uint8_t isLocal = READ_BYTE();
                uint8_t index = READ_BYTE();
                if (isLocal) {
                    // Make an new upvalue to close over the parent's local
                    // variable.
                    closure->upvalues[i] = captureUpvalue(vm, frame->slots + index);
                } else {
                    // Use the same upvalue as the current call frame.
                    closure->upvalues[i] = frame->closure->upvalues[index];
                }
            }

            DISPATCH();
        }

        CASE_CODE(CLOSE_UPVALUE): {
            closeUpvalues(vm, vm->stackTop - 1);
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(RETURN): {
            Value result = pop(vm);

            // Close any upvalues still in scope.
            closeUpvalues(vm, frame->slots);

            vm->frameCount--;

            if (vm->frameCount == 0) {
                pop(vm);
                return INTERPRET_OK;
            }

            vm->stackTop = frame->slots;
            push(vm, result);

            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;
            DISPATCH();
        }

        CASE_CODE(CLASS):
            createClass(vm, READ_STRING(), NULL);
            DISPATCH();

        CASE_CODE(SUBCLASS): {
            Value superclass = peek(vm, 0);
            if (!IS_CLASS(superclass)) {
                frame->ip = ip;
                runtimeError(vm, "Superclass must be a class.");
                return INTERPRET_RUNTIME_ERROR;
            }

            createClass(vm, READ_STRING(), AS_CLASS(superclass));
            DISPATCH();
        }

        CASE_CODE(TRAIT): {
            ObjString *name = READ_STRING();
            ObjTrait *trait = newTrait(vm, name);
            push(vm, OBJ_VAL(trait));
            DISPATCH();
        }

        CASE_CODE(METHOD):
            defineMethod(vm, READ_STRING());
            DISPATCH();

        CASE_CODE(TRAIT_METHOD): {
            defineTraitMethod(vm, READ_STRING());
            DISPATCH();
        }

        CASE_CODE(USE): {
            Value trait = peek(vm, 0);
            if (!IS_TRAIT(trait)) {
                frame->ip = ip;
                runtimeError(vm, "Can only 'use' with a trait");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjClass *klass = AS_CLASS(peek(vm, 1));

            tableAddAll(vm, &AS_TRAIT(trait)->methods, &klass->methods);
            pop(vm); // pop the trait

            DISPATCH();
        }

        CASE_CODE(OPEN_FILE): {
            Value openType = peek(vm, 0);
            Value fileName = peek(vm, 1);

            if (!IS_STRING(openType)) {
                frame->ip = ip;
                runtimeError(vm, "File open type must be a string");
                return INTERPRET_RUNTIME_ERROR;
            }

            if (!IS_STRING(fileName)) {
                frame->ip = ip;
                runtimeError(vm, "Filename must be a string");
                return INTERPRET_RUNTIME_ERROR;
            }

            ObjString *openTypeString = AS_STRING(openType);
            ObjString *fileNameString = AS_STRING(fileName);

            ObjFile *file = initFile(vm);
            file->file = fopen(fileNameString->chars, openTypeString->chars);
            file->path = fileNameString->chars;
            file->openType = openTypeString->chars;

            if (file->file == NULL) {
                frame->ip = ip;
                runtimeError(vm, "Unable to open file");
                return INTERPRET_RUNTIME_ERROR;
            }

            pop(vm);
            pop(vm);
            push(vm, OBJ_VAL(file));
            DISPATCH();
        }

        CASE_CODE(CLOSE_FILE): {
            ObjFile *file = AS_FILE(peek(vm, 0));
            fclose(file->file);
            DISPATCH();
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP

    return INTERPRET_RUNTIME_ERROR;

}

InterpretResult interpret(VM *vm, const char *source) {
    ObjString *name = copyString(vm, "main", 4);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    pop(vm);

    ObjFunction *function = compile(vm, module, source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
    push(vm, OBJ_VAL(function));
    ObjClosure *closure = newClosure(vm, function);
    pop(vm);
    push(vm, OBJ_VAL(closure));
    callValue(vm, OBJ_VAL(closure), 0);
    InterpretResult result = run(vm);

    return result;
}

/*** EXTENSIONS ***/

Table *vm_get_globals(VM *vm) {
    return &vm->globals;
}

size_t vm_sizeof(void) {
    return sizeof(VM);
}

ObjModule *vm_module_get(VM *vm, char *name, int len) {
    Value moduleVal;
    ObjString *string = copyString (vm, name, len);
    if (!tableGet(&vm->modules, string, &moduleVal))
        return NULL;
    return AS_MODULE(moduleVal);
}

Table *vm_get_module_table(VM *vm, char *name, int len) {
    ObjModule *module = vm_module_get(vm, name, len);
    if (NULL == module)
        return NULL;
    return &module->values;
}

Value *vm_table_get_value(VM *vm, Table *table, ObjString *obj, Value *value){
    UNUSED(vm);
    if (false == tableGet(table, obj, value))
        return NULL;
    return value;
}

/*** EXTENSIONS END ***/

    /* 8: natives.c */


// Native functions
static Value typeNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "type() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (IS_BOOL(args[0])) {
        return OBJ_VAL(copyString(vm, "bool", 4));
    } else if (IS_NIL(args[0])) {
        return OBJ_VAL(copyString(vm, "nil", 3));
    } else if (IS_NUMBER(args[0])) {
        return OBJ_VAL(copyString(vm, "number", 6));
    } else if (IS_OBJ(args[0])) {
        switch (OBJ_TYPE(args[0])) {
            case OBJ_NATIVE_CLASS:
            case OBJ_CLASS:
                return OBJ_VAL(copyString(vm, "class", 5));
            case OBJ_TRAIT:
                return OBJ_VAL(copyString(vm, "trait", 5));
            case OBJ_INSTANCE: {
                ObjString *className = AS_INSTANCE(args[0])->klass->name;
                return OBJ_VAL(copyString(vm, className->chars, className->length));
            }
            case OBJ_BOUND_METHOD:
                return OBJ_VAL(copyString(vm, "method", 6));
            case OBJ_CLOSURE:
            case OBJ_FUNCTION:
                return OBJ_VAL(copyString(vm, "function", 8));
            case OBJ_STRING:
                return OBJ_VAL(copyString(vm, "string", 6));
            case OBJ_LIST:
                return OBJ_VAL(copyString(vm, "list", 4));
            case OBJ_DICT:
                return OBJ_VAL(copyString(vm, "dict", 4));
            case OBJ_SET:
                return OBJ_VAL(copyString(vm, "set", 3));
            case OBJ_NATIVE:
                return OBJ_VAL(copyString(vm, "native", 6));
            case OBJ_FILE:
                return OBJ_VAL(copyString(vm, "file", 4));
            default:
                break;
        }
    }

    return OBJ_VAL(copyString(vm, "Unknown Type", 12));
}

static Value setNative(VM *vm, int argCount, Value *args) {
    ObjSet *set = initSet(vm);
    push(vm, OBJ_VAL(set));

    for (int i = 0; i < argCount; i++) {
        setInsert(vm, set, args[i]);
    }
    pop(vm);

    return OBJ_VAL(set);
}

static Value inputNative(VM *vm, int argCount, Value *args) {
    if (argCount > 1) {
        runtimeError(vm, "input() takes either 0 or 1 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (argCount != 0) {
        Value prompt = args[0];
        if (!IS_STRING(prompt)) {
            runtimeError(vm, "input() only takes a string argument");
            return EMPTY_VAL;
        }

        printf("%s", AS_CSTRING(prompt));
    }

    uint64_t currentSize = 128;
    char *line = malloc(currentSize);

    if (line == NULL) {
        runtimeError(vm, "Memory error on input()!");
        return EMPTY_VAL;
    }

    int c = EOF;
    uint64_t i = 0;
    while ((c = getchar()) != '\n' && c != EOF) {
        line[i++] = (char) c;

        if (i + 1 == currentSize) {
            currentSize = GROW_CAPACITY(currentSize);
            line = realloc(line, currentSize);

            if (line == NULL) {
                printf("Unable to allocate memory\n");
                exit(71);
            }
        }
    }

    line[i] = '\0';

    Value l = OBJ_VAL(copyString(vm, line, strlen(line)));
    free(line);
    return l;
}

static Value printNative(VM *vm, int argCount, Value *args) {
    UNUSED(vm);

    if (argCount == 0) {
        printf("\n");
        return NIL_VAL;
    }

    for (int i = 0; i < argCount; ++i) {
        printValue(args[i]);
        printf("\n");
    }

    return NIL_VAL;
}

static Value assertNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "assert() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (isFalsey(args[0])) {
        runtimeError(vm, "assert() was false!");
        return EMPTY_VAL;
    }

    return NIL_VAL;
}

static Value isDefinedNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "isDefined() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "isDefined() only takes a string as an argument");
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);

    Value value;
    if (tableGet(&vm->globals, string, &value))
       return TRUE_VAL;

    return FALSE_VAL;
}

// End of natives

void defineAllNatives(VM *vm) {
    char *nativeNames[] = {
            "input",
            "type",
            "set",
            "print",
            "assert",
            "isDefined"
    };

    NativeFn nativeFunctions[] = {
            inputNative,
            typeNative,
            setNative,
            printNative,
            assertNative,
            isDefinedNative
    };


    for (uint8_t i = 0; i < sizeof(nativeNames) / sizeof(nativeNames[0]); ++i) {
        defineNative(vm, &vm->globals, nativeNames[i], nativeFunctions[i]);
    }
}

    /* 9: memory.c */


#ifdef DEBUG_TRACE_GC
#endif

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(VM *vm, void *previous, size_t oldSize, size_t newSize) {
    vm->bytesAllocated += newSize - oldSize;

#ifdef DEBUG_TRACE_MEM
    printf("Total memory usage: %zu\nNew allocation: %zu\nOld allocation: %zu\n\n", vm->bytesAllocated, newSize, oldSize);
#endif

    if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
        collectGarbage(vm);
#endif

        if (vm->bytesAllocated > vm->nextGC) {
            collectGarbage(vm);
        }
    }

    if (newSize == 0) {
        free(previous);
        return NULL;
    }

    return realloc(previous, newSize);
}

void grayObject(VM *vm, Obj *object) {
    if (object == NULL) return;

    // Don't get caught in cycle.
    if (object->isDark) return;

#ifdef DEBUG_TRACE_GC
    printf("%p gray ", (void *)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    object->isDark = true;

    if (vm->grayCapacity < vm->grayCount + 1) {
        vm->grayCapacity = GROW_CAPACITY(vm->grayCapacity);

        // Not using reallocate() here because we don't want to trigger the
        // GC inside a GC!
        vm->grayStack = realloc(vm->grayStack,
                               sizeof(Obj *) * vm->grayCapacity);
    }

    vm->grayStack[vm->grayCount++] = object;
}

void grayValue(VM *vm, Value value) {
    if (!IS_OBJ(value)) return;
    grayObject(vm, AS_OBJ(value));
}

static void grayArray(VM *vm, ValueArray *array) {
    for (int i = 0; i < array->count; i++) {
        grayValue(vm, array->values[i]);
    }
}

static void blackenObject(VM *vm, Obj *object) {
#ifdef DEBUG_TRACE_GC
    printf("%p blacken ", (void *)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_MODULE: {
            ObjModule *module = (ObjModule *) object;
            grayObject(vm, (Obj *) module->name);
            grayTable(vm, &module->values);
            break;
        }

        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *bound = (ObjBoundMethod *) object;
            grayValue(vm, bound->receiver);
            grayObject(vm, (Obj *) bound->method);
            break;
        }

        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass *) object;
            grayObject(vm, (Obj *) klass->name);
            grayObject(vm, (Obj *) klass->superclass);
            grayTable(vm, &klass->methods);
            break;
        }

        case OBJ_NATIVE_CLASS: {
            ObjClassNative *klass = (ObjClassNative *) object;
            grayObject(vm, (Obj *) klass->name);
            grayTable(vm, &klass->methods);
            grayTable(vm, &klass->properties);
            break;
        }

        case OBJ_TRAIT: {
            ObjTrait *trait = (ObjTrait *) object;
            grayObject(vm, (Obj *) trait->name);
            grayTable(vm, &trait->methods);
            break;
        }

        case OBJ_CLOSURE: {
            ObjClosure *closure = (ObjClosure *) object;
            grayObject(vm, (Obj *) closure->function);
            for (int i = 0; i < closure->upvalueCount; i++) {
                grayObject(vm, (Obj *) closure->upvalues[i]);
            }
            break;
        }

        case OBJ_FUNCTION: {
            ObjFunction *function = (ObjFunction *) object;
            grayObject(vm, (Obj *) function->name);
            grayArray(vm, &function->chunk.constants);
            break;
        }

        case OBJ_INSTANCE: {
            ObjInstance *instance = (ObjInstance *) object;
            grayObject(vm, (Obj *) instance->klass);
            grayTable(vm, &instance->fields);
            break;
        }

        case OBJ_UPVALUE:
            grayValue(vm, ((ObjUpvalue *) object)->closed);
            break;

        case OBJ_LIST: {
            ObjList *list = (ObjList *) object;
            grayArray(vm, &list->values);
            break;
        }

        case OBJ_DICT: {
            ObjDict *dict = (ObjDict *) object;
            grayDict(vm, dict);
            break;
        }

        case OBJ_SET: {
            ObjSet *set = (ObjSet *) object;
            graySet(vm, set);
            break;
        }


        case OBJ_NATIVE:
        case OBJ_STRING:
        case OBJ_FILE:
            break;
    }
}

void freeObject(VM *vm, Obj *object) {
#ifdef DEBUG_TRACE_GC
    printf("%p free type %d\n", (void*)object, object->type);
#endif

    switch (object->type) {
        case OBJ_MODULE: {
            ObjModule *module = (ObjModule *) object;
            freeTable(vm, &module->values);
            FREE(vm, ObjModule, object);
            break;
        }

        case OBJ_BOUND_METHOD: {
            FREE(vm, ObjBoundMethod, object);
            break;
        }

        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass *) object;
            freeTable(vm, &klass->methods);
            FREE(vm, ObjClass, object);
            break;
        }

        case OBJ_NATIVE_CLASS: {
            ObjClassNative *klass = (ObjClassNative *) object;
            freeTable(vm, &klass->methods);
            freeTable(vm, &klass->properties);
            FREE(vm, ObjClassNative, object);
            break;
        }

        case OBJ_TRAIT: {
            ObjTrait *trait = (ObjTrait *) object;
            freeTable(vm, &trait->methods);
            FREE(vm, ObjTrait, object);
            break;
        }

        case OBJ_CLOSURE: {
            ObjClosure *closure = (ObjClosure *) object;
            FREE_ARRAY(vm, ObjUpvalue*, closure->upvalues, closure->upvalueCount);
            FREE(vm, ObjClosure, object);
            break;
        }

        case OBJ_FUNCTION: {
            ObjFunction *function = (ObjFunction *) object;
            freeChunk(vm, &function->chunk);
            FREE(vm, ObjFunction, object);
            break;
        }

        case OBJ_INSTANCE: {
            ObjInstance *instance = (ObjInstance *) object;
            freeTable(vm, &instance->fields);
            FREE(vm, ObjInstance, object);
            break;
        }

        case OBJ_NATIVE: {
            FREE(vm, ObjNative, object);
            break;
        }

        case OBJ_STRING: {
            ObjString *string = (ObjString *) object;
            FREE_ARRAY(vm, char, string->chars, string->length + 1);
            FREE(vm, ObjString, object);
            break;
        }

        case OBJ_LIST: {
            ObjList *list = (ObjList *) object;
            freeValueArray(vm, &list->values);
            FREE(vm, ObjList, list);
            break;
        }

        case OBJ_DICT: {
            ObjDict *dict = (ObjDict *) object;
            FREE_ARRAY(vm, DictItem, dict->entries, dict->capacityMask + 1);
            FREE(vm, ObjDict, dict);
            break;
        }

        case OBJ_SET: {
            ObjSet *set = (ObjSet *) object;
            FREE_ARRAY(vm, SetItem, set->entries, set->capacityMask + 1);
            FREE(vm, ObjSet, set);
            break;
        }

        case OBJ_FILE: {
            FREE(vm, ObjFile, object);
            break;
        }

        case OBJ_UPVALUE: {
            FREE(vm, ObjUpvalue, object);
            break;
        }
    }
}

void collectGarbage(VM *vm) {
#ifdef DEBUG_TRACE_GC
    printf("-- gc begin\n");
    size_t before = vm->bytesAllocated;
#endif

    // Mark the stack roots.
    for (Value *slot = vm->stack; slot < vm->stackTop; slot++) {
        grayValue(vm, *slot);
    }

    for (int i = 0; i < vm->frameCount; i++) {
        grayObject(vm, (Obj *) vm->frames[i].closure);
    }

    // Mark the open upvalues.
    for (ObjUpvalue *upvalue = vm->openUpvalues;
         upvalue != NULL;
         upvalue = upvalue->next) {
        grayObject(vm, (Obj *) upvalue);
    }

    // Mark the global roots.
    grayTable(vm, &vm->modules);
    grayTable(vm, &vm->globals);
    grayTable(vm, &vm->constants);
    grayTable(vm, &vm->imports);
    grayTable(vm, &vm->numberMethods);
    grayTable(vm, &vm->boolMethods);
    grayTable(vm, &vm->nilMethods);
    grayTable(vm, &vm->stringMethods);
    grayTable(vm, &vm->listMethods);
    grayTable(vm, &vm->dictMethods);
    grayTable(vm, &vm->setMethods);
    grayTable(vm, &vm->fileMethods);
    grayTable(vm, &vm->classMethods);
    grayTable(vm, &vm->instanceMethods);
    grayCompilerRoots(vm);
    grayObject(vm, (Obj *) vm->initString);
    grayObject(vm, (Obj *) vm->replVar);

    // Traverse the references.
    while (vm->grayCount > 0) {
        // Pop an item from the gray stack.
        Obj *object = vm->grayStack[--vm->grayCount];
        blackenObject(vm, object);
    }

    // Delete unused interned strings.
    tableRemoveWhite(vm, &vm->strings);

    // Collect the white objects.
    Obj **object = &vm->objects;
    while (*object != NULL) {
        if (!((*object)->isDark)) {
            // This object wasn't reached, so remove it from the list and
            // free it.
            Obj *unreached = *object;
            *object = unreached->next;
            freeObject(vm, unreached);
        } else {
            // This object was reached, so unmark it (for the next GC) and
            // move on to the next.
            (*object)->isDark = false;
            object = &(*object)->next;
        }
    }

    // Adjust the heap size based on live memory.
    vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;

#ifdef DEBUG_TRACE_GC
    printf("-- gc collected %ld bytes (from %ld to %ld) next at %ld\n",
           before - vm->bytesAllocated, before, vm->bytesAllocated,
           vm->nextGC);
#endif
}

void freeObjects(VM *vm) {
    Obj *object = vm->objects;
    while (object != NULL) {
        Obj *next = object->next;
        freeObject(vm, object);
        object = next;
    }

    free(vm->grayStack);
}

    /* 10: util.c */


char *readFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = (char *) malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }

    buffer[bytesRead] = '\0';

    fclose(file);
    return buffer;
}

void defineNative(VM *vm, Table *table, const char *name, NativeFn function) {
    ObjNative *native = newNative(vm, function);
    push(vm, OBJ_VAL(native));
    ObjString *methodName = copyString(vm, name, strlen(name));
    push(vm, OBJ_VAL(methodName));
    tableSet(vm, table, methodName, OBJ_VAL(native));
    pop(vm);
    pop(vm);
}

void defineNativeProperty(VM *vm, Table *table, const char *name, Value value) {
    push(vm, value);
    ObjString *propertyName = copyString(vm, name, strlen(name));
    push(vm, OBJ_VAL(propertyName));
    tableSet(vm, table, propertyName, value);
    pop(vm);
    pop(vm);
}

bool isValidKey(Value value) {
    if (IS_NIL(value) || IS_BOOL(value) || IS_NUMBER(value) ||
    IS_STRING(value)) {
        return true;
    }

    return false;
}

Value boolNative(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "bool() takes no arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    return BOOL_VAL(!isFalsey(args[0]));
}
    /* 11: bool.c */

static Value toStringBool(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    int val = AS_BOOL(args[0]);
    return OBJ_VAL(copyString(vm, val ? "true" : "false", val ? 4 : 5));
}

void declareBoolMethods(VM *vm) {
    defineNative(vm, &vm->boolMethods, "toString", toStringBool);
}

    /* 12: class.c */

static Value clas_toString(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "clas_toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = classToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}

void declareClassMethods(VM *vm) {
    defineNative(vm, &vm->classMethods, "toString", clas_toString);
}

    /* 13: copy.c */

ObjList *copyList(VM* vm, ObjList *oldList, bool shallow);

ObjDict *copyDict(VM* vm, ObjDict *oldDict, bool shallow) {
    ObjDict *newDict = initDict(vm);
    // Push to stack to avoid GC
    push(vm, OBJ_VAL(newDict));

    for (int i = 0; i <= oldDict->capacityMask; ++i) {
        if (IS_EMPTY(oldDict->entries[i].key)) {
            continue;
        }

        Value val = oldDict->entries[i].value;

        if (!shallow) {
            if (IS_DICT(val)) {
                val = OBJ_VAL(copyDict(vm, AS_DICT(val), false));
            } else if (IS_LIST(val)) {
                val = OBJ_VAL(copyList(vm, AS_LIST(val), false));
            } else if (IS_INSTANCE(val)) {
                val = OBJ_VAL(copyInstance(vm, AS_INSTANCE(val), false));
            }
        }

        // Push to stack to avoid GC
        push(vm, val);
        dictSet(vm, newDict, oldDict->entries[i].key, val);
        pop(vm);
    }

    pop(vm);
    return newDict;
}

ObjList *copyList(VM* vm, ObjList *oldList, bool shallow) {
    ObjList *newList = initList(vm);
    // Push to stack to avoid GC
    push(vm, OBJ_VAL(newList));

    for (int i = 0; i < oldList->values.count; ++i) {
        Value val = oldList->values.values[i];

        if (!shallow) {
            if (IS_DICT(val)) {
                val = OBJ_VAL(copyDict(vm, AS_DICT(val), false));
            } else if (IS_LIST(val)) {
                val = OBJ_VAL(copyList(vm, AS_LIST(val), false));
            } else if (IS_INSTANCE(val)) {
                val = OBJ_VAL(copyInstance(vm, AS_INSTANCE(val), false));
            }
        }

        // Push to stack to avoid GC
        push(vm, val);
        writeValueArray(vm, &newList->values, val);
        pop(vm);
    }

    pop(vm);
    return newList;
}

ObjInstance *copyInstance(VM* vm, ObjInstance *oldInstance, bool shallow) {
    ObjInstance *instance = newInstance(vm, oldInstance->klass);
    // Push to stack to avoid GC
    push(vm, OBJ_VAL(instance));

    if (shallow) {
        tableAddAll(vm, &oldInstance->fields, &instance->fields);
    } else {
        for (int i = 0; i <= oldInstance->fields.capacityMask; i++) {
            Entry *entry = &oldInstance->fields.entries[i];
            if (entry->key != NULL) {
                Value val = entry->value;

                if (IS_LIST(val)) {
                    val = OBJ_VAL(copyList(vm, AS_LIST(val), false));
                } else if (IS_DICT(val)) {
                    val = OBJ_VAL(copyDict(vm, AS_DICT(val), false));
                } else if (IS_INSTANCE(val)) {
                    val = OBJ_VAL(copyInstance(vm, AS_INSTANCE(val), false));
                }

                // Push to stack to avoid GC
                push(vm, val);
                tableSet(vm, &instance->fields, entry->key, val);
                pop(vm);
            }
        }
    }

    pop(vm);
    return instance;
}

// TODO: Set copy

    /* 14: dicts.c */

static Value toStringDict(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = dictToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}

static Value lenDict(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "len() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjDict *dict = AS_DICT(args[0]);
    return NUMBER_VAL(dict->count);
}

static Value getDictItem(VM *vm, int argCount, Value *args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError(vm, "get() takes 1 or 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    Value defaultValue = NIL_VAL;
    if (argCount == 2) {
        defaultValue = args[2];
    }

    if (!isValidKey(args[1])) {
        runtimeError(vm, "Dictionary key passed to get() must be an immutable type");
        return EMPTY_VAL;
    }

    ObjDict *dict = AS_DICT(args[0]);

    Value ret;
    if (dictGet(dict, args[1], &ret)) {
        return ret;
    }

    return defaultValue;
}

static Value removeDictItem(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "remove() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!isValidKey(args[1])) {
        runtimeError(vm, "Dictionary key passed to remove() must be an immutable type");
        return EMPTY_VAL;
    }

    ObjDict *dict = AS_DICT(args[0]);

    if (dictDelete(vm, dict, args[1])) {
        return NIL_VAL;
    }

    char *str = valueToString(args[1]);
    runtimeError(vm, "Key '%s' passed to remove() does not exist within the dictionary", str);
    free(str);

    return EMPTY_VAL;
}

static Value dictItemExists(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "exists() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!isValidKey(args[1])) {
        runtimeError(vm, "Dictionary key passed to exists() must be an immutable type");
        return EMPTY_VAL;
    }

    ObjDict *dict = AS_DICT(args[0]);

    if (dict->count == 0) {
        return FALSE_VAL;
    }

    Value v;
    if (dictGet(dict, args[1], &v)) {
        return TRUE_VAL;
    }

    return FALSE_VAL;
}

static Value copyDictShallow(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "copy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjDict *oldDict = AS_DICT(args[0]);
    ObjDict *newDict = copyDict(vm, oldDict, true);

    return OBJ_VAL(newDict);
}

static Value copyDictDeep(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "deepCopy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjDict *oldDict = AS_DICT(args[0]);
    ObjDict *newDict = copyDict(vm, oldDict, false);

    return OBJ_VAL(newDict);
}

void declareDictMethods(VM *vm) {
    defineNative(vm, &vm->dictMethods, "toString", toStringDict);
    defineNative(vm, &vm->dictMethods, "len", lenDict);
    defineNative(vm, &vm->dictMethods, "get", getDictItem);
    defineNative(vm, &vm->dictMethods, "remove", removeDictItem);
    defineNative(vm, &vm->dictMethods, "exists", dictItemExists);
    defineNative(vm, &vm->dictMethods, "copy", copyDictShallow);
    defineNative(vm, &vm->dictMethods, "deepCopy", copyDictDeep);
    defineNative(vm, &vm->dictMethods, "toBool", boolNative); // Defined in util
}

    /* 15: files.c */

static Value writeFile(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "write() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "write() argument must be a string");
        return EMPTY_VAL;
    }

    ObjFile *file = AS_FILE(args[0]);
    ObjString *string = AS_STRING(args[1]);

    if (strcmp(file->openType, "r") == 0) {
        runtimeError(vm, "File is not writable!");
        return EMPTY_VAL;
    }

    int charsWrote = fprintf(file->file, "%s", string->chars);
    fflush(file->file);

    return NUMBER_VAL(charsWrote);
}

static Value writeLineFile(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "writeLine() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "writeLine() argument must be a string");
        return EMPTY_VAL;
    }

    ObjFile *file = AS_FILE(args[0]);
    ObjString *string = AS_STRING(args[1]);

    if (strcmp(file->openType, "r") == 0) {
        runtimeError(vm, "File is not writable!");
        return EMPTY_VAL;
    }

    int charsWrote = fprintf(file->file, "%s\n", string->chars);
    fflush(file->file);

    return NUMBER_VAL(charsWrote);
}

static Value readFullFile(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "read() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjFile *file = AS_FILE(args[0]);

    size_t currentPosition = ftell(file->file);
    // Calculate file size
    fseek(file->file, 0L, SEEK_END);
    size_t fileSize = ftell(file->file);
    rewind(file->file);

    // Reset cursor position
    if (currentPosition < fileSize) {
        fileSize -= currentPosition;
        fseek(file->file, currentPosition, SEEK_SET);
    }

    char *buffer = (char *) malloc(fileSize + 1);
    if (buffer == NULL) {
        runtimeError(vm, "Not enough memory to read \"%s\".\n", file->path);
        return EMPTY_VAL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file->file);
    if (bytesRead < fileSize) {
        free(buffer);
        runtimeError(vm, "Could not read file \"%s\".\n", file->path);
        return EMPTY_VAL;
    }

    buffer[bytesRead] = '\0';
    Value ret = OBJ_VAL(copyString(vm, buffer, fileSize));

    free(buffer);
    return ret;
}

static Value readLineFile(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "readLine() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    // TODO: This could be better
    char line[4096];

    ObjFile *file = AS_FILE(args[0]);
    if (fgets(line, 4096, file->file) != NULL) {
        int lineLength = strlen(line);
        // Remove newline char
        if (line[lineLength - 1] == '\n') {
            lineLength--;
            line[lineLength] = '\0';
        }
        return OBJ_VAL(copyString(vm, line, lineLength));
    }

    return NIL_VAL;
}

static Value seekFile(VM *vm, int argCount, Value *args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError(vm, "seek() takes 1 or 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    int seekType = SEEK_SET;

    if (argCount == 2) {
        if (!IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
            runtimeError(vm, "seek() arguments must be numbers");
            return EMPTY_VAL;
        }

        int seekTypeNum = AS_NUMBER(args[2]);

        switch (seekTypeNum) {
            case 0:
                seekType = SEEK_SET;
                break;
            case 1:
                seekType = SEEK_CUR;
                break;
            case 2:
                seekType = SEEK_END;
                break;
            default:
                seekType = SEEK_SET;
                break;
        }
    }

    if (!IS_NUMBER(args[1])) {
        runtimeError(vm, "seek() argument must be a number");
        return EMPTY_VAL;
    }

    int offset = AS_NUMBER(args[1]);
    ObjFile *file = AS_FILE(args[0]);
    fseek(file->file, offset, seekType);

    return NIL_VAL;
}

void declareFileMethods(VM *vm) {
    defineNative(vm, &vm->fileMethods, "write", writeFile);
    defineNative(vm, &vm->fileMethods, "writeLine", writeLineFile);
    defineNative(vm, &vm->fileMethods, "read", readFullFile);
    defineNative(vm, &vm->fileMethods, "readLine", readLineFile);
    defineNative(vm, &vm->fileMethods, "seek", seekFile);
}

    /* 16: instance.c */

static Value toString(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = instanceToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}


static Value hasAttribute(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "hasAttribute() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjInstance *instance = AS_INSTANCE(args[0]);
    Value value = args[1];

    if (!IS_STRING(value)) {
        runtimeError(vm, "Argument passed to hasAttribute() must be a string");
        return EMPTY_VAL;
    }

    Value _; // Unused variable
    if (tableGet(&instance->fields, AS_STRING(value), &_)) {
        return TRUE_VAL;
    }

    return FALSE_VAL;
}

static Value getAttribute(VM *vm, int argCount, Value *args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError(vm, "getAttribute() takes 1 or 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    Value defaultValue = NIL_VAL;
    // Passed in a default value
    if (argCount == 2) {
        defaultValue = args[2];
    }

    Value key = args[1];

    if (!IS_STRING(key)) {
        runtimeError(vm, "Argument passed to getAttribute() must be a string");
        return EMPTY_VAL;
    }

    ObjInstance *instance = AS_INSTANCE(args[0]);

    Value value;
    if (tableGet(&instance->fields, AS_STRING(key), &value)) {
        return value;
    }

    return defaultValue;
}

static Value setAttribute(VM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "setAttribute() takes 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    Value value = args[2];
    Value key = args[1];

    if (!IS_STRING(key)) {
        runtimeError(vm, "Argument passed to setAttribute() must be a string");
        return EMPTY_VAL;
    }

    ObjInstance *instance = AS_INSTANCE(args[0]);
    tableSet(vm, &instance->fields, AS_STRING(key), value);

    return NIL_VAL;
}

static Value copyShallow(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "copy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjInstance *oldInstance = AS_INSTANCE(args[0]);
    ObjInstance *instance = copyInstance(vm, oldInstance, true);

    return OBJ_VAL(instance);
}

static Value copyDeep(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "deepCopy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjInstance *oldInstance = AS_INSTANCE(args[0]);
    ObjInstance *instance = copyInstance(vm, oldInstance, false);

    return OBJ_VAL(instance);
}

void declareInstanceMethods(VM *vm) {
    defineNative(vm, &vm->instanceMethods, "toString", toString);
    defineNative(vm, &vm->instanceMethods, "hasAttribute", hasAttribute);
    defineNative(vm, &vm->instanceMethods, "getAttribute", getAttribute);
    defineNative(vm, &vm->instanceMethods, "setAttribute", setAttribute);
    defineNative(vm, &vm->instanceMethods, "copy", copyShallow);
    defineNative(vm, &vm->instanceMethods, "deepCopy", copyDeep);
}

    /* 17: lists.c */

static Value toStringList(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = listToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}

static Value lenList(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "len() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    return NUMBER_VAL(list->values.count);
}

static Value extendList(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "extend() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_LIST(args[1])) {
        runtimeError(vm, "extend() argument must be a list");
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    ObjList *listArgument = AS_LIST(args[1]);

    for (int i = 0; i < listArgument->values.count; i++) {
        writeValueArray(vm, &list->values, listArgument->values.values[i]);
    }

    return NIL_VAL;
}

static Value pushListItem(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "push() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    writeValueArray(vm, &list->values, args[1]);

    return NIL_VAL;
}

static Value insertListItem(VM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "insert() takes 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[2])) {
        runtimeError(vm, "insert() second argument must be a number");
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    Value insertValue = args[1];
    int index = AS_NUMBER(args[2]);

    if (index < 0 || index > list->values.count) {
        runtimeError(vm, "Index passed to insert() is out of bounds for the list given");
        return EMPTY_VAL;
    }

    if (list->values.capacity < list->values.count + 1) {
        int oldCapacity = list->values.capacity;
        list->values.capacity = GROW_CAPACITY(oldCapacity);
        list->values.values = GROW_ARRAY(vm, list->values.values, Value,
                                         oldCapacity, list->values.capacity);
    }

    list->values.count++;

    for (int i = list->values.count - 1; i > index; --i) {
        list->values.values[i] = list->values.values[i - 1];
    }

    list->values.values[index] = insertValue;

    return NIL_VAL;
}

static Value popListItem(VM *vm, int argCount, Value *args) {
    if (argCount != 0 && argCount != 1) {
        runtimeError(vm, "pop() takes either 0 or 1 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);

    if (list->values.count == 0) {
        runtimeError(vm, "pop() called on an empty list");
        return EMPTY_VAL;
    }

    Value element;

    if (argCount == 1) {
        if (!IS_NUMBER(args[1])) {
            runtimeError(vm, "pop() index argument must be a number");
            return EMPTY_VAL;
        }

        int index = AS_NUMBER(args[1]);

        if (index < 0 || index > list->values.count) {
            runtimeError(vm, "Index passed to pop() is out of bounds for the list given");
            return EMPTY_VAL;
        }

        element = list->values.values[index];

        for (int i = index; i < list->values.count - 1; ++i) {
            list->values.values[i] = list->values.values[i + 1];
        }
    } else {
        element = list->values.values[list->values.count - 1];
    }

    list->values.count--;

    return element;
}

static Value containsListItem(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "contains() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    Value search = args[1];

    for (int i = 0; i < list->values.count; ++i) {
        if (valuesEqual(list->values.values[i], search)) {
            return TRUE_VAL;
        }
    }

    return FALSE_VAL;
}

static Value joinListItem(VM *vm, int argCount, Value *args) {
    if (argCount != 0 && argCount != 1) {
        runtimeError(vm, "join() takes 1 optional argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    char *delimiter = ", ";

    if (argCount == 1) {
        if (!IS_STRING(args[1])) {
            runtimeError(vm, "join() only takes a string as an argument");
            return EMPTY_VAL;
        }

        delimiter = AS_CSTRING(args[1]);
    }

    char *output;
    char *fullString = NULL;
    int index = 0;
    int delimiterLength = strlen(delimiter);

    for (int j = 0; j < list->values.count - 1; ++j) {
        if (IS_STRING(list->values.values[j])) {
            output = AS_CSTRING(list->values.values[j]);
        } else {
            output = valueToString(list->values.values[j]);
        }
        int elementLength = strlen(output);
        fullString = realloc(fullString, index + elementLength + delimiterLength + 1);

        memcpy(fullString + index, output, elementLength);
        if (!IS_STRING(list->values.values[j])) {
            free(output);
        }
        index += elementLength;
        memcpy(fullString + index, delimiter, delimiterLength);
        index += delimiterLength;
    }

    // Outside the loop as we do not want the append the delimiter on the last element
    if (IS_STRING(list->values.values[list->values.count - 1])) {
        output = AS_CSTRING(list->values.values[list->values.count - 1]);
    } else {
        output = valueToString(list->values.values[list->values.count - 1]);
    }

    int elementLength = strlen(output);
    fullString = realloc(fullString, index + elementLength + 1);
    memcpy(fullString + index, output, elementLength);
    index += elementLength;

    fullString[index] = '\0';

    if (!IS_STRING(list->values.values[list->values.count - 1])) {
        free(output);
    }

    Value ret = OBJ_VAL(copyString(vm, fullString, index));
    free(fullString);
    return ret;
}

static Value copyListShallow(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "copy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *oldList = AS_LIST(args[0]);
    ObjList *newList = copyList(vm, oldList, true);
    return OBJ_VAL(newList);
}

static Value copyListDeep(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "deepCopy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *oldList = AS_LIST(args[0]);
    ObjList *newList = copyList(vm, oldList, false);

    return OBJ_VAL(newList);
}

void declareListMethods(VM *vm) {
    defineNative(vm, &vm->listMethods, "toString", toStringList);
    defineNative(vm, &vm->listMethods, "len", lenList);
    defineNative(vm, &vm->listMethods, "extend", extendList);
    defineNative(vm, &vm->listMethods, "push", pushListItem);
    defineNative(vm, &vm->listMethods, "insert", insertListItem);
    defineNative(vm, &vm->listMethods, "pop", popListItem);
    defineNative(vm, &vm->listMethods, "contains", containsListItem);
    defineNative(vm, &vm->listMethods, "join", joinListItem);
    defineNative(vm, &vm->listMethods, "copy", copyListShallow);
    defineNative(vm, &vm->listMethods, "deepCopy", copyListDeep);
    defineNative(vm, &vm->listMethods, "toBool", boolNative); // Defined in util
}

    /* 18: nil.c */

static Value toStringNil(VM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    return OBJ_VAL(copyString(vm, "nil", 3));
}

void declareNilMethods(VM *vm) {
    defineNative(vm, &vm->nilMethods, "toString", toStringNil);
    defineNative(vm, &vm->nilMethods, "toBool", boolNative); // Defined in util
}

    /* 19: number.c */

static Value toStringNumber(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    double number = AS_NUMBER(args[0]);
    int numberStringLength = snprintf(NULL, 0, "%.15g", number) + 1;
    char numberString[numberStringLength];
    snprintf(numberString, numberStringLength, "%.15g", number);
    return OBJ_VAL(copyString(vm, numberString, numberStringLength - 1));
}

void declareNumberMethods(VM *vm) {
    defineNative(vm, &vm->numberMethods, "toString", toStringNumber);
    defineNative(vm, &vm->numberMethods, "toBool", boolNative); // Defined in util
}

    /* 20: sets.c */

static Value toStringSet(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = setToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}

static Value lenSet(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "len() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjSet *set = AS_SET(args[0]);
    return NUMBER_VAL(set->count);
}

static Value addSetItem(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "add() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!isValidKey(args[1])) {
        runtimeError(vm, "Set value must be an immutable type");
        return EMPTY_VAL;
    }

    ObjSet *set = AS_SET(args[0]);
    setInsert(vm, set, args[1]);

    return NIL_VAL;
}

static Value removeSetItem(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "remove() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjSet *set = AS_SET(args[0]);

    if (!setDelete(vm, set, args[1])) {
        char *str = valueToString(args[1]);
        runtimeError(vm, "Value '%s' passed to remove() does not exist within the set", str);
        free(str);
        return EMPTY_VAL;
    }

    return NIL_VAL;
}

static Value containsSetItem(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "contains() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjSet *set = AS_SET(args[0]);

    return setGet(set, args[1]) ? TRUE_VAL : FALSE_VAL;
}

void declareSetMethods(VM *vm) {
    defineNative(vm, &vm->setMethods, "toString", toStringSet);
    defineNative(vm, &vm->setMethods, "len", lenSet);
    defineNative(vm, &vm->setMethods, "add", addSetItem);
    defineNative(vm, &vm->setMethods, "remove", removeSetItem);
    defineNative(vm, &vm->setMethods, "contains", containsSetItem);
    defineNative(vm, &vm->setMethods, "toBool", boolNative); // Defined in util
}


    /* 21: strings.c */


static Value lenString(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "len() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);
    return NUMBER_VAL(string->length);
}

static Value toNumberString(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toNumber() takes no arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    char *numberString = AS_CSTRING(args[0]);
    char *end;
    errno = 0;

    double number = strtod(numberString, &end);

    // Failed conversion
    if (errno != 0 || *end != '\0') {
        return NIL_VAL;
    }

    return NUMBER_VAL(number);
}

static Value formatString(VM *vm, int argCount, Value *args) {
    if (argCount == 0) {
        runtimeError(vm, "format() takes at least 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    int length = 0;
    char **replaceStrings = malloc(argCount * sizeof(char*));

    for (int j = 1; j < argCount + 1; j++) {
        Value value = args[j];
        if (!IS_STRING(value))
            replaceStrings[j - 1] = valueToString(value);
        else {
            ObjString *strObj = AS_STRING(value);
            char *str = malloc(strObj->length + 1);
            memcpy(str, strObj->chars, strObj->length + 1);
            replaceStrings[j - 1] = str;
        }

        length += strlen(replaceStrings[j - 1]);
    }

    ObjString *string = AS_STRING(args[0]);

    int stringLen = string->length + 1;
    char *tmp = malloc(stringLen);
    char *tmpFree = tmp;
    memcpy(tmp, string->chars, stringLen);

    int count = 0;
    while((tmp = strstr(tmp, "{}")))
    {
        count++;
        tmp++;
    }

    tmp = tmpFree;

    if (count != argCount) {
        runtimeError(vm, "format() placeholders do not match arguments");

        for (int i = 0; i < argCount; ++i) {
            free(replaceStrings[i]);
        }

        free(tmp);
        free(replaceStrings);
        return EMPTY_VAL;
    }

    int fullLength = string->length - count * 2 + length + 1;
    char *pos;
    char *newStr = malloc(sizeof(char) * fullLength);
    int stringLength = 0;

    for (int i = 0; i < argCount; ++i) {
        pos = strstr(tmp, "{}");
        if (pos != NULL)
            *pos = '\0';

        int tmpLength = strlen(tmp);
        int replaceLength = strlen(replaceStrings[i]);
        memcpy(newStr + stringLength, tmp, tmpLength);
        memcpy(newStr + stringLength + tmpLength, replaceStrings[i], replaceLength);
        stringLength += tmpLength + replaceLength;
        tmp = pos + 2;
        free(replaceStrings[i]);
    }

    free(replaceStrings);
    memcpy(newStr + stringLength, tmp, strlen(tmp));
    ObjString *newString = copyString(vm, newStr, fullLength - 1);

    free(newStr);
    free(tmpFree);
    return OBJ_VAL(newString);
}

static Value splitString(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "split() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "Argument passed to split() must be a string");
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);
    char *delimiter = AS_CSTRING(args[1]);

    char *tmp = malloc(string->length + 1);
    char *tmpFree = tmp;
    memcpy(tmp, string->chars, string->length + 1);
    int delimiterLength = strlen(delimiter);
    char *token;

    ObjList *list = initList(vm);
    push(vm, OBJ_VAL(list));

    do {
        token = strstr(tmp, delimiter);
        if (token)
            *token = '\0';

        Value str = OBJ_VAL(copyString(vm, tmp, strlen(tmp)));

        // Push to stack to avoid GC
        push(vm, str);
        writeValueArray(vm, &list->values, str);
        pop(vm);

        tmp = token + delimiterLength;
    } while (token != NULL);
    pop(vm);

    free(tmpFree);
    return OBJ_VAL(list);
}

static Value containsString(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "contains() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "Argument passed to contains() must be a string");
        return EMPTY_VAL;
    }

    char *string = AS_CSTRING(args[0]);
    char *delimiter = AS_CSTRING(args[1]);

    if (!strstr(string, delimiter)) {
        return FALSE_VAL;
    }

    return TRUE_VAL;
}

static Value findString(VM *vm, int argCount, Value *args) {
    if (argCount < 1 || argCount > 2) {
        runtimeError(vm, "find() takes either 1 or 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    int index = 1;

    if (argCount == 2) {
        if (!IS_NUMBER(args[2])) {
            runtimeError(vm, "Index passed to find() must be a number");
            return EMPTY_VAL;
        }

        index = AS_NUMBER(args[2]);
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "Substring passed to find() must be a string");
        return EMPTY_VAL;
    }

    char *substr = AS_CSTRING(args[1]);
    char *string = AS_CSTRING(args[0]);

    int position = 0;

    for (int i = 0; i < index; ++i) {
        char *result = strstr(string, substr);
        if (!result) {
            position = -1;
            break;
        }

        position += (result - string) + (i * strlen(substr));
        string = result + strlen(substr);
    }

    return NUMBER_VAL(position);
}

static Value replaceString(VM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "replace() takes 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError(vm, "Arguments passed to replace() must be a strings");
        return EMPTY_VAL;
    }

    // Pop values off the stack
    Value stringValue = args[0];
    ObjString *to_replace = AS_STRING(args[1]);
    ObjString *replace = AS_STRING(args[2]);
    char *string = AS_CSTRING(stringValue);

    int count = 0;
    int len = to_replace->length;
    int replaceLen = replace->length;
    int stringLen = strlen(string) + 1;

    // Make a copy of the string so we do not modify the original
    char *tmp = malloc(stringLen);
    char *tmpFree = tmp;
    memcpy(tmp, string, stringLen);

    // Count the occurrences of the needle so we can determine the size
    // of the string we need to allocate
    while((tmp = strstr(tmp, to_replace->chars)) != NULL) {
        count++;
        tmp += len;
    }

    // Reset the pointer
    tmp = tmpFree;

    if (count == 0) {
        free(tmpFree);
        return stringValue;
    }

    int length = strlen(tmp) - count * (len - replaceLen) + 1;
    char *pos;
    char *newStr = malloc(sizeof(char) * length);
    int stringLength = 0;

    for (int i = 0; i < count; ++i) {
        pos = strstr(tmp, to_replace->chars);
        if (pos != NULL)
            *pos = '\0';

        int tmpLength = strlen(tmp);
        memcpy(newStr + stringLength, tmp, tmpLength);
        memcpy(newStr + stringLength + tmpLength, replace->chars, replaceLen);
        stringLength += tmpLength + replaceLen;
        tmp = pos + len;
    }

    memcpy(newStr + stringLength, tmp, strlen(tmp));
    ObjString *newString = copyString(vm, newStr, length - 1);

    free(newStr);
    free(tmpFree);
    return OBJ_VAL(newString);
}

static Value lowerString(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "lower() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);

    char *temp = malloc(sizeof(char) * (string->length + 1));

    for(int i = 0; string->chars[i]; i++){
        temp[i] = tolower(string->chars[i]);
    }
    temp[string->length] = '\0';

    Value ret = OBJ_VAL(copyString(vm, temp, string->length));
    free(temp);
    return ret;
}

static Value upperString(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "upper() takes no arguments (%d given)", argCount);
        return EMPTY_VAL ;
    }

    ObjString *string = AS_STRING(args[0]);

    char *temp = malloc(sizeof(char) * (string->length + 1));

    for(int i = 0; string->chars[i]; i++){
        temp[i] = toupper(string->chars[i]);
    }
    temp[string->length] = '\0';

    Value ret = OBJ_VAL(copyString(vm, temp, string->length));
    free(temp);
    return ret;
}

static Value startsWithString(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "startsWith() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "Argument passed to startsWith() must be a string");
        return EMPTY_VAL;
    }

    char *string = AS_CSTRING(args[0]);
    ObjString *start = AS_STRING(args[1]);

    return BOOL_VAL(strncmp(string, start->chars, start->length) == 0);
}

static Value endsWithString(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "endsWith() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "Argument passed to endsWith() must be a string");
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);
    ObjString *suffix = AS_STRING(args[1]);

    if (string->length < suffix->length) {
        return FALSE_VAL;
    }

    return BOOL_VAL(strcmp(string->chars + (string->length - suffix->length), suffix->chars) == 0);
}

static Value leftStripString(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "leftStrip() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }
    ObjString *string = AS_STRING(args[0]);

    bool charSeen = false;
    int i, count = 0;

    char *temp = malloc(sizeof(char) * (string->length + 1));

    for (i = 0; i < string->length; ++i) {
        if (!charSeen && isspace(string->chars[i])) {
            count++;
            continue;
        }
        temp[i - count] = string->chars[i];
        charSeen = true;
    }
    temp[i - count] = '\0';
    Value ret = OBJ_VAL(copyString(vm, temp, i - count));
    free(temp);
    return ret;
}

static Value rightStripString(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "rightStrip() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);
    int length;
    char *temp = malloc(sizeof(char) * (string->length + 1));

    for (length = string->length - 1; length > 0; --length) {
        if (!isspace(string->chars[length])) {
            break;
        }
    }

    memcpy(temp, string->chars, length + 1);
    Value ret = OBJ_VAL(copyString(vm, temp, length + 1));
    free(temp);
    return ret;
}

static Value stripString(VM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "strip() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    Value string = leftStripString(vm, 0, args);
    return rightStripString(vm, 0, &string);
}

static Value countString(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "count() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "Argument passed to count() must be a string");
        return EMPTY_VAL;
    }

    char *haystack = AS_CSTRING(args[0]);
    char *needle = AS_CSTRING(args[1]);

    int count = 0;
    while((haystack = strstr(haystack, needle)))
    {
        count++;
        haystack++;
    }

    return NUMBER_VAL(count);
}

void declareStringMethods(VM *vm) {
    defineNative(vm, &vm->stringMethods, "len", lenString);
    defineNative(vm, &vm->stringMethods, "toNumber", toNumberString);
    defineNative(vm, &vm->stringMethods, "format", formatString);
    defineNative(vm, &vm->stringMethods, "split", splitString);
    defineNative(vm, &vm->stringMethods, "contains", containsString);
    defineNative(vm, &vm->stringMethods, "find", findString);
    defineNative(vm, &vm->stringMethods, "replace", replaceString);
    defineNative(vm, &vm->stringMethods, "lower", lowerString);
    defineNative(vm, &vm->stringMethods, "upper", upperString);
    defineNative(vm, &vm->stringMethods, "startsWith", startsWithString);
    defineNative(vm, &vm->stringMethods, "endsWith", endsWithString);
    defineNative(vm, &vm->stringMethods, "leftStrip", leftStripString);
    defineNative(vm, &vm->stringMethods, "rightStrip", rightStripString);
    defineNative(vm, &vm->stringMethods, "strip", stripString);
    defineNative(vm, &vm->stringMethods, "count", countString);
    defineNative(vm, &vm->stringMethods, "toBool", boolNative); // Defined in util
}

    /* 22: c.c */

Value strerrorGeneric(VM *vm, int error) {
    if (error <= 0) {
        runtimeError(vm, "strerror() argument should be > 0");
        return EMPTY_VAL;
    }

    char buf[MAX_ERROR_LEN];

#ifdef POSIX_STRERROR
    int retval = strerror_r(error, buf, sizeof(buf));

    if (!retval) {
        return OBJ_VAL(copyString(vm, buf, strlen(buf)));
    }

    if (retval == EINVAL) {
        runtimeError(vm, "strerror() argument should be <= %d", LAST_ERROR);
        return EMPTY_VAL;
    }

    /* it is safe to assume that we do not reach here */
    return OBJ_VAL(copyString(vm, "Buffer is too small", 19));
#else
    char *tmp;
    tmp = strerror_r(error, buf, sizeof(buf));
    return OBJ_VAL(copyString(vm, buf, strlen(buf)));
#endif
}

Value strerrorNative(VM *vm, int argCount, Value *args) {
    if (argCount > 1) {
        runtimeError(vm, "strerror() takes either 0 or 1 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    int error;
    if (argCount == 1) {
      error = AS_NUMBER(args[0]);
    } else {
        error = AS_NUMBER(GET_ERRNO(GET_SELF_CLASS));
    }

    return strerrorGeneric(vm, error);
}

void createCClass(VM *vm) {
    ObjString *name = copyString(vm, "C", 1);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define C methods
     */
    defineNative(vm, &klass->methods, "strerror", strerrorNative);

    /**
     * Define C properties
     */
    defineNativeProperty(vm, &klass->properties, "EPERM",  NUMBER_VAL(EPERM));
    defineNativeProperty(vm, &klass->properties, "ENOENT", NUMBER_VAL(ENOENT));
    defineNativeProperty(vm, &klass->properties, "ESRCH",  NUMBER_VAL(ESRCH));
    defineNativeProperty(vm, &klass->properties, "EINTR",  NUMBER_VAL(EINTR));
    defineNativeProperty(vm, &klass->properties, "EIO",    NUMBER_VAL(EIO));
    defineNativeProperty(vm, &klass->properties, "ENXIO",  NUMBER_VAL(ENXIO));
    defineNativeProperty(vm, &klass->properties, "E2BIG",  NUMBER_VAL(E2BIG));
    defineNativeProperty(vm, &klass->properties, "ENOEXEC",NUMBER_VAL(ENOEXEC));
    defineNativeProperty(vm, &klass->properties, "EAGAIN", NUMBER_VAL(EAGAIN));
    defineNativeProperty(vm, &klass->properties, "ENOMEM", NUMBER_VAL(ENOMEM));
    defineNativeProperty(vm, &klass->properties, "EACCES", NUMBER_VAL(EACCES));
    defineNativeProperty(vm, &klass->properties, "EFAULT", NUMBER_VAL(EFAULT));
#ifdef ENOTBLK
    defineNativeProperty(vm, &klass->properties, "ENOTBLK", NUMBER_VAL(ENOTBLK));
#endif
    defineNativeProperty(vm, &klass->properties, "EBUSY",  NUMBER_VAL(EBUSY));
    defineNativeProperty(vm, &klass->properties, "EEXIST", NUMBER_VAL(EEXIST));
    defineNativeProperty(vm, &klass->properties, "EXDEV",  NUMBER_VAL(EXDEV));
    defineNativeProperty(vm, &klass->properties, "ENODEV", NUMBER_VAL(ENODEV));
    defineNativeProperty(vm, &klass->properties, "ENOTDIR",NUMBER_VAL(ENOTDIR));
    defineNativeProperty(vm, &klass->properties, "EISDIR", NUMBER_VAL(EISDIR));
    defineNativeProperty(vm, &klass->properties, "EINVAL", NUMBER_VAL(EINVAL));
    defineNativeProperty(vm, &klass->properties, "ENFILE", NUMBER_VAL(ENFILE));
    defineNativeProperty(vm, &klass->properties, "EMFILE", NUMBER_VAL(EMFILE));
    defineNativeProperty(vm, &klass->properties, "ENOTTY", NUMBER_VAL(ENOTTY));
    defineNativeProperty(vm, &klass->properties, "ETXTBSY",NUMBER_VAL(ETXTBSY));
    defineNativeProperty(vm, &klass->properties, "EFBIG",  NUMBER_VAL(EFBIG));
    defineNativeProperty(vm, &klass->properties, "ENOSPC", NUMBER_VAL(ENOSPC));
    defineNativeProperty(vm, &klass->properties, "ESPIPE", NUMBER_VAL(ESPIPE));
    defineNativeProperty(vm, &klass->properties, "EROFS",  NUMBER_VAL(EROFS));
    defineNativeProperty(vm, &klass->properties, "EMLINK", NUMBER_VAL(EMLINK));
    defineNativeProperty(vm, &klass->properties, "EPIPE",  NUMBER_VAL(EPIPE));
    defineNativeProperty(vm, &klass->properties, "EDOM",   NUMBER_VAL(EDOM));
    defineNativeProperty(vm, &klass->properties, "ERANGE", NUMBER_VAL(ERANGE));
    defineNativeProperty(vm, &klass->properties, "EDEADLK",NUMBER_VAL(EDEADLK));
    defineNativeProperty(vm, &klass->properties, "ENAMETOOLONG", NUMBER_VAL(ENAMETOOLONG));
    defineNativeProperty(vm, &klass->properties, "ENOLCK", NUMBER_VAL(ENOLCK));
    defineNativeProperty(vm, &klass->properties, "ENOSYS", NUMBER_VAL(ENOSYS));
    defineNativeProperty(vm, &klass->properties, "ENOTEMPTY", NUMBER_VAL(ENOTEMPTY));
    defineNativeProperty(vm, &klass->properties, "ELOOP",  NUMBER_VAL(ELOOP));
    defineNativeProperty(vm, &klass->properties, "EWOULDBLOCK", NUMBER_VAL(EWOULDBLOCK));
    defineNativeProperty(vm, &klass->properties, "ENOMSG", NUMBER_VAL(ENOMSG));
    defineNativeProperty(vm, &klass->properties, "EIDRM", NUMBER_VAL(EIDRM));
#ifdef ECHRNG
    defineNativeProperty(vm, &klass->properties, "ECHRNG", NUMBER_VAL(ECHRNG));
#endif
#ifdef EL2NSYNC
    defineNativeProperty(vm, &klass->properties, "EL2NSYNC", NUMBER_VAL(EL2NSYNC));
#endif
#ifdef EL3HLT
    defineNativeProperty(vm, &klass->properties, "EL3HLT", NUMBER_VAL(EL3HLT));
#endif
#ifdef EL3RST
    defineNativeProperty(vm, &klass->properties, "EL3RST", NUMBER_VAL(EL3RST));
#endif
#ifdef ELNRNG
    defineNativeProperty(vm, &klass->properties, "ELNRNG", NUMBER_VAL(ELNRNG));
#endif
#ifdef EUNATCH
    defineNativeProperty(vm, &klass->properties, "EUNATCH", NUMBER_VAL(EUNATCH));
#endif
#ifdef ENOCSI
    defineNativeProperty(vm, &klass->properties, "ENOCSI", NUMBER_VAL(ENOCSI));
#endif
#ifdef EL2HLT
    defineNativeProperty(vm, &klass->properties, "EL2HLT", NUMBER_VAL(EL2HLT));
#endif
#ifdef EBADE
    defineNativeProperty(vm, &klass->properties, "EBADE", NUMBER_VAL(EBADE));
#endif
#ifdef EBADR
    defineNativeProperty(vm, &klass->properties, "EBADR", NUMBER_VAL(EBADR));
#endif
#ifdef EXFULL
    defineNativeProperty(vm, &klass->properties, "EXFULL", NUMBER_VAL(EXFULL));
#endif
#ifdef ENOANO
    defineNativeProperty(vm, &klass->properties, "ENOANO", NUMBER_VAL(ENOANO));
#endif
#ifdef EBADRQC
    defineNativeProperty(vm, &klass->properties, "EBADRQC", NUMBER_VAL(EBADRQC));
#endif
#ifdef EBADSLT
    defineNativeProperty(vm, &klass->properties, "EBADSLT", NUMBER_VAL(EBADSLT));
#endif
#ifdef EDEADLOCK
    defineNativeProperty(vm, &klass->properties, "EDEADLOCK", NUMBER_VAL(EDEADLOCK));
#endif
#ifdef EBFONT
    defineNativeProperty(vm, &klass->properties, "EBFONT", NUMBER_VAL(EBFONT));
#endif
    defineNativeProperty(vm, &klass->properties, "ENOSTR", NUMBER_VAL(ENOSTR));
    defineNativeProperty(vm, &klass->properties, "ENODATA", NUMBER_VAL(ENODATA));
    defineNativeProperty(vm, &klass->properties, "ETIME", NUMBER_VAL(ETIME));
    defineNativeProperty(vm, &klass->properties, "ENOSR", NUMBER_VAL(ENOSR));
#ifdef ENONET
    defineNativeProperty(vm, &klass->properties, "ENONET", NUMBER_VAL(ENONET));
#endif
#ifdef ENOPKG
    defineNativeProperty(vm, &klass->properties, "ENOPKG", NUMBER_VAL(ENOPKG));
#endif
#ifdef EREMOTE
    defineNativeProperty(vm, &klass->properties, "EREMOTE", NUMBER_VAL(EREMOTE));
#endif
    defineNativeProperty(vm, &klass->properties, "ENOLINK", NUMBER_VAL(ENOLINK));
#ifdef EADV
    defineNativeProperty(vm, &klass->properties, "EADV", NUMBER_VAL(EADV));
#endif
#ifdef ESRMNT
    defineNativeProperty(vm, &klass->properties, "ESRMNT", NUMBER_VAL(ESRMNT));
#endif
#ifdef ECOMM
    defineNativeProperty(vm, &klass->properties, "ECOMM", NUMBER_VAL(ECOMM));
#endif
    defineNativeProperty(vm, &klass->properties, "EPROTO", NUMBER_VAL(EPROTO));
    defineNativeProperty(vm, &klass->properties, "EMULTIHOP", NUMBER_VAL(EMULTIHOP));
#ifdef EDOTDOT
    defineNativeProperty(vm, &klass->properties, "EDOTDOT", NUMBER_VAL(EDOTDOT));
#endif
    defineNativeProperty(vm, &klass->properties, "EBADMSG", NUMBER_VAL(EBADMSG));
    defineNativeProperty(vm, &klass->properties, "EOVERFLOW", NUMBER_VAL(EOVERFLOW));
#ifdef ENOTUNIQ
    defineNativeProperty(vm, &klass->properties, "ENOTUNIQ", NUMBER_VAL(ENOTUNIQ));
#endif
#ifdef EBADFD
    defineNativeProperty(vm, &klass->properties, "EBADFD", NUMBER_VAL(EBADFD));
#endif
#ifdef EREMCHG
    defineNativeProperty(vm, &klass->properties, "EREMCHG", NUMBER_VAL(EREMCHG));
#endif
#ifdef ELIBACC
    defineNativeProperty(vm, &klass->properties, "ELIBACC", NUMBER_VAL(ELIBACC));
#endif
#ifdef ELIBBAD
    defineNativeProperty(vm, &klass->properties, "ELIBBAD", NUMBER_VAL(ELIBBAD));
#endif
#ifdef ELIBSCN
    defineNativeProperty(vm, &klass->properties, "ELIBSCN", NUMBER_VAL(ELIBSCN));
#endif
#ifdef ELIBMAX
    defineNativeProperty(vm, &klass->properties, "ELIBMAX", NUMBER_VAL(ELIBMAX));
#endif
#ifdef ELIBEXEC
    defineNativeProperty(vm, &klass->properties, "ELIBEXEC", NUMBER_VAL(ELIBEXEC));
#endif
    defineNativeProperty(vm, &klass->properties, "EILSEQ", NUMBER_VAL(EILSEQ));
#ifdef ERESTART
    defineNativeProperty(vm, &klass->properties, "ERESTART", NUMBER_VAL(ERESTART));
#endif
#ifdef ESTRPIPE
    defineNativeProperty(vm, &klass->properties, "ESTRPIPE", NUMBER_VAL(ESTRPIPE));
#endif
#ifdef EUSERS
    defineNativeProperty(vm, &klass->properties, "EUSERS", NUMBER_VAL(EUSERS));
#endif
    defineNativeProperty(vm, &klass->properties, "ENOTSOCK", NUMBER_VAL(ENOTSOCK));
    defineNativeProperty(vm, &klass->properties, "EDESTADDRREQ", NUMBER_VAL(EDESTADDRREQ));
    defineNativeProperty(vm, &klass->properties, "EMSGSIZE", NUMBER_VAL(EMSGSIZE));
    defineNativeProperty(vm, &klass->properties, "EPROTOTYPE", NUMBER_VAL(EPROTOTYPE));
    defineNativeProperty(vm, &klass->properties, "ENOPROTOOPT", NUMBER_VAL(ENOPROTOOPT));
    defineNativeProperty(vm, &klass->properties, "EPROTONOSUPPORT", NUMBER_VAL(EPROTONOSUPPORT));
#ifdef ESOCKTNOSUPPORT
    defineNativeProperty(vm, &klass->properties, "ESOCKTNOSUPPORT", NUMBER_VAL(ESOCKTNOSUPPORT));
#endif
    defineNativeProperty(vm, &klass->properties, "EOPNOTSUPP", NUMBER_VAL(EOPNOTSUPP));
#ifdef EPFNOSUPPORT
    defineNativeProperty(vm, &klass->properties, "EPFNOSUPPORT", NUMBER_VAL(EPFNOSUPPORT));
#endif
    defineNativeProperty(vm, &klass->properties, "EAFNOSUPPORT", NUMBER_VAL(EAFNOSUPPORT));
    defineNativeProperty(vm, &klass->properties, "EADDRINUSE", NUMBER_VAL(EADDRINUSE));
    defineNativeProperty(vm, &klass->properties, "EADDRNOTAVAIL", NUMBER_VAL(EADDRNOTAVAIL));
    defineNativeProperty(vm, &klass->properties, "ENETDOWN", NUMBER_VAL(ENETDOWN));
    defineNativeProperty(vm, &klass->properties, "ENETUNREACH", NUMBER_VAL(ENETUNREACH));
    defineNativeProperty(vm, &klass->properties, "ENETRESET", NUMBER_VAL(ENETRESET));
    defineNativeProperty(vm, &klass->properties, "ECONNABORTED", NUMBER_VAL(ECONNABORTED));
    defineNativeProperty(vm, &klass->properties, "ECONNRESET", NUMBER_VAL(ECONNRESET));
    defineNativeProperty(vm, &klass->properties, "ENOBUFS", NUMBER_VAL(ENOBUFS));
    defineNativeProperty(vm, &klass->properties, "EISCONN", NUMBER_VAL(EISCONN));
    defineNativeProperty(vm, &klass->properties, "ENOTCONN", NUMBER_VAL(ENOTCONN));
#ifdef ESHUTDOWN
    defineNativeProperty(vm, &klass->properties, "ESHUTDOWN", NUMBER_VAL(ESHUTDOWN));
#endif
#ifdef ETOOMANYREFS
    defineNativeProperty(vm, &klass->properties, "ETOOMANYREFS", NUMBER_VAL(ETOOMANYREFS));
#endif
    defineNativeProperty(vm, &klass->properties, "ETIMEDOUT", NUMBER_VAL(ETIMEDOUT));
    defineNativeProperty(vm, &klass->properties, "ECONNREFUSED", NUMBER_VAL(ECONNREFUSED));
#ifdef EHOSTDOWN
    defineNativeProperty(vm, &klass->properties, "EHOSTDOWN", NUMBER_VAL(EHOSTDOWN));
#endif
    defineNativeProperty(vm, &klass->properties, "EHOSTUNREACH", NUMBER_VAL(EHOSTUNREACH));
    defineNativeProperty(vm, &klass->properties, "EALREADY", NUMBER_VAL(EALREADY));
    defineNativeProperty(vm, &klass->properties, "EINPROGRESS", NUMBER_VAL(EINPROGRESS));
    defineNativeProperty(vm, &klass->properties, "ESTALE", NUMBER_VAL(ESTALE));
#ifdef EUCLEAN
    defineNativeProperty(vm, &klass->properties, "EUCLEAN", NUMBER_VAL(EUCLEAN));
#endif
#ifdef ENOTNAM
    defineNativeProperty(vm, &klass->properties, "ENOTNAM", NUMBER_VAL(ENOTNAM));
#endif
#ifdef ENAVAIL
    defineNativeProperty(vm, &klass->properties, "ENAVAIL", NUMBER_VAL(ENAVAIL));
#endif
#ifdef EISNAM
    defineNativeProperty(vm, &klass->properties, "EISNAM", NUMBER_VAL(EISNAM));
#endif
#ifdef EREMOTEIO
    defineNativeProperty(vm, &klass->properties, "EREMOTEIO", NUMBER_VAL(EREMOTEIO));
#endif
    defineNativeProperty(vm, &klass->properties, "EDQUOT", NUMBER_VAL(EDQUOT));
#ifdef ENOMEDIUM
    defineNativeProperty(vm, &klass->properties, "ENOMEDIUM", NUMBER_VAL(ENOMEDIUM));
#endif
#ifdef EMEDIUMTYPE
    defineNativeProperty(vm, &klass->properties, "EMEDIUMTYPE", NUMBER_VAL(EMEDIUMTYPE));
#endif
    defineNativeProperty(vm, &klass->properties, "ECANCELED", NUMBER_VAL(ECANCELED));
#ifdef ENOKEY
    defineNativeProperty(vm, &klass->properties, "ENOKEY", NUMBER_VAL(ENOKEY));
#endif
#ifdef EKEYEXPIRED
    defineNativeProperty(vm, &klass->properties, "EKEYEXPIRED", NUMBER_VAL(EKEYEXPIRED));
#endif
#ifdef EKEYREVOKED
    defineNativeProperty(vm, &klass->properties, "EKEYREVOKED", NUMBER_VAL(EKEYREVOKED));
#endif
#ifdef EKEYREJECTED
    defineNativeProperty(vm, &klass->properties, "EKEYREJECTED", NUMBER_VAL(EKEYREJECTED));
#endif
    defineNativeProperty(vm, &klass->properties, "EOWNERDEAD", NUMBER_VAL(EOWNERDEAD));
    defineNativeProperty(vm, &klass->properties, "ENOTRECOVERABLE", NUMBER_VAL(ENOTRECOVERABLE));
#ifdef ERFKILL
    defineNativeProperty(vm, &klass->properties, "ERFKILL", NUMBER_VAL(ERFKILL));
#endif
#ifdef EHWPOISON
    defineNativeProperty(vm, &klass->properties, "EHWPOISON", NUMBER_VAL(EHWPOISON));
#endif

    tableSet(vm, &vm->globals, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}


    /* 23: env.c */

static Value env_get(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "env_get() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "env_get() argument must be a string.");
        return EMPTY_VAL;
    }

    char *value = getenv(AS_CSTRING(args[0]));

    if (value != NULL) {
        return OBJ_VAL(copyString(vm, value, strlen(value)));
    }

    /* getenv() doesn't set errno, so we provide an own error */
    errno = EINVAL; /* EINVAL seems appropriate */

    SET_ERRNO(GET_SELF_CLASS);

    return NIL_VAL;
}

static Value set(VM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "set() takes 2 arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0]) || (!IS_STRING(args[1]) && !IS_NIL(args[1]))) {
        runtimeError(vm, "set() arguments must be a string or nil.");
        return EMPTY_VAL;
    }

    char *key = AS_CSTRING(args[0]);

    int retval;
    if (IS_NIL(args[1])) {
        retval = unsetenv(key);
    } else {
        retval = setenv(key, AS_CSTRING(args[1]), 1);
    }

    /* both set errno, though probably they can not fail */
    if (retval == NOTOK) {
        SET_ERRNO(GET_SELF_CLASS);
    }

    return NUMBER_VAL(retval == 0 ? OK : NOTOK);
}

void createEnvClass(VM *vm) {
    ObjString *name = copyString(vm, "Env", 3);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define Env methods
     */
    defineNative(vm, &klass->methods, "strerror", strerrorNative);
    defineNative(vm, &klass->methods, "get", env_get);
    defineNative(vm, &klass->methods, "set", set);

    tableSet(vm, &vm->globals, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}
#ifndef DISABLE_HTTP


    /* 24: http.c */

static Value strerrorHttpNative(VM *vm, int argCount, Value *args) {
    if (argCount > 1) {
        runtimeError(vm, "strerror() takes either 0 or 1 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    int error;
    if (argCount == 1) {
        error = AS_NUMBER(args[0]);
    } else {
        error = AS_NUMBER(GET_ERRNO(GET_SELF_CLASS));
    }

    char *error_string = (char *) curl_easy_strerror(error);
    return OBJ_VAL(copyString(vm, error_string, strlen(error_string)));
}

static void createResponse(VM *vm, Response *response) {
    response->vm = vm;
    response->headers = initList(vm);
    // Push to stack to avoid GC
    push(vm, OBJ_VAL(response->headers));

    response->len = 0;
    response->res = NULL;
}

static size_t writeResponse(char *ptr, size_t size, size_t nmemb, void *data)
{
    Response *response = (Response *) data;
    size_t new_len = response->len + size * nmemb;
    response->res = GROW_ARRAY(response->vm, response->res, char, response->len, new_len + 1);
    if (response->res == NULL) {
        printf("Unable to allocate memory\n");
        exit(71);
    }
    memcpy(response->res + response->len, ptr, size * nmemb);
    response->res[new_len] = '\0';
    response->len = new_len;

    return size * nmemb;
}

static size_t writeHeaders(char *ptr, size_t size, size_t nitems, void *data)
{
    Response *response = (Response *) data;
    // if nitems equals 2 its an empty header
    if (nitems != 2) {
        Value header = OBJ_VAL(copyString(response->vm, ptr, (nitems - 2) * size));
        // Push to stack to avoid GC
        push(response->vm, header);
        writeValueArray(response->vm, &response->headers->values, header);
        pop(response->vm);
    }
    return size * nitems;
}

static char *dictToPostArgs(ObjDict *dict) {
    int len = 100;
    char *ret = malloc(sizeof(char) * len);
    int currentLen = 0;

    for (int i = 0; i <= dict->capacityMask; i++) {
        DictItem *entry = &dict->entries[i];
        if (IS_EMPTY(entry->key)) {
            continue;
        }

        char *key;
        if (IS_STRING(entry->key)) {
            key = AS_CSTRING(entry->key);
        } else {
            key = valueToString(entry->key);
        }

        char *value;
        if (IS_STRING(entry->value)) {
            value = AS_CSTRING(entry->value);
        } else {
            value = valueToString(entry->value);
        }

        int keyLen = strlen(key);
        int valLen = strlen(value);

        if (currentLen + keyLen + valLen > len) {
            len = len * 2 + keyLen + valLen;
            ret = realloc(ret, len);

            if (ret == NULL) {
                printf("Unable to allocate memory\n");
                exit(71);
            }
        }

        memcpy(ret + currentLen, key, keyLen);
        currentLen += keyLen;
        memcpy(ret + currentLen, "=", 1);
        currentLen += 1;
        memcpy(ret + currentLen, value, valLen);
        currentLen += valLen;
        memcpy(ret + currentLen, "&", 1);
        currentLen += 1;

        if (!IS_STRING(entry->key)) {
            free(key);
        }
        if (!IS_STRING(entry->value)) {
            free(value);
        }
    }

    ret[currentLen] = '\0';

    return ret;
}

static ObjDict* endRequest(VM *vm, CURL *curl, Response response) {
    // Get status code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    ObjString *content = takeString(vm, response.res, response.len);

    // Push to stack to avoid GC
    push(vm, OBJ_VAL(content));

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    ObjDict *responseVal = initDict(vm);
    // Push to stack to avoid GC
    push(vm, OBJ_VAL(responseVal));

    ObjString *string = copyString(vm, "content", 7);
    push(vm, OBJ_VAL(string));
    dictSet(vm, responseVal, OBJ_VAL(string), OBJ_VAL(content));
    pop(vm);

    string = copyString(vm, "headers", 7);
    push(vm, OBJ_VAL(string));
    dictSet(vm, responseVal, OBJ_VAL(string), OBJ_VAL(response.headers));
    pop(vm);

    string = copyString(vm, "statusCode", 10);
    push(vm, OBJ_VAL(string));
    dictSet(vm, responseVal, OBJ_VAL(string), NUMBER_VAL(response.statusCode));
    pop(vm);

    // Pop
    pop(vm);
    pop(vm);
    pop(vm);

    return responseVal;
}

static Value get(VM *vm, int argCount, Value *args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError(vm, "get() takes 1 or 2 arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    long timeout = 20;

    if (argCount == 2) {
        if (!IS_NUMBER(args[1])) {
            runtimeError(vm, "Timeout passed to get() must be a number.");
            return EMPTY_VAL;
        }

        timeout = AS_NUMBER(args[1]);
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "URL passed to get() must be a string.");
        return EMPTY_VAL;
    }

    CURL *curl;
    CURLcode curlResponse;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        Response response;
        createResponse(vm, &response);
        char *url = AS_CSTRING(args[0]);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeResponse);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeaders);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);

        /* Perform the request, res will get the return code */
        curlResponse = curl_easy_perform(curl);

        /* Check for errors */
        if (curlResponse != CURLE_OK) {
            errno = curlResponse;
            SET_ERRNO(GET_SELF_CLASS);
            return NIL_VAL;
        }

        return OBJ_VAL(endRequest(vm, curl, response));
    }

    errno = CURLE_FAILED_INIT;
    SET_ERRNO(GET_SELF_CLASS);
    return NIL_VAL;
}

static Value post(VM *vm, int argCount, Value *args) {
    if (argCount != 1 && argCount != 2 && argCount != 3) {
        runtimeError(vm, "post() takes between 1 and 3 arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    long timeout = 20;
    ObjDict *dict = NULL;

    if (argCount == 3) {
        if (!IS_NUMBER(args[2])) {
            runtimeError(vm, "Timeout passed to post() must be a number.");
            return EMPTY_VAL;
        }

        if (!IS_DICT(args[1])) {
            runtimeError(vm, "Post values passed to post() must be a dictionary.");
            return EMPTY_VAL;
        }

        timeout = (long)AS_NUMBER(args[2]);
        dict = AS_DICT(args[1]);
    } else if (argCount == 2) {
        if (!IS_DICT(args[1])) {
            runtimeError(vm, "Post values passed to post() must be a dictionary.");
            return EMPTY_VAL;
        }

        dict = AS_DICT(args[1]);
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "URL passed to post() must be a string.");
        return EMPTY_VAL;
    }

    CURL *curl;
    CURLcode curlResponse;

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl) {
        Response response;
        createResponse(vm, &response);
        char *url = AS_CSTRING(args[0]);
        char *postValue = "";

        if (dict != NULL) {
            postValue = dictToPostArgs(dict);
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postValue);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeResponse);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeaders);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);

        /* Perform the request, res will get the return code */
        curlResponse = curl_easy_perform(curl);

        if (dict != NULL) {
            free(postValue);
        }

        if (curlResponse != CURLE_OK) {
            errno = curlResponse;
            SET_ERRNO(GET_SELF_CLASS);
            return NIL_VAL;
        }

        return OBJ_VAL(endRequest(vm, curl, response));
    }

    errno = CURLE_FAILED_INIT;
    SET_ERRNO(GET_SELF_CLASS);
    return NIL_VAL;
}

void createHTTPClass(VM *vm) {
    ObjString *name = copyString(vm, "HTTP", 4);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define Http methods
     */
    defineNative(vm, &klass->methods, "strerror", strerrorHttpNative);
    defineNative(vm, &klass->methods, "get", get);
    defineNative(vm, &klass->methods, "post", post);

    /**
     * Define Http properties
     */
    defineNativeProperty(vm, &klass->properties, "errno", NUMBER_VAL(0));

    tableSet(vm, &vm->globals, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}

#endif /* DISABLE_HTTP */

    /* 25: jsonParseLib.c */
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


#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
      #define _CRT_SECURE_NO_WARNINGS
   #endif
   #include <stdint.h>
#endif

#define UNUSED(__x__) (void) __x__

const struct _json_value json_value_none;


typedef unsigned int json_l_uchar;

/* There has to be a better way to do this */
static const json_int_t JSON_INT_MAX = sizeof(json_int_t) == 1
                                       ? INT8_MAX
                                       : (sizeof(json_int_t) == 2
                                          ? INT16_MAX
                                          : (sizeof(json_int_t) == 4
                                             ? INT32_MAX
                                             : INT64_MAX));

static unsigned char hex_value (json_char c)
{
    if (isdigit(c))
        return c - '0';

    switch (c) {
        case 'a': case 'A': return 0x0A;
        case 'b': case 'B': return 0x0B;
        case 'c': case 'C': return 0x0C;
        case 'd': case 'D': return 0x0D;
        case 'e': case 'E': return 0x0E;
        case 'f': case 'F': return 0x0F;
        default: return 0xFF;
    }
}

static int would_overflow (json_int_t value, json_char b)
{
    return ((JSON_INT_MAX - (b - '0')) / 10 ) < value;
}

typedef struct
{
    unsigned long used_memory;

    unsigned int uint_max;
    unsigned long ulong_max;

    json_settings settings;
    int first_pass;

    const json_char * ptr;
    unsigned int cur_line, cur_col;

} json_state;

static void * default_alloc (size_t size, int zero, void * user_data)
{
    UNUSED(user_data);

    return zero ? calloc (1, size) : malloc (size);
}

static void default_free (void * ptr, void * user_data)
{
    UNUSED(user_data);

    free (ptr);
}

static void * json_alloc (json_state * state, unsigned long size, int zero)
{
    if ((state->ulong_max - state->used_memory) < size)
        return 0;

    if (state->settings.max_memory
        && (state->used_memory += size) > state->settings.max_memory)
    {
        return 0;
    }

    return state->settings.mem_alloc (size, zero, state->settings.user_data);
}

static int new_value (json_state * state,
                      json_value ** top, json_value ** root, json_value ** alloc,
                      json_type type)
{
    json_value * value;

    if (!state->first_pass)
    {
        value = *top = *alloc;
        *alloc = (*alloc)->_reserved.next_alloc;

        if (!*root)
            *root = value;

        switch (value->type)
        {
            case json_array:

                if (value->u.array.length == 0)
                    break;

                if (! (value->u.array.values = (json_value **) json_alloc
                        (state, value->u.array.length * sizeof (json_value *), 0)) )
                {
                    return 0;
                }

                value->u.array.length = 0;
                break;

            case json_object:

                if (value->u.object.length == 0)
                    break;

                int values_size = sizeof (*value->u.object.values) * value->u.object.length;

                if (! (value->u.object.values = (json_object_entry *) json_alloc
                        (state, values_size + ((unsigned long) value->u.object.values), 0)) )
                {
                    return 0;
                }

                value->_reserved.object_mem = ((char *)*(json_object_entry**) &value->u.object.values) + values_size;

                value->u.object.length = 0;
                break;

            case json_string:

                if (! (value->u.string.ptr = (json_char *) json_alloc
                        (state, (value->u.string.length + 1) * sizeof (json_char), 0)) )
                {
                    return 0;
                }

                value->u.string.length = 0;
                break;

            default:
                break;
        };

        return 1;
    }

    if (! (value = (json_value *) json_alloc
            (state, sizeof (json_value) + state->settings.value_extra, 1)))
    {
        return 0;
    }

    if (!*root)
        *root = value;

    value->type = type;
    value->parent = *top;

#ifdef JSON_TRACK_SOURCE
    value->line = state->cur_line;
      value->col = state->cur_col;
#endif

    if (*alloc)
        (*alloc)->_reserved.next_alloc = value;

    *alloc = *top = value;

    return 1;
}

#define whitespace \
   case '\n': ++ state.cur_line;  state.cur_col = 0; /* FALLTHRU */ \
   case ' ': /* FALLTHRU */ case '\t': /* FALLTHRU */ case '\r' /* FALLTHRU */

#define string_add(b)  \
   do { if (!state.first_pass) string [string_length] = b;  ++ string_length; } while (0);

#define line_and_col \
   state.cur_line, state.cur_col

static const long
        flag_next             = 1 << 0,
        flag_reproc           = 1 << 1,
        flag_need_comma       = 1 << 2,
        flag_seek_value       = 1 << 3,
        flag_escaped          = 1 << 4,
        flag_string           = 1 << 5,
        flag_need_colon       = 1 << 6,
        flag_done             = 1 << 7,
        flag_num_negative     = 1 << 8,
        flag_num_zero         = 1 << 9,
        flag_num_e            = 1 << 10,
        flag_num_e_got_sign   = 1 << 11,
        flag_num_e_negative   = 1 << 12,
        flag_line_comment     = 1 << 13,
        flag_block_comment    = 1 << 14,
        flag_num_got_decimal  = 1 << 15;

json_value * json_parse_ex (json_settings * settings,
                            const json_char * json,
                            size_t length,
                            char * error_buf)
{
    json_char error [json_error_max];
    const json_char * end;
    json_value * top, * root, * alloc = 0;
    json_state state = { 0 };
    double num_digits = 0, num_e = 0;
    double num_fraction = 0;

    /* Skip UTF-8 BOM
     */
    if (length >= 3 && ((unsigned char) json [0]) == 0xEF
        && ((unsigned char) json [1]) == 0xBB
        && ((unsigned char) json [2]) == 0xBF)
    {
        json += 3;
        length -= 3;
    }

    error[0] = '\0';
    end = (json + length);

    memcpy (&state.settings, settings, sizeof (json_settings));

    if (!state.settings.mem_alloc)
        state.settings.mem_alloc = default_alloc;

    if (!state.settings.mem_free)
        state.settings.mem_free = default_free;

    memset (&state.uint_max, 0xFF, sizeof (state.uint_max));
    memset (&state.ulong_max, 0xFF, sizeof (state.ulong_max));

    state.uint_max -= 8; /* limit of how much can be added before next check */
    state.ulong_max -= 8;

    for (state.first_pass = 1; state.first_pass >= 0; -- state.first_pass)
    {
        json_l_uchar l_uchar;
        unsigned char uc_b1, uc_b2, uc_b3, uc_b4;
        json_char * string = 0;
        unsigned int string_length = 0;

        top = root = 0;
        long flags = flag_seek_value;

        state.cur_line = 1;

        for (state.ptr = json ;; ++ state.ptr)
        {
            json_char b = (state.ptr == end ? 0 : *state.ptr);

            if (flags & flag_string)
            {
                if (!b)
                {  sprintf (error, "Unexpected EOF in string (at %u:%u)", line_and_col);
                    goto e_failed;
                }

                if (string_length > state.uint_max)
                    goto e_overflow;

                if (flags & flag_escaped)
                {
                    flags &= ~ flag_escaped;

                    switch (b)
                    {
                        case 'b':  string_add ('\b');  break;
                        case 'f':  string_add ('\f');  break;
                        case 'n':  string_add ('\n');  break;
                        case 'r':  string_add ('\r');  break;
                        case 't':  string_add ('\t');  break;
                        case 'u':

                            if (end - state.ptr <= 4 ||
                                (uc_b1 = hex_value (*++ state.ptr)) == 0xFF ||
                                (uc_b2 = hex_value (*++ state.ptr)) == 0xFF ||
                                (uc_b3 = hex_value (*++ state.ptr)) == 0xFF ||
                                (uc_b4 = hex_value (*++ state.ptr)) == 0xFF)
                            {
                                sprintf (error, "Invalid character value `%c` (at %u:%u)", b, line_and_col);
                                goto e_failed;
                            }

                            uc_b1 = (uc_b1 << 4) | uc_b2;
                            uc_b2 = (uc_b3 << 4) | uc_b4;
                            l_uchar = (uc_b1 << 8) | uc_b2;

                            if ((l_uchar & 0xF800) == 0xD800) {
                                json_l_uchar l_uchar2;

                                if (end - state.ptr <= 6 || (*++ state.ptr) != '\\' || (*++ state.ptr) != 'u' ||
                                    (uc_b1 = hex_value (*++ state.ptr)) == 0xFF ||
                                    (uc_b2 = hex_value (*++ state.ptr)) == 0xFF ||
                                    (uc_b3 = hex_value (*++ state.ptr)) == 0xFF ||
                                    (uc_b4 = hex_value (*++ state.ptr)) == 0xFF)
                                {
                                    sprintf (error, "Invalid character value `%c` (at %u:%u)", b, line_and_col);
                                    goto e_failed;
                                }

                                uc_b1 = (uc_b1 << 4) | uc_b2;
                                uc_b2 = (uc_b3 << 4) | uc_b4;
                                l_uchar2 = (uc_b1 << 8) | uc_b2;

                                l_uchar = 0x010000 | ((l_uchar & 0x3FF) << 10) | (l_uchar2 & 0x3FF);
                            }

                            if (sizeof (json_char) >= sizeof (json_l_uchar) || (l_uchar <= 0x7F))
                            {
                                string_add ((json_char) l_uchar);
                                break;
                            }

                            if (l_uchar <= 0x7FF)
                            {
                                if (state.first_pass)
                                    string_length += 2;
                                else
                                {  string [string_length ++] = 0xC0 | (l_uchar >> 6);
                                    string [string_length ++] = 0x80 | (l_uchar & 0x3F);
                                }

                                break;
                            }

                            if (l_uchar <= 0xFFFF) {
                                if (state.first_pass)
                                    string_length += 3;
                                else
                                {  string [string_length ++] = 0xE0 | (l_uchar >> 12);
                                    string [string_length ++] = 0x80 | ((l_uchar >> 6) & 0x3F);
                                    string [string_length ++] = 0x80 | (l_uchar & 0x3F);
                                }

                                break;
                            }

                            if (state.first_pass)
                                string_length += 4;
                            else
                            {  string [string_length ++] = 0xF0 | (l_uchar >> 18);
                                string [string_length ++] = 0x80 | ((l_uchar >> 12) & 0x3F);
                                string [string_length ++] = 0x80 | ((l_uchar >> 6) & 0x3F);
                                string [string_length ++] = 0x80 | (l_uchar & 0x3F);
                            }

                            break;

                        default:
                            string_add (b);
                    };

                    continue;
                }

                if (b == '\\')
                {
                    flags |= flag_escaped;
                    continue;
                }

                if (b == '"')
                {
                    if (!state.first_pass)
                        string [string_length] = 0;

                    flags &= ~ flag_string;
                    string = 0;

                    switch (top->type)
                    {
                        case json_string:

                            top->u.string.length = string_length;
                            flags |= flag_next;

                            break;

                        case json_object:

                            if (state.first_pass)
                                (*(json_object_entry**) &top->u.object.values) += string_length + 1;
                            else
                            {
                                top->u.object.values [top->u.object.length].name
                                        = (json_char *) top->_reserved.object_mem;

                                top->u.object.values [top->u.object.length].name_length
                                        = string_length;

                                (*(json_char **) &top->_reserved.object_mem) += string_length + 1;
                            }

                            flags |= flag_seek_value | flag_need_colon;
                            continue;

                        default:
                            break;
                    };
                }
                else
                {
                    string_add (b);
                    continue;
                }
            }

            if (state.settings.settings & json_enable_comments)
            {
                if (flags & (flag_line_comment | flag_block_comment))
                {
                    if (flags & flag_line_comment)
                    {
                        if (b == '\r' || b == '\n' || !b)
                        {
                            flags &= ~ flag_line_comment;
                            -- state.ptr;  /* so null can be reproc'd */
                        }

                        continue;
                    }

                    if (flags & flag_block_comment)
                    {
                        if (!b)
                        {  sprintf (error, "%u:%u: Unexpected EOF in block comment", line_and_col);
                            goto e_failed;
                        }

                        if (b == '*' && state.ptr < (end - 1) && state.ptr [1] == '/')
                        {
                            flags &= ~ flag_block_comment;
                            ++ state.ptr;  /* skip closing sequence */
                        }

                        continue;
                    }
                }
                else if (b == '/')
                {
                    if (! (flags & (flag_seek_value | flag_done)) && top->type != json_object)
                    {  sprintf (error, "%u:%u: Comment not allowed here", line_and_col);
                        goto e_failed;
                    }

                    if (++ state.ptr == end)
                    {  sprintf (error, "%u:%u: EOF unexpected", line_and_col);
                        goto e_failed;
                    }

                    switch (b = *state.ptr)
                    {
                        case '/':
                            flags |= flag_line_comment;
                            continue;

                        case '*':
                            flags |= flag_block_comment;
                            continue;

                        default:
                            sprintf (error, "%u:%u: Unexpected `%c` in comment opening sequence", line_and_col, b);
                            goto e_failed;
                    };
                }
            }

            if (flags & flag_done)
            {
                if (!b)
                    break;

                switch (b)
                {
                    whitespace:
                        continue;

                    default:
                        sprintf (error, "%u:%u: Trailing garbage: `%c`",line_and_col, b);
                        goto e_failed;
                };
            }

            if (flags & flag_seek_value)
            {
                switch (b)
                {
                    whitespace:
                        continue;

                    case ']':

                        if (top && top->type == json_array)
                            flags = (flags & ~ (flag_need_comma | flag_seek_value)) | flag_next;
                        else
                        {  sprintf (error, "%u:%u: Unexpected ]", line_and_col);
                            goto e_failed;
                        }

                        break;

                    default:

                        if (flags & flag_need_comma)
                        {
                            if (b == ',')
                            {  flags &= ~ flag_need_comma;
                                continue;
                            }
                            else
                            {
                                sprintf (error, "%u:%u: Expected , before %c", line_and_col, b);
                                goto e_failed;
                            }
                        }

                        if (flags & flag_need_colon)
                        {
                            if (b == ':')
                            {  flags &= ~ flag_need_colon;
                                continue;
                            }
                            else
                            {
                                sprintf (error, "%u:%u: Expected : before %c", line_and_col, b);
                                goto e_failed;
                            }
                        }

                        flags &= ~ flag_seek_value;

                        switch (b)
                        {
                            case '{':

                                if (!new_value (&state, &top, &root, &alloc, json_object))
                                    goto e_alloc_failure;

                                continue;

                            case '[':

                                if (!new_value (&state, &top, &root, &alloc, json_array))
                                    goto e_alloc_failure;

                                flags |= flag_seek_value;
                                continue;

                            case '"':

                                if (!new_value (&state, &top, &root, &alloc, json_string))
                                    goto e_alloc_failure;

                                flags |= flag_string;

                                string = top->u.string.ptr;
                                string_length = 0;

                                continue;

                            case 't':

                                if ((end - state.ptr) < 3 || *(++ state.ptr) != 'r' ||
                                    *(++ state.ptr) != 'u' || *(++ state.ptr) != 'e')
                                {
                                    goto e_unknown_value;
                                }

                                if (!new_value (&state, &top, &root, &alloc, json_boolean))
                                    goto e_alloc_failure;

                                top->u.boolean = 1;

                                flags |= flag_next;
                                break;

                            case 'f':

                                if ((end - state.ptr) < 4 || *(++ state.ptr) != 'a' ||
                                    *(++ state.ptr) != 'l' || *(++ state.ptr) != 's' ||
                                    *(++ state.ptr) != 'e')
                                {
                                    goto e_unknown_value;
                                }

                                if (!new_value (&state, &top, &root, &alloc, json_boolean))
                                    goto e_alloc_failure;

                                flags |= flag_next;
                                break;

                            case 'n':

                                if ((end - state.ptr) < 3 || *(++ state.ptr) != 'u' ||
                                    *(++ state.ptr) != 'l' || *(++ state.ptr) != 'l')
                                {
                                    goto e_unknown_value;
                                }

                                if (!new_value (&state, &top, &root, &alloc, json_null))
                                    goto e_alloc_failure;

                                flags |= flag_next;
                                break;

                            default:

                                if (isdigit (b) || b == '-')
                                {
                                    if (!new_value (&state, &top, &root, &alloc, json_integer))
                                        goto e_alloc_failure;

                                    if (!state.first_pass)
                                    {
                                        while (isdigit (b) || b == '+' || b == '-'
                                               || b == 'e' || b == 'E' || b == '.')
                                        {
                                            if ( (++ state.ptr) == end)
                                            {
                                                b = 0;
                                                break;
                                            }

                                            b = *state.ptr;
                                        }

                                        flags |= flag_next | flag_reproc;
                                        break;
                                    }

                                    flags &= ~ (flag_num_negative | flag_num_e |
                                                flag_num_e_got_sign | flag_num_e_negative |
                                                flag_num_zero);

                                    num_digits = 0;
                                    num_fraction = 0;
                                    num_e = 0;

                                    if (b != '-')
                                    {
                                        flags |= flag_reproc;
                                        break;
                                    }

                                    flags |= flag_num_negative;
                                    continue;
                                }
                                else
                                {  sprintf (error, "%u:%u: Unexpected %c when seeking value", line_and_col, b);
                                    goto e_failed;
                                }
                        };
                };
            }
            else
            {
                switch (top->type)
                {
                    case json_object:

                        switch (b)
                        {
                            whitespace:
                                continue;

                            case '"':

                                if (flags & flag_need_comma)
                                {  sprintf (error, "%u:%u: Expected , before \"", line_and_col);
                                    goto e_failed;
                                }

                                flags |= flag_string;

                                string = (json_char *) top->_reserved.object_mem;
                                string_length = 0;

                                break;

                            case '}':

                                flags = (flags & ~ flag_need_comma) | flag_next;
                                break;

                            case ',':

                                if (flags & flag_need_comma)
                                {
                                    flags &= ~ flag_need_comma;
                                    break;
                                } /* FALLTHRU */

                            default:
                                sprintf (error, "%u:%u: Unexpected `%c` in object", line_and_col, b);
                                goto e_failed;
                        };

                        break;

                    case json_integer:
                    case json_double:

                        if (isdigit (b))
                        {
                            ++ num_digits;

                            if (top->type == json_integer || flags & flag_num_e)
                            {
                                if (! (flags & flag_num_e))
                                {
                                    if (flags & flag_num_zero)
                                    {  sprintf (error, "%u:%u: Unexpected `0` before `%c`", line_and_col, b);
                                        goto e_failed;
                                    }

                                    if (num_digits == 1 && b == '0')
                                        flags |= flag_num_zero;
                                }
                                else
                                {
                                    flags |= flag_num_e_got_sign;
                                    num_e = (num_e * 10) + (b - '0');
                                    continue;
                                }

                                if (would_overflow(top->u.integer, b))
                                {  -- num_digits;
                                    -- state.ptr;
                                    top->type = json_double;
                                    top->u.dbl = (double)top->u.integer;
                                    continue;
                                }

                                top->u.integer = (top->u.integer * 10) + (b - '0');
                                continue;
                            }

                            if (flags & flag_num_got_decimal)
                                num_fraction = (num_fraction * 10) + (b - '0');
                            else
                                top->u.dbl = (top->u.dbl * 10) + (b - '0');

                            continue;
                        }

                        if (b == '+' || b == '-')
                        {
                            if ( (flags & flag_num_e) && !(flags & flag_num_e_got_sign))
                            {
                                flags |= flag_num_e_got_sign;

                                if (b == '-')
                                    flags |= flag_num_e_negative;

                                continue;
                            }
                        }
                        else if (b == '.' && top->type == json_integer)
                        {
                            if (!num_digits)
                            {  sprintf (error, "%u:%u: Expected digit before `.`", line_and_col);
                                goto e_failed;
                            }

                            top->type = json_double;
                            top->u.dbl = (double) top->u.integer;

                            flags |= flag_num_got_decimal;
                            num_digits = 0;
                            continue;
                        }

                        if (! (flags & flag_num_e))
                        {
                            if (top->type == json_double)
                            {
                                if (!num_digits)
                                {  sprintf (error, "%u:%u: Expected digit after `.`", line_and_col);
                                    goto e_failed;
                                }

                                top->u.dbl += num_fraction / pow (10.0, num_digits);
                            }

                            if (b == 'e' || b == 'E')
                            {
                                flags |= flag_num_e;

                                if (top->type == json_integer)
                                {
                                    top->type = json_double;
                                    top->u.dbl = (double) top->u.integer;
                                }

                                num_digits = 0;
                                flags &= ~ flag_num_zero;

                                continue;
                            }
                        }
                        else
                        {
                            if (!num_digits)
                            {  sprintf (error, "%u:%u: Expected digit after `e`", line_and_col);
                                goto e_failed;
                            }

                            top->u.dbl *= pow (10.0, ((flags & flag_num_e_negative) ? - num_e : num_e));
                        }

                        if (flags & flag_num_negative)
                        {
                            if (top->type == json_integer)
                                top->u.integer = - top->u.integer;
                            else
                                top->u.dbl = - top->u.dbl;
                        }

                        flags |= flag_next | flag_reproc;
                        break;

                    default:
                        break;
                };
            }

            if (flags & flag_reproc)
            {
                flags &= ~ flag_reproc;
                -- state.ptr;
            }

            if (flags & flag_next)
            {
                flags = (flags & ~ flag_next) | flag_need_comma;

                if (!top->parent)
                {
                    /* root value done */

                    flags |= flag_done;
                    continue;
                }

                if (top->parent->type == json_array)
                    flags |= flag_seek_value;

                if (!state.first_pass)
                {
                    json_value * parent = top->parent;

                    switch (parent->type)
                    {
                        case json_object:

                            parent->u.object.values
                            [parent->u.object.length].value = top;

                            break;

                        case json_array:

                            parent->u.array.values
                            [parent->u.array.length] = top;

                            break;

                        default:
                            break;
                    };
                }

                if ( (++ top->parent->u.array.length) > state.uint_max)
                    goto e_overflow;

                top = top->parent;

                continue;
            }
        }

        alloc = root;
    }

    return root;

    e_unknown_value:

    sprintf (error, "%u:%u: Unknown value", line_and_col);
    goto e_failed;

    e_alloc_failure:

    strcpy (error, "Memory allocation failure");
    goto e_failed;

    e_overflow:

    sprintf (error, "%u:%u: Too long (caught overflow)", line_and_col);
    goto e_failed;

    e_failed:

    if (error_buf)
    {
        if (*error)
            strcpy (error_buf, error);
        else
            strcpy (error_buf, "Unknown error");
    }

    if (state.first_pass)
        alloc = root;

    while (alloc)
    {
        top = alloc->_reserved.next_alloc;
        state.settings.mem_free (alloc, state.settings.user_data);
        alloc = top;
    }

    if (!state.first_pass)
        json_value_free_ex (&state.settings, root);

    return 0;
}

json_value * json_parse (const json_char * json, size_t length)
{
    json_settings settings = { 0 };
    return json_parse_ex (&settings, json, length, 0);
}

void json_value_free_ex (json_settings * settings, json_value * value)
{
    json_value * cur_value;

    if (!value)
        return;

    value->parent = 0;

    while (value)
    {
        switch (value->type)
        {
            case json_array:

                if (!value->u.array.length)
                {
                    settings->mem_free (value->u.array.values, settings->user_data);
                    break;
                }

                value = value->u.array.values [-- value->u.array.length];
                continue;

            case json_object:

                if (!value->u.object.length)
                {
                    settings->mem_free (value->u.object.values, settings->user_data);
                    break;
                }

                value = value->u.object.values [-- value->u.object.length].value;
                continue;

            case json_string:

                settings->mem_free (value->u.string.ptr, settings->user_data);
                break;

            default:
                break;
        };

        cur_value = value;
        value = value->parent;
        settings->mem_free (cur_value, settings->user_data);
    }
}

void json_value_free (json_value * value)
{
    json_settings settings = { 0 };
    settings.mem_free = default_free;
    json_value_free_ex (&settings, value);
}

    /* 26: jsonBuilderLib.c */

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



#ifdef _MSC_VER
#define snprintf _snprintf
#endif

static const json_serialize_opts Default_Opts =
        {
                json_serialize_mode_single_line,
                0,
                3  /* indent_size */
        };

typedef struct json_builder_value
{
    json_value value;

    int is_builder_value;

    size_t additional_length_allocated;
    size_t length_iterated;

} json_builder_value;

static int builderize (json_value * value)
{
    if (((json_builder_value *) value)->is_builder_value)
        return 1;

    if (value->type == json_object)
    {
        unsigned int i;

        /* Values straight out of the parser have the names of object entries
         * allocated in the same allocation as the values array itself.  This is
         * not desirable when manipulating values because the names would be easy
         * to clobber.
         */
        for (i = 0; i < value->u.object.length; ++ i)
        {
            json_char * name_copy;
            json_object_entry * entry = &value->u.object.values [i];

            if (! (name_copy = (json_char *) malloc ((entry->name_length + 1) * sizeof (json_char))))
                return 0;

            memcpy (name_copy, entry->name, entry->name_length + 1);
            entry->name = name_copy;
        }
    }

    ((json_builder_value *) value)->is_builder_value = 1;

    return 1;
}

const size_t json_builder_extra = sizeof(json_builder_value) - sizeof(json_value);

/* These flags are set up from the opts before serializing to make the
 * serializer conditions simpler.
 */
const int f_spaces_around_brackets = (1 << 0);
const int f_spaces_after_commas    = (1 << 1);
const int f_spaces_after_colons    = (1 << 2);
const int f_tabs                   = (1 << 3);

static int get_serialize_flags (json_serialize_opts opts)
{
    int flags = 0;

    if (opts.mode == json_serialize_mode_packed)
        return 0;

    if (opts.mode == json_serialize_mode_multiline)
    {
        if (opts.opts & json_serialize_opt_use_tabs)
            flags |= f_tabs;
    }
    else
    {
        if (! (opts.opts & json_serialize_opt_pack_brackets))
            flags |= f_spaces_around_brackets;

        if (! (opts.opts & json_serialize_opt_no_space_after_comma))
            flags |= f_spaces_after_commas;
    }

    if (! (opts.opts & json_serialize_opt_no_space_after_colon))
        flags |= f_spaces_after_colons;

    return flags;
}

json_value * json_array_new (size_t length)
{
    json_value * value = (json_value *) calloc (1, sizeof (json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_array;

    if (! (value->u.array.values = (json_value **) malloc (length * sizeof (json_value *))))
    {
        free (value);
        return NULL;
    }

    ((json_builder_value *) value)->additional_length_allocated = length;

    return value;
}

json_value * json_array_push (json_value * array, json_value * value)
{
    assert (array->type == json_array);

    if (!builderize (array) || !builderize (value))
        return NULL;

    if (((json_builder_value *) array)->additional_length_allocated > 0)
    {
        -- ((json_builder_value *) array)->additional_length_allocated;
    }
    else
    {
        json_value ** values_new = (json_value **) realloc
                (array->u.array.values, sizeof (json_value *) * (array->u.array.length + 1));

        if (!values_new)
            return NULL;

        array->u.array.values = values_new;
    }

    array->u.array.values [array->u.array.length] = value;
    ++ array->u.array.length;

    value->parent = array;

    return value;
}

json_value * json_object_new (size_t length)
{
    json_value * value = (json_value *) calloc (1, sizeof (json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_object;

    if (! (value->u.object.values = (json_object_entry *) calloc
            (length, sizeof (*value->u.object.values))))
    {
        free (value);
        return NULL;
    }

    ((json_builder_value *) value)->additional_length_allocated = length;

    return value;
}

json_value * json_object_push (json_value * object,
                               const json_char * name,
                               json_value * value)
{
    return json_object_push_length (object, strlen (name), name, value);
}

json_value * json_object_push_length (json_value * object,
                                      unsigned int name_length, const json_char * name,
                                      json_value * value)
{
    json_char * name_copy;

    assert (object->type == json_object);

    if (! (name_copy = (json_char *) malloc ((name_length + 1) * sizeof (json_char))))
        return NULL;

    memcpy (name_copy, name, name_length * sizeof (json_char));
    name_copy [name_length] = 0;

    if (!json_object_push_nocopy (object, name_length, name_copy, value))
    {
        free (name_copy);
        return NULL;
    }

    return value;
}

json_value * json_object_push_nocopy (json_value * object,
                                      unsigned int name_length, json_char * name,
                                      json_value * value)
{
    json_object_entry * entry;

    assert (object->type == json_object);

    if (!builderize (object) || !builderize (value))
        return NULL;

    if (((json_builder_value *) object)->additional_length_allocated > 0)
    {
        -- ((json_builder_value *) object)->additional_length_allocated;
    }
    else
    {
        json_object_entry * values_new = (json_object_entry *)
                realloc (object->u.object.values, sizeof (*object->u.object.values)
                                                  * (object->u.object.length + 1));

        if (!values_new)
            return NULL;

        object->u.object.values = values_new;
    }

    entry = object->u.object.values + object->u.object.length;

    entry->name_length = name_length;
    entry->name = name;
    entry->value = value;

    ++ object->u.object.length;

    value->parent = object;

    return value;
}

json_value * json_string_new (const json_char * buf)
{
    return json_string_new_length (strlen (buf), buf);
}

json_value * json_string_new_length (unsigned int length, const json_char * buf)
{
    json_value * value;
    json_char * copy = (json_char *) malloc ((length + 1) * sizeof (json_char));

    if (!copy)
        return NULL;

    memcpy (copy, buf, length * sizeof (json_char));
    copy [length] = 0;

    if (! (value = json_string_new_nocopy (length, copy)))
    {
        free (copy);
        return NULL;
    }

    return value;
}

json_value * json_string_new_nocopy (unsigned int length, json_char * buf)
{
    json_value * value = (json_value *) calloc (1, sizeof (json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_string;
    value->u.string.length = length;
    value->u.string.ptr = buf;

    return value;
}

json_value * json_integer_new (json_int_t integer)
{
    json_value * value = (json_value *) calloc (1, sizeof (json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_integer;
    value->u.integer = integer;

    return value;
}

json_value * json_double_new (double dbl)
{
    json_value * value = (json_value *) calloc (1, sizeof (json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_double;
    value->u.dbl = dbl;

    return value;
}

json_value * json_boolean_new (int b)
{
    json_value * value = (json_value *) calloc (1, sizeof (json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_boolean;
    value->u.boolean = b;

    return value;
}

json_value * json_null_new (void)
{
    json_value * value = (json_value *) calloc (1, sizeof (json_builder_value));

    if (!value)
        return NULL;

    ((json_builder_value *) value)->is_builder_value = 1;

    value->type = json_null;

    return value;
}

void json_object_sort (json_value * object, json_value * proto)
{
    unsigned int i, out_index = 0;

    if (!builderize (object))
        return;  /* TODO error */

    assert (object->type == json_object);
    assert (proto->type == json_object);

    for (i = 0; i < proto->u.object.length; ++ i)
    {
        unsigned int j;
        json_object_entry proto_entry = proto->u.object.values [i];

        for (j = 0; j < object->u.object.length; ++ j)
        {
            json_object_entry entry = object->u.object.values [j];

            if (entry.name_length != proto_entry.name_length)
                continue;

            if (memcmp (entry.name, proto_entry.name, entry.name_length) != 0)
                continue;

            object->u.object.values [j] = object->u.object.values [out_index];
            object->u.object.values [out_index] = entry;

            ++ out_index;
        }
    }
}

json_value * json_object_merge (json_value * objectA, json_value * objectB)
{
    unsigned int i;

    assert (objectA->type == json_object);
    assert (objectB->type == json_object);
    assert (objectA != objectB);

    if (!builderize (objectA) || !builderize (objectB))
        return NULL;

    if (objectB->u.object.length <=
        ((json_builder_value *) objectA)->additional_length_allocated)
    {
        ((json_builder_value *) objectA)->additional_length_allocated
                -= objectB->u.object.length;
    }
    else
    {
        json_object_entry * values_new;

        unsigned int alloc =
                objectA->u.object.length
                + ((json_builder_value *) objectA)->additional_length_allocated
                + objectB->u.object.length;

        if (! (values_new = (json_object_entry *)
                realloc (objectA->u.object.values, sizeof (json_object_entry) * alloc)))
        {
            return NULL;
        }

        objectA->u.object.values = values_new;
    }

    for (i = 0; i < objectB->u.object.length; ++ i)
    {
        json_object_entry * entry = &objectA->u.object.values[objectA->u.object.length + i];

        *entry = objectB->u.object.values[i];
        entry->value->parent = objectA;
    }

    objectA->u.object.length += objectB->u.object.length;

    free (objectB->u.object.values);
    free (objectB);

    return objectA;
}

static size_t measure_string (unsigned int length,
                              const json_char * str)
{
    unsigned int i;
    size_t measured_length = 0;

    for(i = 0; i < length; ++ i)
    {
        json_char c = str [i];

        switch (c)
        {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':

                measured_length += 2;
                break;

            default:

                ++ measured_length;
                break;
        };
    };

    return measured_length;
}

#define PRINT_ESCAPED(c) do {  \
   *buf ++ = '\\';             \
   *buf ++ = (c);              \
} while(0);                    \

static size_t serialize_string (json_char * buf,
                                unsigned int length,
                                const json_char * str)
{
    json_char * orig_buf = buf;
    unsigned int i;

    for(i = 0; i < length; ++ i)
    {
        json_char c = str [i];

        switch (c)
        {
            case '"':   PRINT_ESCAPED ('\"');  continue;
            case '\\':  PRINT_ESCAPED ('\\');  continue;
            case '\b':  PRINT_ESCAPED ('b');   continue;
            case '\f':  PRINT_ESCAPED ('f');   continue;
            case '\n':  PRINT_ESCAPED ('n');   continue;
            case '\r':  PRINT_ESCAPED ('r');   continue;
            case '\t':  PRINT_ESCAPED ('t');   continue;

            default:

                *buf ++ = c;
                break;
        };
    };

    return buf - orig_buf;
}

size_t json_measure (json_value * value)
{
    return json_measure_ex (value, Default_Opts);
}

#define MEASURE_NEWLINE() do {                     \
   ++ newlines;                                    \
   indents += depth;                               \
} while(0);                                        \

size_t json_measure_ex (json_value * value, json_serialize_opts opts)
{
    size_t total = 1;  /* null terminator */
    size_t newlines = 0;
    size_t depth = 0;
    size_t indents = 0;
    int flags;
    int bracket_size, comma_size, colon_size;

    flags = get_serialize_flags (opts);

    /* to reduce branching
     */
    bracket_size = (flags & f_spaces_around_brackets) ? 2 : 1;
    comma_size = (flags & f_spaces_after_commas) ? 2 : 1;
    colon_size = (flags & f_spaces_after_colons) ? 2 : 1;

    while (value)
    {
        json_int_t integer;
        json_object_entry * entry;

        switch (value->type)
        {
            case json_array:

                if (((json_builder_value *) value)->length_iterated == 0)
                {
                    if (value->u.array.length == 0)
                    {
                        total += 2;  /* `[]` */
                        break;
                    }

                    total += bracket_size;  /* `[` */

                    ++ depth;
                    MEASURE_NEWLINE(); /* \n after [ */
                }

                if (((json_builder_value *) value)->length_iterated == value->u.array.length)
                {
                    -- depth;
                    MEASURE_NEWLINE();
                    total += bracket_size;  /* `]` */

                    ((json_builder_value *) value)->length_iterated = 0;
                    break;
                }

                if (((json_builder_value *) value)->length_iterated > 0)
                {
                    total += comma_size;  /* `, ` */

                    MEASURE_NEWLINE();
                }

                ((json_builder_value *) value)->length_iterated++;
                value = value->u.array.values [((json_builder_value *) value)->length_iterated - 1];
                continue;

            case json_object:

                if (((json_builder_value *) value)->length_iterated == 0)
                {
                    if (value->u.object.length == 0)
                    {
                        total += 2;  /* `{}` */
                        break;
                    }

                    total += bracket_size;  /* `{` */

                    ++ depth;
                    MEASURE_NEWLINE(); /* \n after { */
                }

                if (((json_builder_value *) value)->length_iterated == value->u.object.length)
                {
                    -- depth;
                    MEASURE_NEWLINE();
                    total += bracket_size;  /* `}` */

                    ((json_builder_value *) value)->length_iterated = 0;
                    break;
                }

                if (((json_builder_value *) value)->length_iterated > 0)
                {
                    total += comma_size;  /* `, ` */
                    MEASURE_NEWLINE();
                }

                entry = value->u.object.values + (((json_builder_value *) value)->length_iterated ++);

                total += 2 + colon_size;  /* `"": ` */
                total += measure_string (entry->name_length, entry->name);

                value = entry->value;
                continue;

            case json_string:

                total += 2;  /* `""` */
                total += measure_string (value->u.string.length, value->u.string.ptr);
                break;

            case json_integer:

                integer = value->u.integer;

                if (integer < 0)
                {
                    total += 1;  /* `-` */
                    integer = - integer;
                }

                ++ total;  /* first digit */

                while (integer >= 10)
                {
                    ++ total;  /* another digit */
                    integer /= 10;
                }

                break;

            case json_double:

                total += snprintf (NULL, 0, "%g", value->u.dbl);

                /* Because sometimes we need to add ".0" if sprintf does not do it
                 * for us. Downside is that we allocate more bytes than strictly
                 * needed for serialization.
                 */
                total += 2;

                break;

            case json_boolean:

                total += value->u.boolean ?
                         4:  /* `true` */
                         5;  /* `false` */

                break;

            case json_null:

                total += 4;  /* `null` */
                break;

            default:
                break;
        };

        value = value->parent;
    }

    if (opts.mode == json_serialize_mode_multiline)
    {
        total += newlines * (((opts.opts & json_serialize_opt_CRLF) ? 2 : 1) + opts.indent_size);
        total += indents * opts.indent_size;
    }

    return total;
}

void json_serialize (json_char * buf, json_value * value)
{
    json_serialize_ex (buf, value, Default_Opts);
}

#define PRINT_NEWLINE() do {                          \
   if (opts.mode == json_serialize_mode_multiline) {  \
      if (opts.opts & json_serialize_opt_CRLF)        \
         *buf ++ = '\r';                              \
      *buf ++ = '\n';                                 \
      for(i = 0; i < indent; ++ i)                    \
         *buf ++ = indent_char;                       \
   }                                                  \
} while(0);                                           \

#define PRINT_OPENING_BRACKET(c) do {                 \
   *buf ++ = (c);                                     \
   if (flags & f_spaces_around_brackets)              \
      *buf ++ = ' ';                                  \
} while(0);                                           \

#define PRINT_CLOSING_BRACKET(c) do {                 \
   if (flags & f_spaces_around_brackets)              \
      *buf ++ = ' ';                                  \
   *buf ++ = (c);                                     \
} while(0);                                           \

void json_serialize_ex (json_char * buf, json_value * value, json_serialize_opts opts)
{
    json_int_t integer, orig_integer;
    json_object_entry * entry;
    json_char * ptr;
    int indent = 0;
    char indent_char;
    int i;
    int flags;

    flags = get_serialize_flags (opts);

    indent_char = (flags & f_tabs) ? '\t' : ' ';

    while (value)
    {
        switch (value->type)
        {
            case json_array:

                if (((json_builder_value *) value)->length_iterated == 0)
                {
                    if (value->u.array.length == 0)
                    {
                        *buf ++ = '[';
                        *buf ++ = ']';

                        break;
                    }

                    PRINT_OPENING_BRACKET ('[');

                    indent += opts.indent_size;
                    PRINT_NEWLINE();
                }

                if (((json_builder_value *) value)->length_iterated == value->u.array.length)
                {
                    indent -= opts.indent_size;
                    PRINT_NEWLINE();
                    PRINT_CLOSING_BRACKET (']');

                    ((json_builder_value *) value)->length_iterated = 0;
                    break;
                }

                if (((json_builder_value *) value)->length_iterated > 0)
                {
                    *buf ++ = ',';

                    if (flags & f_spaces_after_commas)
                        *buf ++ = ' ';

                    PRINT_NEWLINE();
                }

                ((json_builder_value *) value)->length_iterated++;
                value = value->u.array.values [((json_builder_value *) value)->length_iterated - 1];
                continue;

            case json_object:

                if (((json_builder_value *) value)->length_iterated == 0)
                {
                    if (value->u.object.length == 0)
                    {
                        *buf ++ = '{';
                        *buf ++ = '}';

                        break;
                    }

                    PRINT_OPENING_BRACKET ('{');

                    indent += opts.indent_size;
                    PRINT_NEWLINE();
                }

                if (((json_builder_value *) value)->length_iterated == value->u.object.length)
                {
                    indent -= opts.indent_size;
                    PRINT_NEWLINE();
                    PRINT_CLOSING_BRACKET ('}');

                    ((json_builder_value *) value)->length_iterated = 0;
                    break;
                }

                if (((json_builder_value *) value)->length_iterated > 0)
                {
                    *buf ++ = ',';

                    if (flags & f_spaces_after_commas)
                        *buf ++ = ' ';

                    PRINT_NEWLINE();
                }

                entry = value->u.object.values + (((json_builder_value *) value)->length_iterated ++);

                *buf ++ = '\"';
                buf += serialize_string (buf, entry->name_length, entry->name);
                *buf ++ = '\"';
                *buf ++ = ':';

                if (flags & f_spaces_after_colons)
                    *buf ++ = ' ';

                value = entry->value;
                continue;

            case json_string:

                *buf ++ = '\"';
                buf += serialize_string (buf, value->u.string.length, value->u.string.ptr);
                *buf ++ = '\"';
                break;

            case json_integer:

                integer = value->u.integer;

                if (integer < 0)
                {
                    *buf ++ = '-';
                    integer = - integer;
                }

                orig_integer = integer;

                ++ buf;

                while (integer >= 10)
                {
                    ++ buf;
                    integer /= 10;
                }

                integer = orig_integer;
                ptr = buf;

                do
                {
                    *-- ptr = "0123456789"[integer % 10];

                } while ((integer /= 10) > 0);

                break;

            case json_double:

                ptr = buf;
                buf += sprintf (buf, "%g", value->u.dbl);

                json_char *dot;

                if ((dot = strchr (ptr, ',')))
                {
                    *dot = '.';
                }
                else if (!strchr (ptr, '.') && !strchr (ptr, 'e'))
                {
                    *buf ++ = '.';
                    *buf ++ = '0';
                }

                break;

            case json_boolean:

                if (value->u.boolean)
                {
                    memcpy (buf, "true", 4);
                    buf += 4;
                }
                else
                {
                    memcpy (buf, "false", 5);
                    buf += 5;
                }

                break;

            case json_null:

                memcpy (buf, "null", 4);
                buf += 4;
                break;

            default:
                break;
        };

        value = value->parent;
    }

    *buf = 0;
}

void json_builder_free (json_value * value)
{
    json_value * cur_value;

    if (!value)
        return;

    value->parent = 0;

    while (value)
    {
        switch (value->type)
        {
            case json_array:

                if (!value->u.array.length)
                {
                    free (value->u.array.values);
                    break;
                }

                value = value->u.array.values [-- value->u.array.length];
                continue;

            case json_object:

                if (!value->u.object.length)
                {
                    free (value->u.object.values);
                    break;
                }

                -- value->u.object.length;

                if (((json_builder_value *) value)->is_builder_value)
                {
                    /* Names are allocated separately for builder values.  In parser
                     * values, they are part of the same allocation as the values array
                     * itself.
                     */
                    free (value->u.object.values [value->u.object.length].name);
                }

                value = value->u.object.values [value->u.object.length].value;
                continue;

            case json_string:

                free (value->u.string.ptr);
                break;

            default:
                break;
        };

        cur_value = value;
        value = value->parent;
        free (cur_value);
    }
}
    /* 27: json.c */

struct json_error_table_t {
  int error;
  const char *description;
} json_error_table [] = {
#define JSON_ENULL 1
  { JSON_ENULL, "Json object value is nil"},
#define JSON_ENOTYPE 2
  { JSON_ENOTYPE, "No such type"},
#define JSON_EINVAL 3
  { JSON_EINVAL, "Invalid JSON object"},
#define JSON_ENOSERIAL 4
  { JSON_ENOSERIAL, "Object is not serializable"},
  { -1, NULL}};

static Value strerrorJsonNative(VM *vm, int argCount, Value *args) {
    if (argCount > 1) {
        runtimeError(vm, "strerror() takes either 0 or 1 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    int error;
    if (argCount == 1) {
        error = AS_NUMBER(args[0]);
    } else {
        error = AS_NUMBER(GET_ERRNO(GET_SELF_CLASS));
    }

    if (error == 0) {
        return OBJ_VAL(copyString(vm, "", 0));
    }

    if (error < 0) {
        runtimeError(vm, "strerror() argument should be greater than or equal to 0");
        return EMPTY_VAL;
    }

    for (int i = 0; json_error_table[i].error != -1; i++) {
        if (error == json_error_table[i].error) {
            return OBJ_VAL(copyString(vm, json_error_table[i].description,
                strlen (json_error_table[i].description)));
        }
    }

    runtimeError(vm, "strerror() argument should be <= %d", JSON_ENOSERIAL);
    return EMPTY_VAL;
}

static Value parseJson(VM *vm, json_value *json) {
    switch (json->type) {
        case json_none:
        case json_null: {
            // TODO: We return nil on failure however "null" is valid JSON
            // TODO: We need a better way of handling this scenario
            return NIL_VAL;
        }

        case json_object: {
            ObjDict *dict = initDict(vm);
            // Push value to stack to avoid GC
            push(vm, OBJ_VAL(dict));

            for (unsigned int i = 0; i < json->u.object.length; i++) {
                Value val = parseJson(vm, json->u.object.values[i].value);
                push(vm, val);
                dictSet(vm, dict, OBJ_VAL(copyString(vm, json->u.object.values[i].name, json->u.object.values[i].name_length)), val);
                pop(vm);
            }

            pop(vm);

            return OBJ_VAL(dict);
        }

        case json_array: {
            ObjList *list = initList(vm);
            // Push value to stack to avoid GC
            push(vm, OBJ_VAL(list));

            for (unsigned int i = 0; i < json->u.array.length; i++) {
                Value val = parseJson(vm, json->u.array.values[i]);
                push(vm, val);
                writeValueArray(vm, &list->values, val);
                pop(vm);
            }

            // Pop list
            pop(vm);

            return OBJ_VAL(list);
        }

        case json_integer: {
            return NUMBER_VAL(json->u.integer);
        }

        case json_double: {
            return NUMBER_VAL(json->u.dbl);
        }

        case json_string: {
            return OBJ_VAL(copyString(vm, json->u.string.ptr, json->u.string.length));
        }

        case json_boolean: {
            return BOOL_VAL(json->u.boolean);
        }

        default: {
            errno = JSON_ENOTYPE;
            return EMPTY_VAL;
        }
    }
}

static Value parse(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "parse() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "parse() argument must be a string.");
        return EMPTY_VAL;
    }

    ObjString *json = AS_STRING(args[0]);
    json_value *json_obj = json_parse(json->chars, json->length);

    if (json_obj == NULL) {
        errno = JSON_EINVAL;
        SET_ERRNO(GET_SELF_CLASS);
        return NIL_VAL;
    }

    Value val = parseJson(vm, json_obj);

    if (val == EMPTY_VAL) {
        SET_ERRNO(GET_SELF_CLASS);
        return NIL_VAL;
    }

    json_value_free(json_obj);

    return val;
}

json_value* stringifyJson(Value value) {
    if (IS_NIL(value)) {
        return json_null_new();
    } else if (IS_BOOL(value)) {
        return json_boolean_new(AS_BOOL(value));
    } else if (IS_NUMBER(value)) {
        double num = AS_NUMBER(value);

        if ((int) num == num) {
            return json_integer_new((int) num);
        }

        return json_double_new(num);
    } else if (IS_OBJ(value)) {
        switch (AS_OBJ(value)->type) {
            case OBJ_STRING: {
                return json_string_new(AS_CSTRING(value));
            }

            case OBJ_LIST: {
                ObjList *list = AS_LIST(value);
                json_value *json = json_array_new(list->values.count);

                for (int i = 0; i < list->values.count; i++) {
                    json_array_push(json, stringifyJson(list->values.values[i]));
                }

                return json;
            }

            case OBJ_DICT: {
                ObjDict *dict = AS_DICT(value);
                json_value *json = json_object_new(dict->count);

                for (int i = 0; i <= dict->capacityMask; i++) {
                    DictItem *entry = &dict->entries[i];
                    if (IS_EMPTY(entry->key)) {
                        continue;
                    }

                    char *key;

                    if (IS_STRING(entry->key)) {
                        ObjString *s = AS_STRING(entry->key);
                        key = s->chars;
                    } else if (IS_NIL(entry->key)) {
                        key = malloc(5);
                        memcpy(key, "null", 4);
                        key[4] = '\0';
                    } else {
                        key = valueToString(entry->key);
                    }

                    json_object_push(
                            json,
                            key,
                            stringifyJson(entry->value)
                    );

                    if (!IS_STRING(entry->key)) {
                        free(key);
                    }
                }

                return json;
            }

            // Pass through and return NULL
            default: {}
        }
    }

    return NULL;
}

static Value stringify(VM *vm, int argCount, Value *args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError(vm, "stringify() takes 1 or 2 arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    int indent = 4;
    int lineType = json_serialize_mode_single_line;

    if (argCount == 2) {
        if (!IS_NUMBER(args[1])) {
            runtimeError(vm, "stringify() second argument must be a number.");
            return EMPTY_VAL;
        }

        lineType = json_serialize_mode_multiline;
        indent = AS_NUMBER(args[1]);
    }

    json_value *json = stringifyJson(args[0]);

    if (json == NULL) {
        errno = JSON_ENOSERIAL;
        SET_ERRNO(GET_SELF_CLASS);
        return NIL_VAL;
    }

    json_serialize_opts default_opts =
    {
        lineType,
        json_serialize_opt_pack_brackets,
        indent
    };

    char *buf = malloc(json_measure_ex(json, default_opts));
    json_serialize_ex(buf, json, default_opts);
    ObjString *string = copyString(vm, buf, strlen(buf));
    free(buf);
    json_builder_free(json);
    return OBJ_VAL(string);
}

void createJSONClass(VM *vm) {
    ObjString *name = copyString(vm, "JSON", 4);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define Json methods
     */
    defineNative(vm, &klass->methods, "strerror", strerrorJsonNative);
    defineNative(vm, &klass->methods, "parse", parse);
    defineNative(vm, &klass->methods, "stringify", stringify);

    /**
     * Define Json properties
     */
    defineNativeProperty(vm, &klass->properties, "errno", NUMBER_VAL(0));
    defineNativeProperty(vm, &klass->properties, "ENULL", NUMBER_VAL(JSON_ENULL));
    defineNativeProperty(vm, &klass->properties, "ENOTYPE", NUMBER_VAL(JSON_ENOTYPE));
    defineNativeProperty(vm, &klass->properties, "EINVAL", NUMBER_VAL(JSON_EINVAL));
    defineNativeProperty(vm, &klass->properties, "ENOSERIAL", NUMBER_VAL(JSON_ENOSERIAL));

    tableSet(vm, &vm->globals, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}

    /* 28: math.c */

static Value averageNative(VM *vm, int argCount, Value *args) {
    double average = 0;

    if (argCount == 0) {
        return NUMBER_VAL(0);
    } else if (argCount == 1 && IS_LIST(args[0])) {
        ObjList *list = AS_LIST(args[0]);
        argCount = list->values.count;
        args = list->values.values;
    }

    for (int i = 0; i < argCount; ++i) {
        Value value = args[i];
        if (!IS_NUMBER(value)) {
            runtimeError(vm, "A non-number value passed to average()");
            return EMPTY_VAL;
        }
        average = average + AS_NUMBER(value);
    }

    return NUMBER_VAL(average / argCount);
}

static Value floorNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "floor() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "A non-number value passed to floor()");
        return EMPTY_VAL;
    }

    return NUMBER_VAL(floor(AS_NUMBER(args[0])));
}

static Value roundNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "round() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "A non-number value passed to round()");
        return EMPTY_VAL;
    }

    return NUMBER_VAL(round(AS_NUMBER(args[0])));
}

static Value ceilNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "ceil() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "A non-number value passed to ceil()");
        return EMPTY_VAL;
    }

    return NUMBER_VAL(ceil(AS_NUMBER(args[0])));
}

static Value absNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "abs() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "A non-number value passed to abs()");
        return EMPTY_VAL;
    }

    double absValue = AS_NUMBER(args[0]);

    if (absValue < 0)
        return NUMBER_VAL(absValue * -1);

    return NUMBER_VAL(absValue);
}

static Value sumNative(VM *vm, int argCount, Value *args) {
    double sum = 0;

    if (argCount == 0) {
        return NUMBER_VAL(0);
    } else if (argCount == 1 && IS_LIST(args[0])) {
        ObjList *list = AS_LIST(args[0]);
        argCount = list->values.count;
        args = list->values.values;
    }

    for (int i = 0; i < argCount; ++i) {
        Value value = args[i];
        if (!IS_NUMBER(value)) {
            runtimeError(vm, "A non-number value passed to sum()");
            return EMPTY_VAL;
        }
        sum = sum + AS_NUMBER(value);
    }

    return NUMBER_VAL(sum);
}

static Value minNative(VM *vm, int argCount, Value *args) {
    if (argCount == 0) {
        return NUMBER_VAL(0);
    } else if (argCount == 1 && IS_LIST(args[0])) {
        ObjList *list = AS_LIST(args[0]);
        argCount = list->values.count;
        args = list->values.values;
    }

    double minimum = AS_NUMBER(args[0]);

    for (int i = 1; i < argCount; ++i) {
        Value value = args[i];
        if (!IS_NUMBER(value)) {
            runtimeError(vm, "A non-number value passed to min()");
            return EMPTY_VAL;
        }

        double current = AS_NUMBER(value);

        if (minimum > current) {
            minimum = current;
        }
    }

    return NUMBER_VAL(minimum);
}

static Value maxNative(VM *vm, int argCount, Value *args) {
    if (argCount == 0) {
        return NUMBER_VAL(0);
    } else if (argCount == 1 && IS_LIST(args[0])) {
        ObjList *list = AS_LIST(args[0]);
        argCount = list->values.count;
        args = list->values.values;
    }

    double maximum = AS_NUMBER(args[0]);

    for (int i = 1; i < argCount; ++i) {
        Value value = args[i];
        if (!IS_NUMBER(value)) {
            runtimeError(vm, "A non-number value passed to max()");
            return EMPTY_VAL;
        }

        double current = AS_NUMBER(value);

        if (maximum < current) {
            maximum = current;
        }
    }

    return NUMBER_VAL(maximum);
}

void createMathsClass(VM *vm) {
    ObjString *name = copyString(vm, "Math", 4);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define Math methods
     */
    defineNative(vm, &klass->methods, "average", averageNative);
    defineNative(vm, &klass->methods, "floor", floorNative);
    defineNative(vm, &klass->methods, "round", roundNative);
    defineNative(vm, &klass->methods, "ceil", ceilNative);
    defineNative(vm, &klass->methods, "abs", absNative);
    defineNative(vm, &klass->methods, "max", maxNative);
    defineNative(vm, &klass->methods, "min", minNative);
    defineNative(vm, &klass->methods, "sum", sumNative);

    /**
     * Define Math properties
     */
    defineNativeProperty(vm, &klass->properties, "PI", NUMBER_VAL(3.14159265358979));
    defineNativeProperty(vm, &klass->properties, "e", NUMBER_VAL(2.71828182845905));

    tableSet(vm, &vm->globals, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}

    /* 29: path.c */

#ifdef HAS_REALPATH
static Value realpathNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "realpath() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "realpath() argument must be a string");
        return EMPTY_VAL;
    }

    char *path = AS_CSTRING(args[0]);

    char tmp[PATH_MAX + 1];
    if (NULL == realpath(path, tmp)) {
        SET_ERRNO(GET_SELF_CLASS);
        return NIL_VAL;
    }

    return OBJ_VAL(copyString(vm, tmp, strlen (tmp)));
}
#endif

static Value isAbsoluteNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "isAbsolute() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "isAbsolute() argument must be a string");
        return EMPTY_VAL;
    }

    char *path = AS_CSTRING(args[0]);

    return (IS_DIR_SEPARATOR(path[0]) ? TRUE_VAL : FALSE_VAL);
}

static Value basenameNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "basename() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "basename() argument must be a string");
        return EMPTY_VAL;
    }

    ObjString *PathString = AS_STRING(args[0]);
    char *path = PathString->chars;

    int len = PathString->length;

    if (!len || (len == 1 && *path != DIR_SEPARATOR)) {
        return OBJ_VAL(copyString(vm, "", 0));
    }

    char *p = path + len - 1;
    while (p > path && (*(p - 1) != DIR_SEPARATOR)) --p;

    return OBJ_VAL(copyString(vm, p, (len - (p - path))));
}

static Value extnameNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "extname() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "extname() argument must be a string");
        return EMPTY_VAL;
    }

    ObjString *PathString = AS_STRING(args[0]);
    char *path = PathString->chars;

    int len = PathString->length;

    if (!len) {
        return OBJ_VAL(copyString(vm, path, len));
    }

    char *p = path + len;
    while (p > path && (*(p - 1) != '.')) --p;

    if (p == path) {
        return OBJ_VAL(copyString(vm, "", 0));
    }

    p--;

    return OBJ_VAL(copyString(vm, p, len - (p - path)));
}

static Value dirnameNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "dirname() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "dirname() argument must be a string");
        return EMPTY_VAL;
    }

    ObjString *PathString = AS_STRING(args[0]);
    char *path = PathString->chars;

    int len = PathString->length;

    if (!len) {
        return OBJ_VAL(copyString(vm, ".", 1));
    }

    char *sep = path + len;

    /* trailing slashes */
    while (sep != path) {
        if (0 == IS_DIR_SEPARATOR (*sep))
            break;
        sep--;
    }

    /* first found */
    while (sep != path) {
        if (IS_DIR_SEPARATOR (*sep))
            break;
        sep--;
    }

    /* trim again */
    while (sep != path) {
        if (0 == IS_DIR_SEPARATOR (*sep))
            break;
        sep--;
    }

    if (sep == path && *sep != DIR_SEPARATOR) {
        return OBJ_VAL(copyString(vm, ".", 1));
    }

    len = sep - path + 1;

    return OBJ_VAL(copyString(vm, path, len));
}

void createPathClass(VM *vm) {
    ObjString *name = copyString(vm, "Path", 4);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define Path methods
     */
#ifdef HAS_REALPATH
    defineNative(vm, &klass->methods, "realpath", realpathNative);
    defineNativeProperty(vm, &klass->properties, "errno", NUMBER_VAL(0));
    defineNative(vm, &klass->methods, "strerror", strerrorNative); // only realpath uset errno
#endif
    defineNative(vm, &klass->methods, "isAbsolute", isAbsoluteNative);
    defineNative(vm, &klass->methods, "basename", basenameNative);
    defineNative(vm, &klass->methods, "extname", extnameNative);
    defineNative(vm, &klass->methods, "dirname", dirnameNative);

    defineNativeProperty(vm, &klass->properties, "delimiter", OBJ_VAL(
        copyString(vm, PATH_DELIMITER_AS_STRING, PATH_DELIMITER_STRLEN)));
    defineNativeProperty(vm, &klass->properties, "dirSeparator", OBJ_VAL(
        copyString(vm, DIR_SEPARATOR_AS_STRING, DIR_SEPARATOR_STRLEN)));

    tableSet(vm, &vm->globals, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}

    /* 30: system.c */

static Value getgidNative(VM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getgid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getgid());
}

static Value getegidNative(VM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getegid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getegid());
}

static Value getuidNative(VM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getuid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getuid());
}

static Value geteuidNative(VM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "geteuid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(geteuid());
}

static Value getppidNative(VM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getppid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getppid());
}

static Value getpidNative(VM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getpid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getpid());
}

static Value rmdirNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "rmdir() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "rmdir() argument must be a string");
        return EMPTY_VAL;
    }

    char *dir = AS_CSTRING(args[0]);

    int retval = rmdir(dir);

    if (-1 == retval) {
      SET_ERRNO(GET_SELF_CLASS);
    }

    return NUMBER_VAL(retval == 0 ? OK : NOTOK);
}

static Value mkdirNative(VM *vm, int argCount, Value *args) {
    if (argCount == 0 || argCount > 2) {
        runtimeError(vm, "mkdir() takes 1 or 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "mkdir() first argument must be a string");
        return EMPTY_VAL;
    }

    char *dir = AS_CSTRING(args[0]);

    int mode = 0777;

    if (argCount == 2) {
        if (!IS_NUMBER(args[1])) {
            runtimeError(vm, "mkdir() second argument must be a number");
            return EMPTY_VAL;
        }

        mode = AS_NUMBER(args[1]);
    }

    int retval = MKDIR(dir, mode);

    if (retval == NOTOK) {
      SET_ERRNO(GET_SELF_CLASS);
    }

    return NUMBER_VAL(retval == 0 ? OK : NOTOK);
}

static Value removeNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "remove() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "remove() argument must be a string");
        return EMPTY_VAL;
    }

    char *file = AS_CSTRING(args[0]);

    int retval = REMOVE(file);

    if (retval == NOTOK) {
      SET_ERRNO(GET_SELF_CLASS);
    }

    return NUMBER_VAL(retval == 0 ? OK : NOTOK);
}

static Value setCWDNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "setcwd() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "setcwd() argument must be a string");
        return EMPTY_VAL;
    }

    char *dir = AS_CSTRING(args[0]);

    int retval = chdir(dir);

    if (retval == NOTOK) {
        SET_ERRNO(GET_SELF_CLASS);
    }

    return NUMBER_VAL(retval == 0 ? OK : NOTOK);
}

static Value getCWDNative(VM *vm, int argCount, Value *args) {
    UNUSED(argCount); UNUSED(args);

    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        return OBJ_VAL(copyString(vm, cwd, strlen(cwd)));
    }

    SET_ERRNO(GET_SELF_CLASS);

    return NIL_VAL;
}

static Value timeNative(VM *vm, int argCount, Value *args) {
    UNUSED(vm); UNUSED(argCount); UNUSED(args);

    return NUMBER_VAL((double) time(NULL));
}

static Value clockNative(VM *vm, int argCount, Value *args) {
    UNUSED(vm); UNUSED(argCount); UNUSED(args);

    return NUMBER_VAL((double) clock() / CLOCKS_PER_SEC);
}

static Value collectNative(VM *vm, int argCount, Value *args) {
    UNUSED(argCount); UNUSED(args);

    collectGarbage(vm);
    return NIL_VAL;
}

static Value sleepNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "sleep() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "sleep() argument must be a number");
        return EMPTY_VAL;
    }

    double stopTime = AS_NUMBER(args[0]);

#ifdef _WIN32
    Sleep(stopTime * 1000);
#else
    sleep(stopTime);
#endif
    return NIL_VAL;
}

static Value exitNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "exit() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "exit() argument must be a number");
        return EMPTY_VAL;
    }

    exit(AS_NUMBER(args[0]));
    return EMPTY_VAL; /* satisfy the tcc compiler */
}

void initArgv(VM *vm, Table *table, int argc, const char *argv[]) {
    ObjList *list = initList(vm);
    push(vm, OBJ_VAL(list));

    for (int i = 1; i < argc; i++) {
        Value arg = OBJ_VAL(copyString(vm, argv[i], strlen(argv[i])));
        push(vm, arg);
        writeValueArray(vm, &list->values, arg);
        pop(vm);
    }

    defineNativeProperty(vm, table, "argv", OBJ_VAL(list));
    pop(vm);
}

void initPlatform(VM *vm, Table *table) {
#ifdef _WIN32
    defineNativeProperty(vm, table, "platform", OBJ_VAL(copyString(vm, "windows", 7)));
    return;
#endif

    struct utsname u;
    if (-1 == uname(&u)) {
        defineNativeProperty(vm, table, "platform", OBJ_VAL(copyString(vm,
            "unknown", 7)));
        return;
    }

    u.sysname[0] = tolower(u.sysname[0]);
    defineNativeProperty(vm, table, "platform", OBJ_VAL(copyString(vm, u.sysname,
        strlen(u.sysname))));
}

void createSystemClass(VM *vm, int argc, const char *argv[]) {
    ObjString *name = copyString(vm, "System", 6);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define System methods
     */
    defineNative(vm, &klass->methods, "strerror", strerrorNative);
    defineNative(vm, &klass->methods, "getgid", getgidNative);
    defineNative(vm, &klass->methods, "getegid", getegidNative);
    defineNative(vm, &klass->methods, "getuid", getuidNative);
    defineNative(vm, &klass->methods, "geteuid", geteuidNative);
    defineNative(vm, &klass->methods, "getppid", getppidNative);
    defineNative(vm, &klass->methods, "getpid", getpidNative);
    defineNative(vm, &klass->methods, "rmdir", rmdirNative);
    defineNative(vm, &klass->methods, "mkdir", mkdirNative);
    defineNative(vm, &klass->methods, "remove", removeNative);
    defineNative(vm, &klass->methods, "setCWD", setCWDNative);
    defineNative(vm, &klass->methods, "getCWD", getCWDNative);
    defineNative(vm, &klass->methods, "time", timeNative);
    defineNative(vm, &klass->methods, "clock", clockNative);
    defineNative(vm, &klass->methods, "collect", collectNative);
    defineNative(vm, &klass->methods, "sleep", sleepNative);
    /*** DISABLED ***/
    (void) exitNative;
    // defineNative(vm, &klass->methods, "exit", exitNative);

    /**
     * Define System properties
     */
    if (!vm->repl) {
        // Set argv variable
        initArgv(vm, &klass->properties, argc, argv);
    }

    initPlatform(vm, &klass->properties);

    defineNativeProperty(vm, &klass->properties, "errno", NUMBER_VAL(0));

    defineNativeProperty(vm, &klass->properties, "S_IRWXU", NUMBER_VAL(448));
    defineNativeProperty(vm, &klass->properties, "S_IRUSR", NUMBER_VAL(256));
    defineNativeProperty(vm, &klass->properties, "S_IWUSR", NUMBER_VAL(128));
    defineNativeProperty(vm, &klass->properties, "S_IXUSR", NUMBER_VAL(64));
    defineNativeProperty(vm, &klass->properties, "S_IRWXG", NUMBER_VAL(56));
    defineNativeProperty(vm, &klass->properties, "S_IRGRP", NUMBER_VAL(32));
    defineNativeProperty(vm, &klass->properties, "S_IWGRP", NUMBER_VAL(16));
    defineNativeProperty(vm, &klass->properties, "S_IXGRP", NUMBER_VAL(8));
    defineNativeProperty(vm, &klass->properties, "S_IRWXO", NUMBER_VAL(7));
    defineNativeProperty(vm, &klass->properties, "S_IROTH", NUMBER_VAL(4));
    defineNativeProperty(vm, &klass->properties, "S_IWOTH", NUMBER_VAL(2));
    defineNativeProperty(vm, &klass->properties, "S_IXOTH", NUMBER_VAL(1));
    defineNativeProperty(vm, &klass->properties, "S_ISUID", NUMBER_VAL(2048));
    defineNativeProperty(vm, &klass->properties, "S_ISGID", NUMBER_VAL(1024));

    tableSet(vm, &vm->globals, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}

    /* 31: datetime.c */

static Value nowNative(VM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "now() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    time_t t = time(NULL);
    struct tm tictoc;
    char time[26];

    localtime_r(&t, &tictoc);
    asctime_r(&tictoc, time);

    // -1 to remove newline
    return OBJ_VAL(copyString(vm, time, strlen(time) - 1));
}

static Value nowUTCNative(VM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "nowUTC() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    time_t t = time(NULL);
    struct tm tictoc;
    char time[26];

    gmtime_r(&t, &tictoc);
    asctime_r(&tictoc, time);

    // -1 to remove newline
    return OBJ_VAL(copyString(vm, time, strlen(time) - 1));
}

static Value strftimeNative(VM *vm, int argCount, Value *args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError(vm, "strftime() takes 1 or 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "strftime() argument must be a string");
        return EMPTY_VAL;
    }

    time_t t;

    if (argCount == 2) {
        if (!IS_NUMBER(args[1])) {
            runtimeError(vm, "strftime() optional argument must be a number");
            return EMPTY_VAL;
        }

        t = AS_NUMBER(args[1]);
    } else {
        time(&t);
    }

    ObjString *format = AS_STRING(args[0]);

    /** this is to avoid an eternal loop while calling strftime() below */
    if (0 == format->length)
        return OBJ_VAL(copyString(vm, "", 0));

    char *fmt = format->chars;

    struct tm tictoc;
    int len = (format->length > 128 ? format->length * 4 : 128);
    char buffer[len], *point = buffer;

    gmtime_r(&t, &tictoc);

    /**
     * strtime returns 0 when it fails to write - this would be due to the buffer
     * not being large enough. In that instance we double the buffer length until
     * there is a big enough buffer.
     */

    /** however is not guaranteed that 0 indicates a failure (`man strftime' says so).
     * So we might want to catch up the eternal loop, by using a maximum iterator.
     */

    ObjString *res;

    int max_iterations = 8;  // maximum 65536 bytes with the default 128 len,
                             // more if the given string is > 128
    int iterator = 0;
    while (strftime(point, sizeof(char) * len, fmt, &tictoc) == 0) {
        if (++iterator > max_iterations) {
            res = copyString(vm, "", 0);
            goto theend;
        }

        len *= 2;

        if (buffer == point)
            point = malloc (len);
        else
            point = realloc (point, len);

    }

    res = copyString(vm, point, strlen(point));

theend:
    if (buffer != point)
        free(point);

    return OBJ_VAL(res);
}

static Value strptimeNative(VM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "strptime() takes 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError(vm, "strptime() arguments must be strings");
        return EMPTY_VAL;
    }

    struct tm tictoc = {0};
    tictoc.tm_mday = 1;
    tictoc.tm_isdst = -1;

    char *end = strptime(AS_CSTRING(args[1]), AS_CSTRING(args[0]), &tictoc);

    if (end == NULL) {
        return NIL_VAL;
    }

    return NUMBER_VAL((double) mktime(&tictoc));
}

void createDatetimeClass(VM *vm) {
    ObjString *name = copyString(vm, "Datetime", 8);
    push(vm, OBJ_VAL(name));
    ObjClassNative *klass = newClassNative(vm, name);
    push(vm, OBJ_VAL(klass));

    /**
     * Define Datetime methods
     */
    defineNative(vm, &klass->methods, "now", nowNative);
    defineNative(vm, &klass->methods, "nowUTC", nowUTCNative);
    defineNative(vm, &klass->methods, "strftime", strftimeNative);
    defineNative(vm, &klass->methods, "strptime", strptimeNative);

    tableSet(vm, &vm->globals, name, OBJ_VAL(klass));
    pop(vm);
    pop(vm);
}
