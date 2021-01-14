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
#include <sys/wait.h>
#ifndef DISABLE_HTTP
#include <curl/curl.h>
#endif /* DISABLE_HTTP */
#ifndef DISABLE_SQLITE
#include <sqlite3.h>
#endif /* DISABLE_SQLITE */
#include <dirent.h>
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>

#include "__lai.h"

    /* 1: vm/value.c */


#define TABLE_MAX_LOAD 0.75
#define TABLE_MIN_LOAD 0.25

void initValueArray(ValueArray *array) {
    array->values = NULL;
    array->capacity = 0;
    array->count = 0;
}

void writeValueArray(DictuVM *vm, ValueArray *array, Value value) {
    if (array->capacity < array->count + 1) {
        int oldCapacity = array->capacity;
        array->capacity = GROW_CAPACITY(oldCapacity);
        array->values = GROW_ARRAY(vm, array->values, Value,
                                   oldCapacity, array->capacity);
    }

    array->values[array->count] = value;
    array->count++;
}

void freeValueArray(DictuVM *vm, ValueArray *array) {
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

static void adjustDictCapacity(DictuVM *vm, ObjDict *dict, int capacityMask) {
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

bool dictSet(DictuVM *vm, ObjDict *dict, Value key, Value value) {
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

bool dictDelete(DictuVM *vm, ObjDict *dict, Value key) {
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
        capacityMask = SHRINK_CAPACITY(dict->capacityMask + 1) - 1;
        adjustDictCapacity(vm, dict, capacityMask);
    }

    return true;
}

void grayDict(DictuVM *vm, ObjDict *dict) {
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

static void adjustSetCapacity(DictuVM *vm, ObjSet *set, int capacityMask) {
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

bool setInsert(DictuVM *vm, ObjSet *set, Value value) {
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

bool setDelete(DictuVM *vm, ObjSet *set, Value value) {
    if (set->count == 0) return false;

    SetItem *entry = findSetEntry(set->entries, set->capacityMask, value);
    if (IS_EMPTY(entry->value)) return false;

    // Place a tombstone in the entry.
    set->count--;
    entry->deleted = true;

    if (set->count - 1 < set->capacityMask * TABLE_MIN_LOAD) {
        // Figure out the new table size.
        int capacityMask = SHRINK_CAPACITY(set->capacityMask + 1) - 1;
        adjustSetCapacity(vm, set, capacityMask);
    }

    return true;
}

void graySet(DictuVM *vm, ObjSet *set) {
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

// Calling function needs to free memory
char *valueTypeToString(DictuVM *vm, Value value, int *length) {
#define CONVERT(typeString, size)                     \
    do {                                              \
        char *string = ALLOCATE(vm, char, size + 1);  \
        memcpy(string, #typeString, size);            \
        string[size] = '\0';                          \
        *length = size;                               \
        return string;                                \
    } while (false)

#define CONVERT_VARIABLE(typeString, size)            \
    do {                                              \
        char *string = ALLOCATE(vm, char, size + 1);  \
        memcpy(string, typeString, size);             \
        string[size] = '\0';                          \
        *length = size;                               \
        return string;                                \
    } while (false)


    if (IS_BOOL(value)) {
        CONVERT(bool, 4);
    } else if (IS_NIL(value)) {
        CONVERT(nil, 3);
    } else if (IS_NUMBER(value)) {
        CONVERT(number, 6);
    } else if (IS_OBJ(value)) {
        switch (OBJ_TYPE(value)) {
            case OBJ_CLASS: {
                switch (AS_CLASS(value)->type) {
                    case CLASS_DEFAULT:
                    case CLASS_ABSTRACT: {
                        CONVERT(class, 5);
                    }
                    case CLASS_TRAIT: {
                        CONVERT(trait, 5);
                    }
                }

                break;
            }
            case OBJ_MODULE: {
                CONVERT(module, 6);
            }
            case OBJ_INSTANCE: {
                ObjString *className = AS_INSTANCE(value)->klass->name;

                CONVERT_VARIABLE(className->chars, className->length);
            }
            case OBJ_BOUND_METHOD: {
                CONVERT(method, 6);
            }
            case OBJ_CLOSURE:
            case OBJ_FUNCTION: {
                CONVERT(function, 8);
            }
            case OBJ_STRING: {
                CONVERT(string, 6);
            }
            case OBJ_LIST: {
                CONVERT(list, 4);
            }
            case OBJ_DICT: {
                CONVERT(dict, 4);
            }
            case OBJ_SET: {
                CONVERT(set, 3);
            }
            case OBJ_NATIVE: {
                CONVERT(native, 6);
            }
            case OBJ_FILE: {
                CONVERT(file, 4);
            }
            default:
                break;
        }
    }

    CONVERT(unknown, 7);
#undef CONVERT
#undef CONVERT_VARIABLE
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
    /* 2: vm/chunk.c */

void initChunk(DictuVM *vm, Chunk *chunk) {
    UNUSED(vm);

    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    initValueArray(&chunk->constants);
}

void freeChunk(DictuVM *vm, Chunk *chunk) {
    FREE_ARRAY(vm, uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(vm, int, chunk->lines, chunk->capacity);
    freeValueArray(vm, &chunk->constants);
    initChunk(vm, chunk);
}

void writeChunk(DictuVM *vm, Chunk *chunk, uint8_t byte, int line) {
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

int addConstant(DictuVM *vm, Chunk *chunk, Value value) {
    push(vm, value);
    writeValueArray(vm, &chunk->constants, value);
    pop(vm);
    return chunk->constants.count - 1;
}

    /* 3: vm/table.c */


#define TABLE_MAX_LOAD 0.75

void initTable(Table *table) {
    table->count = 0;
    table->capacityMask = -1;
    table->entries = NULL;
}

void freeTable(DictuVM *vm, Table *table) {
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

static void adjustCapacity(DictuVM *vm, Table *table, int capacityMask) {
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

bool tableSet(DictuVM *vm, Table *table, ObjString *key, Value value) {
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

bool tableDelete(DictuVM *vm, Table *table, ObjString *key) {
    UNUSED(vm);
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

    return true;
}

void tableAddAll(DictuVM *vm, Table *from, Table *to) {
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

void tableRemoveWhite(DictuVM *vm, Table *table) {
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry *entry = &table->entries[i];
        if (entry->key != NULL && !entry->key->obj.isDark) {
            tableDelete(vm, table, entry->key);
        }
    }
}

void grayTable(DictuVM *vm, Table *table) {
    for (int i = 0; i <= table->capacityMask; i++) {
        Entry *entry = &table->entries[i];
        grayObject(vm, (Obj *) entry->key);
        grayValue(vm, entry->value);
    }
}

    /* 4: vm/object.c */


#define ALLOCATE_OBJ(vm, type, objectType) \
    (type*)allocateObject(vm, sizeof(type), objectType)

static Obj *allocateObject(DictuVM *vm, size_t size, ObjType type) {
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

ObjModule *newModule(DictuVM *vm, ObjString *name) {
    Value moduleVal;
    if (tableGet(&vm->modules, name, &moduleVal)) {
        return AS_MODULE(moduleVal);
    }

    ObjModule *module = ALLOCATE_OBJ(vm, ObjModule, OBJ_MODULE);
    initTable(&module->values);
    module->name = name;
    module->path = NULL;

    push(vm, OBJ_VAL(module));
    ObjString *__file__ = copyString(vm, "__file__", 8);
    push(vm, OBJ_VAL(__file__));

    tableSet(vm, &module->values, __file__, OBJ_VAL(name));
    tableSet(vm, &vm->modules, name, OBJ_VAL(module));

    pop(vm);
    pop(vm);

    return module;
}

ObjBoundMethod *newBoundMethod(DictuVM *vm, Value receiver, ObjClosure *method) {
    ObjBoundMethod *bound = ALLOCATE_OBJ(vm, ObjBoundMethod,
                                         OBJ_BOUND_METHOD);

    bound->receiver = receiver;
    bound->method = method;
    return bound;
}

ObjClass *newClass(DictuVM *vm, ObjString *name, ObjClass *superclass, ClassType type) {
    ObjClass *klass = ALLOCATE_OBJ(vm, ObjClass, OBJ_CLASS);
    klass->name = name;
    klass->superclass = superclass;
    klass->type = type;
    initTable(&klass->abstractMethods);
    initTable(&klass->methods);
    initTable(&klass->properties);
    return klass;
}

ObjClosure *newClosure(DictuVM *vm, ObjFunction *function) {
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

ObjFunction *newFunction(DictuVM *vm, ObjModule *module, FunctionType type) {
    ObjFunction *function = ALLOCATE_OBJ(vm, ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->arityOptional = 0;
    function->upvalueCount = 0;
    function->propertyCount = 0;
    function->propertyIndexes = NULL;
    function->propertyNames = NULL;
    function->name = NULL;
    function->type = type;
    function->module = module;
    initChunk(vm, &function->chunk);

    return function;
}

ObjInstance *newInstance(DictuVM *vm, ObjClass *klass) {
    ObjInstance *instance = ALLOCATE_OBJ(vm, ObjInstance, OBJ_INSTANCE);
    instance->klass = klass;
    initTable(&instance->fields);
    return instance;
}

ObjNative *newNative(DictuVM *vm, NativeFn function) {
    ObjNative *native = ALLOCATE_OBJ(vm, ObjNative, OBJ_NATIVE);
    native->function = function;
    return native;
}

static ObjString *allocateString(DictuVM *vm, char *chars, int length,
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

ObjList *newList(DictuVM *vm) {
    ObjList *list = ALLOCATE_OBJ(vm, ObjList, OBJ_LIST);
    initValueArray(&list->values);
    return list;
}

ObjDict *newDict(DictuVM *vm) {
    ObjDict *dict = ALLOCATE_OBJ(vm, ObjDict, OBJ_DICT);
    dict->count = 0;
    dict->capacityMask = -1;
    dict->entries = NULL;
    return dict;
}

ObjSet *newSet(DictuVM *vm) {
    ObjSet *set = ALLOCATE_OBJ(vm, ObjSet, OBJ_SET);
    set->count = 0;
    set->capacityMask = -1;
    set->entries = NULL;
    return set;
}

ObjFile *newFile(DictuVM *vm) {
    return ALLOCATE_OBJ(vm, ObjFile, OBJ_FILE);
}

ObjAbstract *newAbstract(DictuVM *vm, AbstractFreeFn func) {
    ObjAbstract *abstract = ALLOCATE_OBJ(vm, ObjAbstract, OBJ_ABSTRACT);
    abstract->data = NULL;
    abstract->func = func;
    initTable(&abstract->values);

    return abstract;
}

ObjResult *newResult(DictuVM *vm, ResultStatus status, Value value) {
    ObjResult *result = ALLOCATE_OBJ(vm, ObjResult, OBJ_RESULT);
    result->status = status;
    result->value = value;

    return result;
}

Value newResultSuccess(DictuVM *vm, Value value) {
    push(vm, value);
    ObjResult *result = newResult(vm, SUCCESS, value);
    pop(vm);
    return OBJ_VAL(result);
}

Value newResultError(DictuVM *vm, char *errorMsg) {
    Value error = OBJ_VAL(copyString(vm, errorMsg, strlen(errorMsg)));
    push(vm, error);
    ObjResult *result = newResult(vm, ERR, error);
    pop(vm);
    return OBJ_VAL(result);
}

static uint32_t hashString(const char *key, int length) {
    uint32_t hash = 2166136261u;

    for (int i = 0; i < length; i++) {
        hash ^= key[i];
        hash *= 16777619;
    }

    return hash;
}

ObjString *takeString(DictuVM *vm, char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm->strings, chars, length,
                                          hash);
    if (interned != NULL) {
        FREE_ARRAY(vm, char, chars, length + 1);
        return interned;
    }

    // Ensure terminating char is present
    chars[length] = '\0';
    return allocateString(vm, chars, length, hash);
}

ObjString *copyString(DictuVM *vm, const char *chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString *interned = tableFindString(&vm->strings, chars, length,
                                          hash);
    if (interned != NULL) return interned;

    char *heapChars = ALLOCATE(vm, char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(vm, heapChars, length, hash);
}

ObjUpvalue *newUpvalue(DictuVM *vm, Value *slot) {
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

       if (keySize > (size - dictStringLength - keySize - 4)) {
           if (keySize > size) {
               size += keySize * 2 + 4;
           } else {
               size *= 2 + 4;
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

       if (elementSize > (size - dictStringLength - elementSize - 6)) {
           if (elementSize > size) {
               size += elementSize * 2 + 6;
           } else {
               size = size * 2 + 6;
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
    memcpy(classString, "<Cls ", 5);
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
            snprintf(moduleString, (module->name->length + 10), "<Module %s>", module->name->chars);
            return moduleString;
        }

        case OBJ_CLASS: {
            if (IS_TRAIT(value)) {
                ObjClass *trait = AS_CLASS(value);
                char *traitString = malloc(sizeof(char) * (trait->name->length + 10));
                snprintf(traitString, trait->name->length + 9, "<Trait %s>", trait->name->chars);
                return traitString;
            }

            return classToString(value);
        }

        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *method = AS_BOUND_METHOD(value);
            char *methodString;

            if (method->method->function->name != NULL) {
                methodString = malloc(sizeof(char) * (method->method->function->name->length + 17));

                switch (method->method->function->type) {
                    case TYPE_STATIC: {
                        snprintf(methodString, method->method->function->name->length + 17, "<Bound Method %s>", method->method->function->name->chars);
                        break;
                    }

                    default: {
                        snprintf(methodString, method->method->function->name->length + 17, "<Static Method %s>", method->method->function->name->chars);
                        break;
                    }
                }
            } else {
                methodString = malloc(sizeof(char) * 16);

                switch (method->method->function->type) {
                    case TYPE_STATIC: {
                        memcpy(methodString, "<Static Method>", 15);
                        methodString[15] = '\0';
                        break;
                    }

                    default: {
                        memcpy(methodString, "<Bound Method>", 15);
                        methodString[15] = '\0';
                        break;
                    }
                }
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
                memcpy(closureString, "<Script>", 8);
                closureString[8] = '\0';
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
                memcpy(functionString, "<fn>", 4);
                functionString[4] = '\0';
            }

            return functionString;
        }

        case OBJ_INSTANCE: {
            return instanceToString(value);
        }

        case OBJ_NATIVE: {
            char *nativeString = malloc(sizeof(char) * 12);
            memcpy(nativeString, "<fn native>", 11);
            nativeString[11] = '\0';
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
            snprintf(fileString, strlen(file->path) + 8, "<File %s>", file->path);
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
            char *upvalueString = malloc(sizeof(char) * 8);
            memcpy(upvalueString, "upvalue", 7);
            upvalueString[7] = '\0';
            return upvalueString;
        }

        // TODO: Think about string conversion for abstract types
        case OBJ_ABSTRACT: {
            break;
        }

        case OBJ_RESULT: {
            ObjResult *result = AS_RESULT(value);
            if (result->status == SUCCESS) {
                char *resultString = malloc(sizeof(char) * 13);
                snprintf(resultString, 13, "<Result Suc>");
                return resultString;
            }

            char *resultString = malloc(sizeof(char) * 13);
            snprintf(resultString, 13, "<Result Err>");
            return resultString;
        }
    }

    char *unknown = malloc(sizeof(char) * 9);
    snprintf(unknown, 8, "%s", "unknown");
    return unknown;
}

    /* 5: vm/scanner.c */


void initScanner(Scanner *scanner, const char *source) {
    scanner->start = source;
    scanner->current = source;
    scanner->line = 1;
    scanner->rawString = false;
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isHexDigit(char c) {
    return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f') || (c == '_'));
}

static bool isAtEnd(Scanner *scanner) {
    return *scanner->current == '\0';
}

static char scan_advance(Scanner *scanner) {
    scanner->current++;
    return scanner->current[-1];
}

static char scan_peek(Scanner *scanner) {
    return *scanner->current;
}

static char scan_peekNext(Scanner *scanner) {
    if (isAtEnd(scanner)) return '\0';
    return scanner->current[1];
}

static bool scan_match(Scanner *scanner, char expected) {
    if (isAtEnd(scanner)) return false;
    if (*scanner->current != expected) return false;

    scanner->current++;
    return true;
}

static Token makeToken(Scanner *scanner, TokenType type) {
    Token token;
    token.type = type;
    token.start = scanner->start;
    token.length = (int) (scanner->current - scanner->start);
    token.line = scanner->line;

    return token;
}

static Token errorToken(Scanner *scanner, const char *message) {
    Token token;
    token.type = TOKEN_ERROR;
    token.start = message;
    token.length = (int) strlen(message);
    token.line = scanner->line;

    return token;
}

static void skipWhitespace(Scanner *scanner) {
    for (;;) {
        char c = scan_peek(scanner);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                scan_advance(scanner);
                break;

            case '\n':
                scanner->line++;
                scan_advance(scanner);
                break;

            case '/':
                if (scan_peekNext(scanner) == '*') {
                    // Multiline comments
                    scan_advance(scanner);
                    scan_advance(scanner);
                    while (true) {
                        while (scan_peek(scanner) != '*' && !isAtEnd(scanner)) {
                            if ((c = scan_advance(scanner)) == '\n') {
                                scanner->line++;
                            }
                        }

                        if (isAtEnd(scanner))
                            return;

                        if (scan_peekNext(scanner) == '/') {
                            break;
                        }
                        scan_advance(scanner);
                    }
                    scan_advance(scanner);
                    scan_advance(scanner);
                } else if (scan_peekNext(scanner) == '/') {
                    // A comment goes until the end of the line.
                    while (scan_peek(scanner) != '\n' && !isAtEnd(scanner)) scan_advance(scanner);
                } else {
                    return;
                }
                break;

            default:
                return;
        }
    }
}

static TokenType checkKeyword(Scanner *scanner, int start, int length,
                              const char *rest, TokenType type) {
    if (scanner->current - scanner->start == start + length &&
        memcmp(scanner->start + start, rest, length) == 0) {
        return type;
    }

    return TOKEN_IDENTIFIER;
}

static TokenType identifierType(Scanner *scanner) {
    switch (scanner->start[0]) {
        case 'a':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'b': {
                        return checkKeyword(scanner, 2, 6, "stract", TOKEN_ABSTRACT);
                    }

                    case 'n': {
                        return checkKeyword(scanner, 2, 1, "d", TOKEN_AND);
                    }

                    case 's': {
                        return checkKeyword(scanner, 2, 0, "", TOKEN_AS);
                    }
                }
            }
            break;
        case 'b':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'e':
                        return checkKeyword(scanner, 2, 1, "g", TOKEN_LEFT_BRACE);
                    case 'r':
                        return checkKeyword(scanner, 2, 3, "eak", TOKEN_BREAK);
                 }
             }
             break;
        case 'c':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'l':
                        return checkKeyword(scanner, 2, 3, "ass", TOKEN_CLASS);
                    case 'o':
                        // Skip second char
                        // Skip third char
                        if (scanner->current - scanner->start > 3) {
                            switch (scanner->start[3]) {
                                case 't':
                                    return checkKeyword(scanner, 4, 4, "inue", TOKEN_CONTINUE);
                                case 's':
                                    return checkKeyword(scanner, 4, 1, "t", TOKEN_CONST);
                            }
                        }

                }
            }
            break;
        case 'd':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'e':
                        return checkKeyword(scanner, 2, 1, "f", TOKEN_DEF);
                    case 'o':
                        return checkKeyword(scanner, 1, 1, "o", TOKEN_LEFT_BRACE);
                }
            }
            break;
        case 'e':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'n':
                        return checkKeyword(scanner, 2, 1, "d", TOKEN_RIGHT_BRACE);
                    case 'l':
                        return checkKeyword(scanner, 2, 2, "se", TOKEN_ELSE);
                }
            }
            break;
        case 'f':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'a':
                        return checkKeyword(scanner, 2, 3, "lse", TOKEN_FALSE);
                    case 'r':
                        return checkKeyword(scanner, 2, 2, "om", TOKEN_FROM);
                    case 'o':
                        if (scanner->current - scanner->start == 3)
                            return checkKeyword(scanner, 2, 1, "r", TOKEN_FOR);

                        if (TOKEN_IDENTIFIER == checkKeyword(scanner, 2, 5, "rever", 0))
                            return TOKEN_IDENTIFIER;

                        char *modified = (char *) scanner->start;
                        char replaced[] = "while (1) {";
                        for (int i = 0; i < 11; i++)
                            modified[i] = replaced[i];
                        scanner->start   = (const char *) modified;
                        scanner->current = scanner->start + 5;
                        return TOKEN_WHILE;
                }
            }
            break;
        case 'i':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'f':
                        return checkKeyword(scanner, 2, 0, "", TOKEN_IF);
                    case 'm':
                        return checkKeyword(scanner, 2, 4, "port", TOKEN_IMPORT);
                    case 's':
                      if (scanner->current - (scanner->start + 1) > 1)
                          return checkKeyword(scanner, 2, 3, "not", TOKEN_BANG_EQUAL);
                      return checkKeyword(scanner, 1, 1, "s", TOKEN_EQUAL_EQUAL);
                 }
            }
            break;
        case 'n':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'o':
                        return checkKeyword(scanner, 2, 1, "t", TOKEN_BANG);
                    case 'i':
                        return checkKeyword(scanner, 2, 1, "l", TOKEN_NIL);
                }
            }
            break;
        case 'o':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'r':
                        if (scanner->current - (scanner->start + 1) > 1) {
                            if (TOKEN_ELSE != checkKeyword(scanner, 2, 4, "else", TOKEN_ELSE))
                                return TOKEN_IDENTIFIER;

                            char *modified = (char *) scanner->start;
                            modified[0] = '}'; modified[1] = ' ';
                            scanner->start   = (const char *) modified;
                            scanner->current = scanner->start + 1;
                            return TOKEN_RIGHT_BRACE;
                        }
                    return checkKeyword(scanner, 1, 1, "r", TOKEN_OR);
                }
             }
             break;
        case 'r':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'e':
                        return checkKeyword(scanner, 2, 4, "turn", TOKEN_RETURN);
                }
            } else {
                if (scanner->start[1] == '"' || scanner->start[1] == '\'') {
                    scanner->rawString = true;
                    return TOKEN_R;
                }
            }
            break;
        case 's':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'u':
                        return checkKeyword(scanner, 2, 3, "per", TOKEN_SUPER);
                    case 't':
                        return checkKeyword(scanner, 2, 4, "atic", TOKEN_STATIC);
                }
            }
            break;
        case 't':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'h':
                  	  if (scanner->current - scanner->start > 1) {
                            switch (scanner->start[2]) {
                                case 'e':
                                    return checkKeyword(scanner, 3, 1, "n", TOKEN_LEFT_BRACE);
                                case 'i':
                                    return checkKeyword(scanner, 3, 1, "s", TOKEN_THIS);
                            }
                        }
                        break;
                    case 'r':
                        if (scanner->current - scanner->start > 1) {
                            switch (scanner->start[2]) {
                                case 'u':
                                    return checkKeyword(scanner, 3, 1, "e", TOKEN_TRUE);
                                case 'a':
                                    return checkKeyword(scanner, 3, 2, "it", TOKEN_TRAIT);
                            }
                        }
                    break;
                }
            }
            break;
        case 'u':
            return checkKeyword(scanner, 1, 2, "se", TOKEN_USE);
        case 'v':
            return checkKeyword(scanner, 1, 2, "ar", TOKEN_VAR);
        case 'w':
            if (scanner->current - scanner->start > 1) {
                switch (scanner->start[1]) {
                    case 'h':
                        return checkKeyword(scanner, 2, 3, "ile", TOKEN_WHILE);
                    case 'i':
                        return checkKeyword(scanner, 2, 2, "th", TOKEN_WITH);
                }
            }
            break;
    }

    return TOKEN_IDENTIFIER;
}

static Token identifier(Scanner *scanner) {
    while (isAlpha(scan_peek(scanner)) || isDigit(scan_peek(scanner))) scan_advance(scanner);

    return makeToken(scanner,identifierType(scanner));
}

static Token exponent(Scanner *scanner) {
    // Consume the "e"
    scan_advance(scanner);
    while (scan_peek(scanner) == '_') scan_advance(scanner);
    if (scan_peek(scanner) == '+' || scan_peek(scanner) == '-') {
        // Consume the "+ or -"
        scan_advance(scanner);
    }
    if (!isDigit(scan_peek(scanner)) && scan_peek(scanner) != '_') return errorToken(scanner, "Invalid exopnent literal");
    while (isDigit(scan_peek(scanner)) || scan_peek(scanner) == '_') scan_advance(scanner);
    return makeToken(scanner,TOKEN_NUMBER);
}

static Token number(Scanner *scanner) {
    while (isDigit(scan_peek(scanner)) || scan_peek(scanner) == '_') scan_advance(scanner);
    if (scan_peek(scanner) == 'e' || scan_peek(scanner) == 'E')
        return exponent(scanner);
    // Look for a fractional part.
    if (scan_peek(scanner) == '.' && (isDigit(scan_peekNext(scanner)))) {
        // Consume the "."
        scan_advance(scanner);
        while (isDigit(scan_peek(scanner)) || scan_peek(scanner) == '_') scan_advance(scanner);
        if (scan_peek(scanner) == 'e' || scan_peek(scanner) == 'E')
            return exponent(scanner);
    }
    return makeToken(scanner,TOKEN_NUMBER);
}

static Token hexNumber(Scanner *scanner) {
    while (scan_peek(scanner) == '_') scan_advance(scanner);
    if (scan_peek(scanner) == '0')scan_advance(scanner);
    if ((scan_peek(scanner) == 'x') || (scan_peek(scanner) == 'X')) {
        scan_advance(scanner);
        if (!isHexDigit(scan_peek(scanner))) return errorToken(scanner, "Invalid hex literal");
        while (isHexDigit(scan_peek(scanner))) scan_advance(scanner);
        return makeToken(scanner,TOKEN_NUMBER);
    } else return number(scanner);
}


static Token string(Scanner *scanner, char stringToken) {
    while (scan_peek(scanner) != stringToken && !isAtEnd(scanner)) {
        if (scan_peek(scanner) == '\n') {
            scanner->line++;
        } else if (scan_peek(scanner) == '\\' && !scanner->rawString) {
            scanner->current++;
        }
        scan_advance(scanner);
    }
    if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated string.");

    // The closing " or '.
    scan_advance(scanner);
    scanner->rawString = false;
    return makeToken(scanner,TOKEN_STRING);
}

void backTrack(Scanner *scanner) {
    scanner->current--;
}

Token scanToken(Scanner *scanner) {
    skipWhitespace(scanner);

    scanner->start = scanner->current;

    if (isAtEnd(scanner)) return makeToken(scanner, TOKEN_EOF);

    char c = scan_advance(scanner);

    if (isAlpha(c)) return identifier(scanner);
    if (isDigit(c)) return hexNumber(scanner);

    switch (c) {
        case '(':
            return makeToken(scanner, TOKEN_LEFT_PAREN);
        case ')':
            return makeToken(scanner, TOKEN_RIGHT_PAREN);
        case '{':
            return makeToken(scanner, TOKEN_LEFT_BRACE);
        case '}':
            return makeToken(scanner, TOKEN_RIGHT_BRACE);
        case '[':
            return makeToken(scanner, TOKEN_LEFT_BRACKET);
        case ']':
            return makeToken(scanner, TOKEN_RIGHT_BRACKET);
        case ';':
            return makeToken(scanner, TOKEN_SEMICOLON);
        case ':':
            return makeToken(scanner, TOKEN_COLON);
        case ',':
            return makeToken(scanner, TOKEN_COMMA);
        case '.':
            return makeToken(scanner, TOKEN_DOT);
        case '/': {
            if (scan_match(scanner, '=')) {
                return makeToken(scanner, TOKEN_DIVIDE_EQUALS);
            } else {
                return makeToken(scanner, TOKEN_SLASH);
            }
        }
        case '*': {
            if (scan_match(scanner, '=')) {
                return makeToken(scanner, TOKEN_MULTIPLY_EQUALS);
            } else if (scan_match(scanner, '*')) {
                return makeToken(scanner, TOKEN_STAR_STAR);
            } else {
                return makeToken(scanner, TOKEN_STAR);
            }
        }
        case '%':
            return makeToken(scanner, TOKEN_PERCENT);
        case '-': {
            if (scan_match(scanner, '=')) {
                return makeToken(scanner, TOKEN_MINUS_EQUALS);
            } else {
                return makeToken(scanner, TOKEN_MINUS);
            }
        }
        case '+': {
            if (scan_match(scanner, '=')) {
                return makeToken(scanner, TOKEN_PLUS_EQUALS);
            } else {
                return makeToken(scanner, TOKEN_PLUS);
            }
        }
        case '&':
            return makeToken(scanner, scan_match(scanner, '=') ? TOKEN_AMPERSAND_EQUALS : TOKEN_AMPERSAND);
        case '^':
            return makeToken(scanner, scan_match(scanner, '=') ? TOKEN_CARET_EQUALS : TOKEN_CARET);
        case '|':
            return makeToken(scanner, scan_match(scanner, '=') ? TOKEN_PIPE_EQUALS : TOKEN_PIPE);
        case '!':
            return makeToken(scanner, scan_match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
        case '=':
            if (scan_match(scanner, '=')) {
                return makeToken(scanner, TOKEN_EQUAL_EQUAL);
            } else if (scan_match(scanner, '>')) {
                return makeToken(scanner, TOKEN_ARROW);
            } else {
                return makeToken(scanner, TOKEN_EQUAL);
            }
        case '?':
            if (scan_match(scanner, '.')) {
                return makeToken(scanner, TOKEN_QUESTION_DOT);
            }
            return makeToken(scanner, TOKEN_QUESTION);
        case '<':
            return makeToken(scanner, scan_match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
        case '>':
            return makeToken(scanner, scan_match(scanner, '=') ?
                             TOKEN_GREATER_EQUAL : TOKEN_GREATER);
        case '"':
            return string(scanner, '"');
        case '\'':
            return string(scanner, '\'');
    }

    return errorToken(scanner, "Unexpected character.");
}

    /* 6: vm/compiler.c */


#ifdef DEBUG_PRINT_CODE


#endif

static Chunk *currentChunk(Compiler *compiler) {
    return &compiler->function->chunk;
}

static void errorAt(Parser *parser, Token *token, const char *message) {
    if (parser->panicMode) return;
    parser->panicMode = true;

    fprintf(stderr, "[%s line %d] Error", parser->module->name->chars, token->line);

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
        parser->current = scanToken(&parser->scanner);
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

    compiler->function = newFunction(parser->vm, parser->module, type);

    switch (type) {
        case TYPE_INITIALIZER:
        case TYPE_METHOD:
        case TYPE_STATIC:
        case TYPE_ABSTRACT:
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
                                                : function->module->name->chars);
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
static void declareVariable(Compiler *compiler, Token *name) {
    // Global variables are implicitly declared.
    if (compiler->scopeDepth == 0) return;

    // See if a local variable with this name is already declared in this
    // scope.
    // Token *name = &compiler->parser->previous;
    for (int i = compiler->localCount - 1; i >= 0; i--) {
        Local *local = &compiler->locals[i];
        if (local->depth != -1 && local->depth < compiler->scopeDepth) break;
        if (identifiersEqual(name, &local->name)) {
            errorAt(compiler->parser, name, "Variable with this name already declared in this scope.");
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

    declareVariable(compiler, &compiler->parser->previous);
    return 0;
}

static void defineVariable(Compiler *compiler, uint8_t global, bool constant) {
    if (compiler->scopeDepth == 0) {
        if (constant) {
            tableSet(compiler->parser->vm, &compiler->parser->vm->constants,
                     AS_STRING(currentChunk(compiler)->constants.values[global]), NIL_VAL);
        } else {
            // If it's not constant, remove
            tableDelete(compiler->parser->vm, &compiler->parser->vm->constants,
                        AS_STRING(currentChunk(compiler)->constants.values[global]));
        }

        emitBytes(compiler, OP_DEFINE_MODULE, global);
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

static void and_(Compiler *compiler, Token previousToken, bool canAssign) {
    UNUSED(previousToken);
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

static bool foldBinary(Compiler *compiler, TokenType operatorType) {
#define FOLD(operator)                                                         \
    do {                                                                       \
        Chunk *chunk = currentChunk(compiler);                                 \
        uint8_t index = chunk->code[chunk->count - 1];                         \
        uint8_t constant = chunk->code[chunk->count - 3];                      \
        if (chunk->code[chunk->count - 2] != OP_CONSTANT) return false;        \
        if (chunk->code[chunk->count - 4] != OP_CONSTANT) return false;        \
        chunk->constants.values[constant] = NUMBER_VAL(                        \
            AS_NUMBER(chunk->constants.values[constant]) operator              \
            AS_NUMBER(chunk->constants.values[index])                          \
        );                                                                     \
        chunk->constants.count--;                                              \
        chunk->count -= 2;                                                     \
        return true;                                                           \
    } while (false)

#define FOLD_FUNC(func)                                                        \
    do {                                                                       \
        Chunk *chunk = currentChunk(compiler);                                 \
        uint8_t index = chunk->code[chunk->count - 1];                         \
        uint8_t constant = chunk->code[chunk->count - 3];                      \
        if (chunk->code[chunk->count - 2] != OP_CONSTANT) return false;        \
        if (chunk->code[chunk->count - 4] != OP_CONSTANT) return false;        \
        chunk->constants.values[constant] = NUMBER_VAL(                        \
            func(                                                              \
                AS_NUMBER(chunk->constants.values[constant]),                  \
                AS_NUMBER(chunk->constants.values[index])                      \
            )                                                                  \
        );                                                                     \
        chunk->constants.count--;                                              \
        chunk->count -= 2;                                                     \
        return true;                                                           \
    } while (false)

    switch (operatorType) {
        case TOKEN_PLUS: {
            FOLD(+);
            return false;
        }

        case TOKEN_MINUS: {
            FOLD(-);
            return false;
        }

        case TOKEN_STAR: {
            FOLD(*);
            return false;
        }

        case TOKEN_SLASH: {
            FOLD(/);
            return false;
        }

        case TOKEN_PERCENT: {
            FOLD_FUNC(fmod);
            return false;
        }

        case TOKEN_STAR_STAR: {
            FOLD_FUNC(powf);
            return false;
        }

        default: {
            return false;
        }
    }
#undef FOLD
#undef FOLD_FUNC
}

static void binary(Compiler *compiler, Token previousToken, bool canAssign) {
    UNUSED(canAssign);

    TokenType operatorType = compiler->parser->previous.type;

    ParseRule *rule = getRule(operatorType);
    parsePrecedence(compiler, (Precedence) (rule->precedence + 1));

    TokenType currentToken = compiler->parser->previous.type;

    // Attempt constant fold.
    if ((previousToken.type == TOKEN_NUMBER) &&
        (currentToken == TOKEN_NUMBER || currentToken == TOKEN_LEFT_PAREN) &&
        foldBinary(compiler, operatorType)
            ) {
        return;
    }

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
            emitByte(compiler, OP_SUBTRACT);
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

static void ternary(Compiler *compiler, Token previousToken, bool canAssign) {
    UNUSED(previousToken);
    UNUSED(canAssign);
    // Jump to the else branch if the condition is false.
    int elseJump = emitJump(compiler, OP_JUMP_IF_FALSE);

    // Compile the then branch.
    emitByte(compiler, OP_POP); // Condition.
    expression(compiler);

    // Jump over the else branch when the if branch is taken.
    int endJump = emitJump(compiler, OP_JUMP);

    // Compile the else branch.
    patchJump(compiler, elseJump);
    emitByte(compiler, OP_POP); // Condition.

    consume(compiler, TOKEN_COLON, "Expected colon after ternary expression");
    expression(compiler);

    patchJump(compiler, endJump);
}

static void comp_call(Compiler *compiler, Token previousToken, bool canAssign) {
    UNUSED(previousToken);
    UNUSED(canAssign);

    int argCount = argumentList(compiler);
    emitBytes(compiler, OP_CALL, argCount);
}

static void dot(Compiler *compiler, Token previousToken, bool canAssign) {
    UNUSED(previousToken);

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
        emitByte(compiler, OP_SUBTRACT);
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

static void chain(Compiler *compiler, Token previousToken, bool canAssign) {
    // If the operand is not nil we want to stop, otherwise continue
    int endJump = emitJump(compiler, OP_JUMP_IF_NIL);

    dot(compiler, previousToken, canAssign);

    patchJump(compiler, endJump);
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
        int index = 0;
        uint8_t identifiers[255];
        int indexes[255];
        do {
            bool varKeyword = match(compiler, TOKEN_VAR);
            consume(compiler, TOKEN_IDENTIFIER, "Expect parameter name.");
            uint8_t paramConstant = identifierConstant(fnCompiler, &fnCompiler->parser->previous);
            declareVariable(fnCompiler, &fnCompiler->parser->previous);
            defineVariable(fnCompiler, paramConstant, false);

            if (type == TYPE_INITIALIZER && varKeyword) {
                identifiers[fnCompiler->function->propertyCount] = paramConstant;
                indexes[fnCompiler->function->propertyCount++] = index;
            } else if (varKeyword) {
                error(fnCompiler->parser, "var keyword in a function definition that is not a class constructor");
            }

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
            index++;
        } while (match(fnCompiler, TOKEN_COMMA));

        if (fnCompiler->function->arityOptional > 0) {
            emitByte(fnCompiler, OP_DEFINE_OPTIONAL);
            emitBytes(fnCompiler, fnCompiler->function->arity, fnCompiler->function->arityOptional);
        }

        if (fnCompiler->function->propertyCount > 0) {
            DictuVM *vm = fnCompiler->parser->vm;
            push(vm, OBJ_VAL(fnCompiler->function));
            fnCompiler->function->propertyIndexes = ALLOCATE(vm, int, fnCompiler->function->propertyCount);
            fnCompiler->function->propertyNames = ALLOCATE(vm, int, fnCompiler->function->propertyCount);
            pop(vm);

            for (int i = 0; i < fnCompiler->function->propertyCount; ++i) {
                fnCompiler->function->propertyNames[i] = identifiers[i];
                fnCompiler->function->propertyIndexes[i] = indexes[i];
            }

            emitBytes(fnCompiler, OP_SET_INIT_PROPERTIES, makeConstant(fnCompiler, OBJ_VAL(fnCompiler->function)));
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

    // We allocate the whole range for the worst case.
    // Also account for the null-byte.
    char *buffer = ALLOCATE(compiler->parser->vm, char, compiler->parser->previous.length + 1);
    char *current = buffer;

    // Strip it of any underscores.
    for (int i = 0; i < compiler->parser->previous.length; i++) {
        char c = compiler->parser->previous.start[i];

        if (c != '_') {
            *(current++) = c;
        }
    }

    // Terminate the string with a null character.
    *current = '\0';

    // Parse the string.
    double value = strtod(buffer, NULL);
    emitConstant(compiler, NUMBER_VAL(value));

    // Free the malloc'd buffer.
    FREE_ARRAY(compiler->parser->vm, char, buffer, compiler->parser->previous.length + 1);
}

static void or_(Compiler *compiler, Token previousToken, bool canAssign) {
    UNUSED(previousToken);
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
    int stringLength = parser->previous.length - 2;

    char *string = ALLOCATE(parser->vm, char, stringLength + 1);

    memcpy(string, parser->previous.start + 1, stringLength);
    int length = parseString(string, stringLength);

    // If there were escape chars and the string shrank, resize the buffer
    if (length != stringLength) {
        string = SHRINK_ARRAY(parser->vm, string, char, stringLength + 1, length + 1);
    }
    string[length] = '\0';

    emitConstant(compiler, OBJ_VAL(takeString(parser->vm, string, length)));
}

static void list(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    int count = 0;

    do {
        if (check(compiler, TOKEN_RIGHT_BRACKET))
            break;

        expression(compiler);
        count++;
    } while (match(compiler, TOKEN_COMMA));

    emitBytes(compiler, OP_NEW_LIST, count);
    consume(compiler, TOKEN_RIGHT_BRACKET, "Expected closing ']'");
}

static void dict(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    int count = 0;

    do {
        if (check(compiler, TOKEN_RIGHT_BRACE))
            break;

        expression(compiler);
        consume(compiler, TOKEN_COLON, "Expected ':'");
        expression(compiler);
        count++;
    } while (match(compiler, TOKEN_COMMA));

    emitBytes(compiler, OP_NEW_DICT, count);

    consume(compiler, TOKEN_RIGHT_BRACE, "Expected closing '}'");
}

static void subscript(Compiler *compiler, Token previousToken, bool canAssign) {
    UNUSED(previousToken);
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
        emitBytes(compiler, OP_SUBSCRIPT_PUSH, OP_ADD);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_MINUS_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_SUBSCRIPT_PUSH, OP_SUBTRACT);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_MULTIPLY_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_SUBSCRIPT_PUSH, OP_MULTIPLY);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_DIVIDE_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_SUBSCRIPT_PUSH, OP_DIVIDE);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_AMPERSAND_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_SUBSCRIPT_PUSH, OP_BITWISE_AND);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_CARET_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_SUBSCRIPT_PUSH, OP_BITWISE_XOR);
        emitByte(compiler, OP_SUBSCRIPT_ASSIGN);
    } else if (canAssign && match(compiler, TOKEN_PIPE_EQUALS)) {
        expression(compiler);
        emitBytes(compiler, OP_SUBSCRIPT_PUSH, OP_BITWISE_OR);
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
    } else if (setOp == OP_SET_MODULE) {
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
        ObjString *string = copyString(compiler->parser->vm, name.start, name.length);
        Value value;
        if (tableGet(&compiler->parser->vm->globals, string, &value)) {
            getOp = OP_GET_GLOBAL;
            canAssign = false;
        } else {
            getOp = OP_GET_MODULE;
            setOp = OP_SET_MODULE;
        }
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
        emitByte(compiler, OP_SUBTRACT);
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

static bool foldUnary(Compiler *compiler, TokenType operatorType) {
    TokenType valueToken = compiler->parser->previous.type;

    switch (operatorType) {
        case TOKEN_BANG: {
            if (valueToken == TOKEN_TRUE) {
                Chunk *chunk = currentChunk(compiler);
                chunk->code[chunk->count - 1] = OP_FALSE;
                return true;
            } else if (valueToken == TOKEN_FALSE) {
                Chunk *chunk = currentChunk(compiler);
                chunk->code[chunk->count - 1] = OP_TRUE;
                return true;
            }

            return false;
        }

        case TOKEN_MINUS: {
            if (valueToken == TOKEN_NUMBER) {
                Chunk *chunk = currentChunk(compiler);
                uint8_t constant = chunk->code[chunk->count - 1];
                chunk->constants.values[constant] = NUMBER_VAL(-AS_NUMBER(chunk->constants.values[constant]));
                return true;
            }

            return false;
        }

        default: {
            return false;
        }
    }
}

static void unary(Compiler *compiler, bool canAssign) {
    UNUSED(canAssign);

    TokenType operatorType = compiler->parser->previous.type;
    parsePrecedence(compiler, PREC_UNARY);

    // Constant fold.
    if (foldUnary(compiler, operatorType)) {
        return;
    }

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
        {NULL,     ternary,   PREC_ASSIGNMENT},               // TOKEN_QUESTION
        {NULL,     chain,   PREC_CHAIN},              // TOKEN_QUESTION_DOT
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
        {NULL,     NULL,      PREC_NONE},               // TOKEN_ABSTRACT
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
        {NULL,     NULL,      PREC_NONE},               // TOKEN_FROM
        {NULL,     NULL,      PREC_NONE},               // TOKEN_ERROR
};

static void parsePrecedence(Compiler *compiler, Precedence precedence) {
    Parser *parser = compiler->parser;
    advance(parser);
    ParsePrefixFn prefixRule = getRule(parser->previous.type)->prefix;
    if (prefixRule == NULL) {
        error(parser, "Expect expression.");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    prefixRule(compiler, canAssign);

    while (precedence <= getRule(parser->current.type)->precedence) {
        Token token = compiler->parser->previous;
        advance(parser);
        ParseInfixFn infixRule = getRule(parser->previous.type)->infix;
        infixRule(compiler, token, canAssign);
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

static void method(Compiler *compiler) {
    FunctionType type;

    compiler->class->staticMethod = false;
    type = TYPE_METHOD;

    if (match(compiler, TOKEN_STATIC)) {
        type = TYPE_STATIC;
        compiler->class->staticMethod = true;
    } else if (match(compiler, TOKEN_ABSTRACT)) {
        if (!compiler->class->abstractClass) {
            error(compiler->parser, "Abstract methods can only appear within abstract classes.");
            return;
        }

        type = TYPE_ABSTRACT;
    }

    consume(compiler, TOKEN_IDENTIFIER, "Expect method name.");
    uint8_t constant = identifierConstant(compiler, &compiler->parser->previous);

    // If the method is named "init", it's an initializer.
    if (compiler->parser->previous.length == 4 &&
        memcmp(compiler->parser->previous.start, "init", 4) == 0) {
        type = TYPE_INITIALIZER;
    }

    if (type != TYPE_ABSTRACT) {
        comp_function(compiler, type);
    } else {
        Compiler fnCompiler;

        // Setup function and parse parameters
        beginFunction(compiler, &fnCompiler, TYPE_ABSTRACT);
        endCompiler(&fnCompiler);

        if (check(compiler, TOKEN_LEFT_BRACE)) {
            error(compiler->parser, "Abstract methods can not have an implementation.");
            return;
        }
    }

    emitBytes(compiler, OP_METHOD, constant);
}

static void setupClassCompiler(Compiler *compiler, ClassCompiler *classCompiler, bool abstract) {
    classCompiler->name = compiler->parser->previous;
    classCompiler->hasSuperclass = false;
    classCompiler->enclosing = compiler->class;
    classCompiler->staticMethod = false;
    classCompiler->abstractClass = abstract;
    compiler->class = classCompiler;
}

static void parseClassBody(Compiler *compiler) {
    while (!check(compiler, TOKEN_RIGHT_BRACE) && !check(compiler, TOKEN_EOF)) {
        if (match(compiler, TOKEN_USE)) {
            useStatement(compiler);
        } else if (match(compiler, TOKEN_VAR)) {
            consume(compiler, TOKEN_IDENTIFIER, "Expect class variable name.");
            uint8_t name = identifierConstant(compiler, &compiler->parser->previous);

            consume(compiler, TOKEN_EQUAL, "Expect '=' after expression.");
            expression(compiler);
            emitBytes(compiler, OP_SET_CLASS_VAR, name);

            consume(compiler, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
        } else {
            method(compiler);
        }
    }
}

static void classDeclaration(Compiler *compiler) {
    consume(compiler, TOKEN_IDENTIFIER, "Expect class name.");
    uint8_t nameConstant = identifierConstant(compiler, &compiler->parser->previous);
    declareVariable(compiler, &compiler->parser->previous);

    ClassCompiler classCompiler;
    setupClassCompiler(compiler, &classCompiler, false);

    if (match(compiler, TOKEN_LESS)) {
        consume(compiler, TOKEN_IDENTIFIER, "Expect superclass name.");
        classCompiler.hasSuperclass = true;

        beginScope(compiler);

        // Store the superclass in a local variable named "super".
        variable(compiler, false);
        addLocal(compiler, syntheticToken("super"));

        emitBytes(compiler, OP_SUBCLASS, CLASS_DEFAULT);
    } else {
        emitBytes(compiler, OP_CLASS, CLASS_DEFAULT);
    }
    emitByte(compiler, nameConstant);

    consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    parseClassBody(compiler);

    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

    if (classCompiler.hasSuperclass) {
        endScope(compiler);

        // If there's a super class, check abstract methods have been defined
        emitByte(compiler, OP_END_CLASS);
    }

    defineVariable(compiler, nameConstant, false);
    compiler->class = compiler->class->enclosing;
}

static void abstractClassDeclaration(Compiler *compiler) {
    consume(compiler, TOKEN_CLASS, "Expect class keyword.");

    consume(compiler, TOKEN_IDENTIFIER, "Expect class name.");
    uint8_t nameConstant = identifierConstant(compiler, &compiler->parser->previous);
    declareVariable(compiler, &compiler->parser->previous);

    ClassCompiler classCompiler;
    setupClassCompiler(compiler, &classCompiler, true);

    if (match(compiler, TOKEN_LESS)) {
        consume(compiler, TOKEN_IDENTIFIER, "Expect superclass name.");
        classCompiler.hasSuperclass = true;

        beginScope(compiler);

        // Store the superclass in a local variable named "super".
        variable(compiler, false);
        addLocal(compiler, syntheticToken("super"));

        emitBytes(compiler, OP_SUBCLASS, CLASS_ABSTRACT);
    } else {
        emitBytes(compiler, OP_CLASS, CLASS_ABSTRACT);
    }
    emitByte(compiler, nameConstant);

    consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' before class body.");

    parseClassBody(compiler);

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
    declareVariable(compiler, &compiler->parser->previous);

    ClassCompiler classCompiler;
    setupClassCompiler(compiler, &classCompiler, false);

    emitBytes(compiler, OP_CLASS, CLASS_TRAIT);
    emitByte(compiler, nameConstant);

    consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' before trait body.");

    parseClassBody(compiler);

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
    if (match(compiler, TOKEN_LEFT_BRACKET)) {
        Token variables[255];
        int varCount = 0;

        do {
            consume(compiler, TOKEN_IDENTIFIER, "Expect variable name.");
            variables[varCount] = compiler->parser->previous;
            varCount++;
        } while (match(compiler, TOKEN_COMMA));

        consume(compiler, TOKEN_RIGHT_BRACKET, "Expect ']' after list destructure.");
        consume(compiler, TOKEN_EQUAL, "Expect '=' after list destructure.");

        expression(compiler);

        emitBytes(compiler, OP_UNPACK_LIST, varCount);

        if (compiler->scopeDepth == 0) {
            for (int i = varCount - 1; i >= 0; --i) {
                uint8_t identifier = identifierConstant(compiler, &variables[i]);
                defineVariable(compiler, identifier, constant);
            }
        } else {
            for (int i = 0; i < varCount; ++i) {
                declareVariable(compiler, &variables[i]);
                defineVariable(compiler, 0, constant);
            }
        }
    } else {
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
    }

    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
}

static void expressionStatement(Compiler *compiler) {
    Token previous = compiler->parser->previous;
    advance(compiler->parser);
    TokenType t = compiler->parser->current.type;

    for (int i = 0; i < compiler->parser->current.length; ++i) {
        backTrack(&compiler->parser->scanner);
    }
    compiler->parser->current = compiler->parser->previous;
    compiler->parser->previous = previous;

    expression(compiler);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after expression.");
    if (compiler->parser->vm->repl && t != TOKEN_EQUAL) {
        emitByte(compiler, OP_POP_REPL);
    } else {
        emitByte(compiler, OP_POP);
    }
}

static int getArgCount(uint8_t code, const ValueArray constants, int ip) {
    switch (code) {
        case OP_NIL:
        case OP_TRUE:
        case OP_FALSE:
        case OP_SUBSCRIPT:
        case OP_SUBSCRIPT_ASSIGN:
        case OP_SUBSCRIPT_PUSH:
        case OP_SLICE:
        case OP_POP:
        case OP_EQUAL:
        case OP_GREATER:
        case OP_LESS:
        case OP_ADD:
        case OP_MULTIPLY:
        case OP_DIVIDE:
        case OP_POW:
        case OP_MOD:
        case OP_NOT:
        case OP_NEGATE:
        case OP_CLOSE_UPVALUE:
        case OP_RETURN:
        case OP_EMPTY:
        case OP_END_CLASS:
        case OP_IMPORT_VARIABLE:
        case OP_IMPORT_END:
        case OP_USE:
        case OP_OPEN_FILE:
        case OP_CLOSE_FILE:
        case OP_BREAK:
        case OP_BITWISE_AND:
        case OP_BITWISE_XOR:
        case OP_BITWISE_OR:
        case OP_POP_REPL:
            return 0;

        case OP_CONSTANT:
        case OP_UNPACK_LIST:
        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
        case OP_GET_GLOBAL:
        case OP_GET_MODULE:
        case OP_DEFINE_MODULE:
        case OP_SET_MODULE:
        case OP_GET_UPVALUE:
        case OP_SET_UPVALUE:
        case OP_GET_PROPERTY:
        case OP_GET_PROPERTY_NO_POP:
        case OP_SET_PROPERTY:
        case OP_SET_CLASS_VAR:
        case OP_SET_INIT_PROPERTIES:
        case OP_GET_SUPER:
        case OP_CALL:
        case OP_METHOD:
        case OP_IMPORT:
        case OP_NEW_LIST:
        case OP_NEW_DICT:
            return 1;

        case OP_DEFINE_OPTIONAL:
        case OP_JUMP:
        case OP_JUMP_IF_NIL:
        case OP_JUMP_IF_FALSE:
        case OP_LOOP:
        case OP_INVOKE:
        case OP_SUPER:
        case OP_CLASS:
        case OP_SUBCLASS:
        case OP_IMPORT_BUILTIN:
            return 2;

        case OP_IMPORT_BUILTIN_VARIABLE:
            return 3;

        case OP_CLOSURE: {
            ObjFunction* loadedFn = AS_FUNCTION(constants.values[ip + 1]);

            // There is one byte for the constant, then two for each upvalue.
            return 1 + (loadedFn->upvalueCount * 2);
        }

        case OP_IMPORT_FROM: {
            int count = constants.values[ip + 1];
            return 1 + count;
        }
    }

    return 0;
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
            i += 1 + getArgCount(compiler->function->chunk.code[i], compiler->function->chunk.constants, i);
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
    if (match(compiler, TOKEN_STRING)) {
        int importConstant = makeConstant(compiler, OBJ_VAL(copyString(
                compiler->parser->vm,
                compiler->parser->previous.start + 1,
                compiler->parser->previous.length - 2)));

        emitBytes(compiler, OP_IMPORT, importConstant);
        emitByte(compiler, OP_POP);

        if (match(compiler, TOKEN_AS)) {
            uint8_t importName = parseVariable(compiler, "Expect import alias.", false);
            emitByte(compiler, OP_IMPORT_VARIABLE);
            defineVariable(compiler, importName, false);
        }
    } else {
        consume(compiler, TOKEN_IDENTIFIER, "Expect import identifier.");
        uint8_t importName = identifierConstant(compiler, &compiler->parser->previous);
        declareVariable(compiler, &compiler->parser->previous);

        int index = findBuiltinModule(
                (char *) compiler->parser->previous.start,
                compiler->parser->previous.length - compiler->parser->current.length
        );

        if (index == -1) {
            error(compiler->parser, "Unknown module");
        }

        emitBytes(compiler, OP_IMPORT_BUILTIN, index);
        emitByte(compiler, importName);

        defineVariable(compiler, importName, false);
    }

    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after import.");
    emitByte(compiler, OP_IMPORT_END);
}

static void fromImportStatement(Compiler *compiler) {
    if (match(compiler, TOKEN_STRING)) {
        int importConstant = makeConstant(compiler, OBJ_VAL(copyString(
                compiler->parser->vm,
                compiler->parser->previous.start + 1,
                compiler->parser->previous.length - 2)));

        consume(compiler, TOKEN_IMPORT, "Expect 'import' after import path.");
        emitBytes(compiler, OP_IMPORT, importConstant);
        emitByte(compiler, OP_POP);

        uint8_t variables[255];
        Token tokens[255];
        int varCount = 0;

        do {
            consume(compiler, TOKEN_IDENTIFIER, "Expect variable name.");
            tokens[varCount] = compiler->parser->previous;
            variables[varCount] = identifierConstant(compiler, &compiler->parser->previous);
            varCount++;

            if (varCount > 255) {
                error(compiler->parser, "Cannot have more than 255 variables.");
            }
        } while (match(compiler, TOKEN_COMMA));

        emitBytes(compiler, OP_IMPORT_FROM, varCount);

        for (int i = 0; i < varCount; ++i) {
            emitByte(compiler, variables[i]);
        }

        // This needs to be two separate loops as we need
        // all the variables popped before defining.
        if (compiler->scopeDepth == 0) {
            for (int i = varCount - 1; i >= 0; --i) {
                defineVariable(compiler, variables[i], false);
            }
        } else {
            for (int i = 0; i < varCount; ++i) {
                declareVariable(compiler, &tokens[i]);
                defineVariable(compiler, 0, false);
            }
        }

        emitByte(compiler, OP_IMPORT_END);
    } else {
        consume(compiler, TOKEN_IDENTIFIER, "Expect import identifier.");
        uint8_t importName = identifierConstant(compiler, &compiler->parser->previous);

        int index = findBuiltinModule(
                (char *) compiler->parser->previous.start,
                compiler->parser->previous.length
        );

        consume(compiler, TOKEN_IMPORT, "Expect 'import' after identifier");

        if (index == -1) {
            error(compiler->parser, "Unknown module");
        }

        uint8_t variables[255];
        Token tokens[255];
        int varCount = 0;

        do {
            consume(compiler, TOKEN_IDENTIFIER, "Expect variable name.");
            tokens[varCount] = compiler->parser->previous;
            variables[varCount] = identifierConstant(compiler, &compiler->parser->previous);
            varCount++;

            if (varCount > 255) {
                error(compiler->parser, "Cannot have more than 255 variables.");
            }
        } while (match(compiler, TOKEN_COMMA));

        emitBytes(compiler, OP_IMPORT_BUILTIN_VARIABLE, index);
        emitBytes(compiler, importName, varCount);

        for (int i = 0; i < varCount; ++i) {
            emitByte(compiler, variables[i]);
        }

        if (compiler->scopeDepth == 0) {
            for (int i = varCount - 1; i >= 0; --i) {
                defineVariable(compiler, variables[i], false);
            }
        } else {
            for (int i = 0; i < varCount; ++i) {
                declareVariable(compiler, &tokens[i]);
                defineVariable(compiler, 0, false);
            }
        }
    }

    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after import.");
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
    } else if (match(compiler, TOKEN_ABSTRACT)) {
        abstractClassDeclaration(compiler);
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
    } else if (match(compiler, TOKEN_FROM)) {
        fromImportStatement(compiler);
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
                backTrack(&parser->scanner);
                backTrack(&parser->scanner);
                parser->current = previous;
                expressionStatement(compiler);
                return;
            }
        }

        if (check(compiler, TOKEN_COLON)) {
            for (int i = 0; i < parser->current.length + parser->previous.length; ++i) {
                backTrack(&parser->scanner);
            }

            parser->current = previous;
            expressionStatement(compiler);
            return;
        }

        // Reset the scanner to the previous position
        for (int i = 0; i < parser->current.length; ++i) {
            backTrack(&parser->scanner);
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

ObjFunction *compile(DictuVM *vm, ObjModule *module, const char *source) {
    Parser parser;
    parser.vm = vm;
    parser.hadError = false;
    parser.panicMode = false;
    parser.module = module;

    Scanner scanner;
    initScanner(&scanner, source);
    parser.scanner = scanner;

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

void grayCompilerRoots(DictuVM *vm) {
    Compiler *compiler = vm->compiler;

    while (compiler != NULL) {
        grayObject(vm, (Obj *) compiler->function);
        grayTable(vm, &compiler->stringConstants);
        compiler = compiler->enclosing;
    }
}
    /* 7: vm/vm.c */


static void resetStack(DictuVM *vm) {
    vm->stackTop = vm->stack;
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
    vm->compiler = NULL;
}

void runtimeError(DictuVM *vm, const char *format, ...) {
    for (int i = vm->frameCount - 1; i >= 0; i--) {
        CallFrame *frame = &vm->frames[i];

        ObjFunction *function = frame->closure->function;

        // -1 because the IP is sitting on the next instruction to be
        // executed.
        size_t instruction = frame->ip - function->chunk.code - 1;
        fprintf(stderr, "[line %d] in ",
                function->chunk.lines[instruction]);

        if (function->name == NULL) {
            fprintf(stderr, "%s: ", function->module->name->chars);
            i = -1;
        } else {
            fprintf(stderr, "%s() [%s]: ", function->name->chars, function->module->name->chars);
        }

        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        fputs("\n", stderr);
        va_end(args);
    }

    resetStack(vm);
}

DictuVM *dictuInitVM(bool repl, int argc, char *argv[]) {
    DictuVM *vm = malloc(sizeof(*vm));

    if (vm == NULL) {
        printf("Unable to allocate memory\n");
        exit(71);
    }

    memset(vm, '\0', sizeof(DictuVM));

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
    initTable(&vm->resultMethods);

    vm->frames = ALLOCATE(vm, CallFrame, vm->frameCapacity);
    vm->initString = copyString(vm, "init", 4);

    // Native functions
    defineAllNatives(vm);

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
    declareResultMethods(vm);

    /**
     * Native classes which are not required to be
     * imported. For imported modules see optionals.c
     */
    createSystemModule(vm, argc, argv);
    createCModule(vm);

    if (vm->repl) {
        vm->replVar = copyString(vm, "_", 1);
    }

    return vm;
}

void dictuFreeVM(DictuVM *vm) {
    freeTable(vm, &vm->modules);
    freeTable(vm, &vm->globals);
    freeTable(vm, &vm->constants);
    freeTable(vm, &vm->strings);
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
    freeTable(vm, &vm->resultMethods);
    FREE_ARRAY(vm, CallFrame, vm->frames, vm->frameCapacity);
    vm->initString = NULL;
    vm->replVar = NULL;
    freeObjects(vm);

#if defined(DEBUG_TRACE_MEM) || defined(DEBUG_FINAL_MEM)
#ifdef __MINGW32__
    printf("Total memory usage: %lu\n", (unsigned long)vm->bytesAllocated);
#else
    printf("Total memory usage: %zu\n", vm->bytesAllocated);
#endif
#endif

    free(vm);
}

void push(DictuVM *vm, Value value) {
    *vm->stackTop = value;
    vm->stackTop++;
}

Value pop(DictuVM *vm) {
    vm->stackTop--;
    return *vm->stackTop;
}

Value peek(DictuVM *vm, int distance) {
    return vm->stackTop[-1 - distance];
}

static bool call(DictuVM *vm, ObjClosure *closure, int argCount) {
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

static bool callValue(DictuVM *vm, Value callee, int argCount) {
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
                // If it's not a default class, e.g a trait, it is not callable
                if (!(IS_DEFAULT_CLASS(callee))) {
                    break;
                }

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

            case OBJ_CLOSURE: {
                vm->stackTop[-argCount - 1] = callee;
                return call(vm, AS_CLOSURE(callee), argCount);
            }

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

static bool callNativeMethod(DictuVM *vm, Value method, int argCount) {
    NativeFn native = AS_NATIVE(method);

    Value result = native(vm, argCount, vm->stackTop - argCount - 1);

    if (IS_EMPTY(result))
        return false;

    vm->stackTop -= argCount + 1;
    push(vm, result);
    return true;
}

static bool invokeFromClass(DictuVM *vm, ObjClass *klass, ObjString *name,
                            int argCount) {
    // Look for the method.
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
        runtimeError(vm, "Undefined property '%s'.", name->chars);
        return false;
    }

    return call(vm, AS_CLOSURE(method), argCount);
}

static bool invoke(DictuVM *vm, ObjString *name, int argCount) {
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
                if (!tableGet(&module->values, name, &value)) {
                    runtimeError(vm, "Undefined property '%s'.", name->chars);
                    return false;
                }
                return callValue(vm, value, argCount);
            }

            case OBJ_CLASS: {
                ObjClass *instance = AS_CLASS(receiver);
                Value method;
                if (tableGet(&instance->methods, name, &method)) {
                    if (AS_CLOSURE(method)->function->type != TYPE_STATIC) {
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
                    if (IS_NATIVE(value)) {
                        return callNativeMethod(vm, value, argCount);
                    }

                    push(vm, peek(vm, 0));

                    for (int i = 2; i <= argCount + 1; i++) {
                        vm->stackTop[-i] = peek(vm, i);
                    }

                    return call(vm, AS_CLOSURE(value), argCount + 1);
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

            case OBJ_RESULT: {
                Value value;
                if (tableGet(&vm->resultMethods, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "Result has no method %s().", name->chars);
                return false;
            }

            case OBJ_ABSTRACT: {
                ObjAbstract *abstract = AS_ABSTRACT(receiver);

                Value value;
                if (tableGet(&abstract->values, name, &value)) {
                    return callNativeMethod(vm, value, argCount);
                }

                runtimeError(vm, "Object has no method %s().", name->chars);
                return false;
            }

            default:
                break;
        }
    }

    runtimeError(vm, "Only instances have methods.");
    return false;
}

static bool bindMethod(DictuVM *vm, ObjClass *klass, ObjString *name) {
    Value method;
    if (!tableGet(&klass->methods, name, &method)) {
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
// new open upvalue and adds it to the DictuVM's list of upvalues.
static ObjUpvalue *captureUpvalue(DictuVM *vm, Value *local) {
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

static void closeUpvalues(DictuVM *vm, Value *last) {
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

static void defineMethod(DictuVM *vm, ObjString *name) {
    Value method = peek(vm, 0);
    ObjClass *klass = AS_CLASS(peek(vm, 1));

    if (AS_CLOSURE(method)->function->type == TYPE_ABSTRACT) {
        tableSet(vm, &klass->abstractMethods, name, method);
    } else {
        tableSet(vm, &klass->methods, name, method);
    }
    pop(vm);
}

static void createClass(DictuVM *vm, ObjString *name, ObjClass *superclass, ClassType type) {
    ObjClass *klass = newClass(vm, name, superclass, type);
    push(vm, OBJ_VAL(klass));

    // Inherit methods.
    if (superclass != NULL) {
        tableAddAll(vm, &superclass->methods, &klass->methods);
        tableAddAll(vm, &superclass->abstractMethods, &klass->abstractMethods);
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

static void concatenate(DictuVM *vm) {
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

static void setReplVar(DictuVM *vm, Value value) {
    tableSet(vm, &vm->globals, vm->replVar, value);
}

static DictuInterpretResult run(DictuVM *vm) {

    CallFrame *frame = &vm->frames[vm->frameCount - 1];
    register uint8_t* ip = frame->ip;

    #define READ_BYTE() (*ip++)
    #define READ_SHORT() \
        (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))

    #define READ_CONSTANT() \
                (frame->closure->function->chunk.constants.values[READ_BYTE()])

    #define READ_STRING() AS_STRING(READ_CONSTANT())

    #define BINARY_OP(valueType, op, type)                                                                \
        do {                                                                                              \
          if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) {                                       \
              int firstValLength = 0;                                                                     \
              int secondValLength = 0;                                                                    \
              char *firstVal = valueTypeToString(vm, peek(vm, 1), &firstValLength);                       \
              char *secondVal = valueTypeToString(vm, peek(vm, 0), &secondValLength);                     \
                                                                                                          \
              STORE_FRAME;                                                                                \
              runtimeError(vm, "Unsupported operand types for "#op": '%s', '%s'", firstVal, secondVal);   \
              FREE_ARRAY(vm, char, firstVal, firstValLength + 1);                                         \
              FREE_ARRAY(vm, char, secondVal, secondValLength + 1);                                       \
              return INTERPRET_RUNTIME_ERROR;                                                             \
          }                                                                                               \
                                                                                                          \
          type b = AS_NUMBER(pop(vm));                                                                    \
          type a = AS_NUMBER(pop(vm));                                                                    \
          push(vm, valueType(a op b));                                                                    \
        } while (false)

    #define BINARY_OP_FUNCTION(valueType, op, func, type)                                                                \
        do {                                                                                              \
          if (!IS_NUMBER(peek(vm, 0)) || !IS_NUMBER(peek(vm, 1))) {                                       \
              int firstValLength = 0;                                                                     \
              int secondValLength = 0;                                                                    \
              char *firstVal = valueTypeToString(vm, peek(vm, 0), &firstValLength);                       \
              char *secondVal = valueTypeToString(vm, peek(vm, 1), &secondValLength);                     \
                                                                                                          \
              STORE_FRAME;                                                                                \
              runtimeError(vm, "Unsupported operand types for "#op": '%s', '%s'", firstVal, secondVal);   \
              FREE_ARRAY(vm, char, firstVal, firstValLength + 1);                                         \
              FREE_ARRAY(vm, char, secondVal, secondValLength + 1);                                       \
              return INTERPRET_RUNTIME_ERROR;                                                             \
          }                                                                                               \
                                                                                                          \
          type b = AS_NUMBER(pop(vm));                                                                    \
          type a = AS_NUMBER(pop(vm));                                                                    \
          push(vm, valueType(func(a, b)));                                                                \
        } while (false)

    #define STORE_FRAME frame->ip = ip

    #define RUNTIME_ERROR(...)                                              \
        do {                                                                \
            STORE_FRAME;                                                    \
            runtimeError(vm, __VA_ARGS__);                                  \
            return INTERPRET_RUNTIME_ERROR;                                 \
        } while (0)

    #define RUNTIME_ERROR_TYPE(error, distance)                                    \
        do {                                                                       \
            STORE_FRAME;                                                           \
            int valLength = 0;                                                     \
            char *val = valueTypeToString(vm, peek(vm, distance), &valLength);     \
            runtimeError(vm, error, val);                                          \
            FREE_ARRAY(vm, char, val, valLength + 1);                              \
            return INTERPRET_RUNTIME_ERROR;                                        \
        } while (0)

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
                        (int) (ip - frame->closure->function->chunk.code));                \
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
            if (!tableGet(&vm->globals, name, &value)) {
                RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
            }
            push(vm, value);
            DISPATCH();
        }

        CASE_CODE(GET_MODULE): {
            ObjString *name = READ_STRING();
            Value value;
            if (!tableGet(&frame->closure->function->module->values, name, &value)) {
                RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
            }
            push(vm, value);
            DISPATCH();
        }

        CASE_CODE(DEFINE_MODULE): {
            ObjString *name = READ_STRING();
            tableSet(vm, &frame->closure->function->module->values, name, peek(vm, 0));
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(SET_MODULE): {
            ObjString *name = READ_STRING();
            if (tableSet(vm, &frame->closure->function->module->values, name, peek(vm, 0))) {
                tableDelete(vm, &frame->closure->function->module->values, name);
                RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
            }
            DISPATCH();
        }

        CASE_CODE(DEFINE_OPTIONAL): {
            int arity = READ_BYTE();
            int arityOptional = READ_BYTE();
            int argCount = vm->stackTop - frame->slots - arityOptional - 1;

            // Temp array while we shuffle the stack.
            // Can not have more than 255 args to a function, so
            // we can define this with a constant limit
            Value values[255];
            int index;

            for (index = 0; index < arityOptional + argCount; index++) {
                values[index] = pop(vm);
            }

            --index;

            for (int i = 0; i < argCount; i++) {
                push(vm, values[index - i]);
            }

            // Calculate how many "default" values are required
            int remaining = arity + arityOptional - argCount;

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

                if (bindMethod(vm, instance->klass, name)) {
                    DISPATCH();
                }

                // Check class for properties
                ObjClass *klass = instance->klass;

                while (klass != NULL) {
                    if (tableGet(&klass->properties, name, &value)) {
                        pop(vm); // Instance.
                        push(vm, value);
                        DISPATCH();
                    }

                    klass = klass->superclass;
                }

                RUNTIME_ERROR("'%s' instance has no property: '%s'.", instance->klass->name->chars, name->chars);
            } else if (IS_MODULE(peek(vm, 0))) {
                ObjModule *module = AS_MODULE(peek(vm, 0));
                ObjString *name = READ_STRING();
                Value value;
                if (tableGet(&module->values, name, &value)) {
                    pop(vm); // Module.
                    push(vm, value);
                    DISPATCH();
                }

                RUNTIME_ERROR("'%s' module has no property: '%s'.", module->name->chars, name->chars);
            } else if (IS_CLASS(peek(vm, 0))) {
                ObjClass *klass = AS_CLASS(peek(vm, 0));
                // Used to keep a reference to the class for the runtime error below
                ObjClass *klassStore = klass;
                ObjString *name = READ_STRING();

                Value value;
                while (klass != NULL) {
                    if (tableGet(&klass->properties, name, &value)) {
                        pop(vm); // Class.
                        push(vm, value);
                        DISPATCH();
                    }

                    klass = klass->superclass;
                }

                RUNTIME_ERROR("'%s' class has no property: '%s'.", klassStore->name->chars, name->chars);
            }

            RUNTIME_ERROR_TYPE("'%s' type has no properties", 0);
        }

        CASE_CODE(GET_PROPERTY_NO_POP): {
            if (!IS_INSTANCE(peek(vm, 0))) {
                RUNTIME_ERROR("Only instances have properties.");
            }

            ObjInstance *instance = AS_INSTANCE(peek(vm, 0));
            ObjString *name = READ_STRING();
            Value value;
            if (tableGet(&instance->fields, name, &value)) {
                push(vm, value);
                DISPATCH();
            }

            if (bindMethod(vm, instance->klass, name)) {
                DISPATCH();
            }

            // Check class for properties
            ObjClass *klass = instance->klass;

            while (klass != NULL) {
                if (tableGet(&klass->properties, name, &value)) {
                    push(vm, value);
                    DISPATCH();
                }

                klass = klass->superclass;
            }

            RUNTIME_ERROR("'%s' instance has no property: '%s'.", instance->klass->name->chars, name->chars);
        }

        CASE_CODE(SET_PROPERTY): {
            if (IS_INSTANCE(peek(vm, 1))) {
                ObjInstance *instance = AS_INSTANCE(peek(vm, 1));
                tableSet(vm, &instance->fields, READ_STRING(), peek(vm, 0));
                pop(vm);
                pop(vm);
                push(vm, NIL_VAL);
                DISPATCH();
            } else if (IS_CLASS(peek(vm, 1))) {
                ObjClass *klass = AS_CLASS(peek(vm, 1));
                tableSet(vm, &klass->properties, READ_STRING(), peek(vm, 0));
                pop(vm);
                pop(vm);
                push(vm, NIL_VAL);
                DISPATCH();
            }

            RUNTIME_ERROR_TYPE("Can not set property on type '%s'", 1);
        }

        CASE_CODE(SET_CLASS_VAR): {
            // No type check required as this opcode is only ever emitted when parsing a class
            ObjClass *klass = AS_CLASS(peek(vm, 1));
            tableSet(vm, &klass->properties, READ_STRING(), peek(vm, 0));
            pop(vm);
            DISPATCH();
        }

        CASE_CODE(SET_INIT_PROPERTIES): {
            ObjFunction *function = AS_FUNCTION(READ_CONSTANT());
            int argCount = function->arity + function->arityOptional;
            ObjInstance *instance = AS_INSTANCE(peek(vm, function->arity + function->arityOptional));

            for (int i = 0; i < function->propertyCount; ++i) {
                ObjString *propertyName = AS_STRING(function->chunk.constants.values[function->propertyNames[i]]);
                tableSet(vm, &instance->fields, propertyName, peek(vm, argCount - function->propertyIndexes[i] - 1));
            }

            DISPATCH();
        }

        CASE_CODE(GET_SUPER): {
            ObjString *name = READ_STRING();
            ObjClass *superclass = AS_CLASS(pop(vm));

            if (!bindMethod(vm, superclass, name)) {
                RUNTIME_ERROR("Undefined property '%s'.", name->chars);
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

                ObjList *finalList = newList(vm);
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
                RUNTIME_ERROR("Unsupported operand types for +: %s, %s", valueToString(peek(vm, 0)), valueToString(peek(vm, 1)));
            }
            DISPATCH();
        }

        CASE_CODE(SUBTRACT): {
            BINARY_OP(NUMBER_VAL, -, double);
            DISPATCH();
        }

        CASE_CODE(MULTIPLY):
            BINARY_OP(NUMBER_VAL, *, double);
            DISPATCH();

        CASE_CODE(DIVIDE):
            BINARY_OP(NUMBER_VAL, /, double);
            DISPATCH();

        CASE_CODE(POW): {
            BINARY_OP_FUNCTION(NUMBER_VAL, **, powf, double);
            DISPATCH();
        }

        CASE_CODE(MOD): {
            BINARY_OP_FUNCTION(NUMBER_VAL, **, fmod, double);
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
                RUNTIME_ERROR_TYPE("Unsupported operand type for unary -: '%s'", 0);
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

        CASE_CODE(JUMP_IF_NIL): {
            uint16_t offset = READ_SHORT();
            if (IS_NIL(peek(vm, 0))) ip += offset;
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
                vm->lastModule = AS_MODULE(moduleVal);
                push(vm, NIL_VAL);
                DISPATCH();
            }

            char path[PATH_MAX];
            if (!resolvePath(frame->closure->function->module->path->chars, fileName->chars, path)) {
                RUNTIME_ERROR("Could not open file \"%s\".", fileName->chars);
            }

            char *source = readFile(vm, path);

            if (source == NULL) {
                RUNTIME_ERROR("Could not open file \"%s\".", fileName->chars);
            }

            ObjString *pathObj = copyString(vm, path, strlen(path));
            push(vm, OBJ_VAL(pathObj));
            ObjModule *module = newModule(vm, pathObj);
            module->path = dirname(vm, path, strlen(path));
            vm->lastModule = module;
            pop(vm);

            push(vm, OBJ_VAL(module));
            ObjFunction *function = compile(vm, module, source);
            pop(vm);

            FREE_ARRAY(vm, char, source, strlen(source) + 1);

            if (function == NULL) return INTERPRET_COMPILE_ERROR;
            push(vm, OBJ_VAL(function));
            ObjClosure *closure = newClosure(vm, function);
            pop(vm);
            push(vm, OBJ_VAL(closure));

            frame->ip = ip;
            call(vm, closure, 0);
            frame = &vm->frames[vm->frameCount - 1];
            ip = frame->ip;

            DISPATCH();
        }

        CASE_CODE(IMPORT_BUILTIN): {
            int index = READ_BYTE();
            ObjString *fileName = READ_STRING();
            Value moduleVal;

            // If we have imported this module already, skip.
            if (tableGet(&vm->modules, fileName, &moduleVal)) {
                push(vm, moduleVal);
                DISPATCH();
            }

            ObjModule *module = importBuiltinModule(vm, index);

            push(vm, OBJ_VAL(module));
            DISPATCH();
        }

        CASE_CODE(IMPORT_BUILTIN_VARIABLE): {
            int index = READ_BYTE();
            ObjString *fileName = READ_STRING();
            int varCount = READ_BYTE();

            Value moduleVal;
            ObjModule *module;

            if (tableGet(&vm->modules, fileName, &moduleVal)) {
                module = AS_MODULE(moduleVal);
            } else {
                module = importBuiltinModule(vm, index);
            }

            for (int i = 0; i < varCount; i++) {
                Value moduleVariable;
                ObjString *variable = READ_STRING();

                if (!tableGet(&module->values, variable, &moduleVariable)) {
                    RUNTIME_ERROR("%s can't be found in module %s", variable->chars, module->name->chars);
                }

                push(vm, moduleVariable);
            }

            DISPATCH();
        }

        CASE_CODE(IMPORT_VARIABLE): {
            push(vm, OBJ_VAL(vm->lastModule));
            DISPATCH();
        }

        CASE_CODE(IMPORT_FROM): {
            int varCount = READ_BYTE();

            for (int i = 0; i < varCount; i++) {
                Value moduleVariable;
                ObjString *variable = READ_STRING();

                if (!tableGet(&vm->lastModule->values, variable, &moduleVariable)) {
                    RUNTIME_ERROR("%s can't be found in module %s", variable->chars, vm->lastModule->name->chars);
                }

                push(vm, moduleVariable);
            }

            DISPATCH();
        }

        CASE_CODE(IMPORT_END): {
            vm->lastModule = frame->closure->function->module;
            DISPATCH();
        }

        CASE_CODE(NEW_LIST): {
            int count = READ_BYTE();
            ObjList *list = newList(vm);
            push(vm, OBJ_VAL(list));

            for (int i = count; i > 0; i--) {
                writeValueArray(vm, &list->values, peek(vm, i));
            }

            vm->stackTop -= count + 1;
            push(vm, OBJ_VAL(list));
            DISPATCH();
        }

        CASE_CODE(UNPACK_LIST): {
            int varCount = READ_BYTE();

            if (!IS_LIST(peek(vm, 0))) {
                RUNTIME_ERROR("Attempting to unpack a value which is not a list.");
            }

            ObjList *list = AS_LIST(pop(vm));

            if (varCount != list->values.count) {
                if (varCount < list->values.count) {
                    RUNTIME_ERROR("Too many values to unpack");
                } else {
                    RUNTIME_ERROR("Not enough values to unpack");
                }
            }

            for (int i = 0; i < list->values.count; ++i) {
                push(vm, list->values.values[i]);
            }

            DISPATCH();
        }

        CASE_CODE(NEW_DICT): {
            int count = READ_BYTE();
            ObjDict *dict = newDict(vm);
            push(vm, OBJ_VAL(dict));

            for (int i = count * 2; i > 0; i -= 2) {
                if (!isValidKey(peek(vm, i))) {
                    RUNTIME_ERROR("Dictionary key must be an immutable type.");
                }

                dictSet(vm, dict, peek(vm, i), peek(vm, i - 1));
            }

            vm->stackTop -= count * 2 + 1;
            push(vm, OBJ_VAL(dict));

            DISPATCH();
        }

        CASE_CODE(SUBSCRIPT): {
            Value indexValue = peek(vm, 0);
            Value subscriptValue = peek(vm, 1);

            if (!IS_OBJ(subscriptValue)) {
                RUNTIME_ERROR_TYPE("'%s' is not subscriptable", 1);
            }

            switch (getObjType(subscriptValue)) {
                case OBJ_LIST: {
                    if (!IS_NUMBER(indexValue)) {
                        RUNTIME_ERROR("List index must be a number.");
                    }

                    ObjList *list = AS_LIST(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    // Allow negative indexes
                    if (index < 0)
                        index = list->values.count + index;

                    if (index >= 0 && index < list->values.count) {
                        pop(vm);
                        pop(vm);
                        push(vm, list->values.values[index]);
                        DISPATCH();
                    }

                    RUNTIME_ERROR("List index out of bounds.");
                }

                case OBJ_STRING: {
                    ObjString *string = AS_STRING(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    // Allow negative indexes
                    if (index < 0)
                        index = string->length + index;

                    if (index >= 0 && index < string->length) {
                        pop(vm);
                        pop(vm);
                        push(vm, OBJ_VAL(copyString(vm, &string->chars[index], 1)));
                        DISPATCH();
                    }

                    RUNTIME_ERROR("String index out of bounds.");
                }

                case OBJ_DICT: {
                    ObjDict *dict = AS_DICT(subscriptValue);
                    if (!isValidKey(indexValue)) {
                        RUNTIME_ERROR("Dictionary key must be an immutable type.");
                    }

                    Value v;
                    pop(vm);
                    pop(vm);
                    if (dictGet(dict, indexValue, &v)) {
                        push(vm, v);
                        DISPATCH();
                    }

                    RUNTIME_ERROR("Key %s does not exist within dictionary.", valueToString(indexValue));
                }

                default: {
                    RUNTIME_ERROR_TYPE("'%s' is not subscriptable", 1);
                }
            }
        }

        CASE_CODE(SUBSCRIPT_ASSIGN): {
            Value assignValue = peek(vm, 0);
            Value indexValue = peek(vm, 1);
            Value subscriptValue = peek(vm, 2);

            if (!IS_OBJ(subscriptValue)) {
                RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
            }

            switch (getObjType(subscriptValue)) {
                case OBJ_LIST: {
                    if (!IS_NUMBER(indexValue)) {
                        RUNTIME_ERROR("List index must be a number.");
                    }

                    ObjList *list = AS_LIST(subscriptValue);
                    int index = AS_NUMBER(indexValue);

                    if (index < 0)
                        index = list->values.count + index;

                    if (index >= 0 && index < list->values.count) {
                        list->values.values[index] = assignValue;
                        pop(vm);
                        pop(vm);
                        pop(vm);
                        push(vm, NIL_VAL);
                        DISPATCH();
                    }

                    RUNTIME_ERROR("List index out of bounds.");
                }

                case OBJ_DICT: {
                    ObjDict *dict = AS_DICT(subscriptValue);
                    if (!isValidKey(indexValue)) {
                        RUNTIME_ERROR("Dictionary key must be an immutable type.");
                    }

                    dictSet(vm, dict, indexValue, assignValue);
                    pop(vm);
                    pop(vm);
                    pop(vm);
                    push(vm, NIL_VAL);
                    DISPATCH();
                }

                default: {
                    RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
                }
            }
        }

        CASE_CODE(SUBSCRIPT_PUSH): {
            Value value = peek(vm, 0);
            Value indexValue = peek(vm, 1);
            Value subscriptValue = peek(vm, 2);

            if (!IS_OBJ(subscriptValue)) {
                RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
            }

            switch (getObjType(subscriptValue)) {
                case OBJ_LIST: {
                    if (!IS_NUMBER(indexValue)) {
                        RUNTIME_ERROR("List index must be a number.");
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

                    RUNTIME_ERROR("List index out of bounds.");
                }

                case OBJ_DICT: {
                    ObjDict *dict = AS_DICT(subscriptValue);
                    if (!isValidKey(indexValue)) {
                        RUNTIME_ERROR("Dictionary key must be an immutable type.");
                    }

                    Value dictValue;
                    if (!dictGet(dict, indexValue, &dictValue)) {
                        RUNTIME_ERROR("Key %s does not exist within dictionary.", valueToString(indexValue));
                    }

                    vm->stackTop[-1] = dictValue;
                    push(vm, value);

                    DISPATCH();
                }

                default: {
                    RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
                }
            }
            DISPATCH();
        }

        CASE_CODE(SLICE): {
            Value sliceEndIndex = peek(vm, 0);
            Value sliceStartIndex = peek(vm, 1);
            Value objectValue = peek(vm, 2);

            if (!IS_OBJ(objectValue)) {
                RUNTIME_ERROR("Can only slice on lists and strings.");
            }

            if ((!IS_NUMBER(sliceStartIndex) && !IS_EMPTY(sliceStartIndex)) || (!IS_NUMBER(sliceEndIndex) && !IS_EMPTY(sliceEndIndex))) {
                RUNTIME_ERROR("Slice index must be a number.");
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
                    ObjList *createdList = newList(vm);
                    push(vm, OBJ_VAL(createdList));
                    ObjList *list = AS_LIST(objectValue);

                    if (IS_EMPTY(sliceEndIndex)) {
                        indexEnd = list->values.count;
                    } else {
                        indexEnd = AS_NUMBER(sliceEndIndex);

                        if (indexEnd > list->values.count) {
                            indexEnd = list->values.count;
                        } else if (indexEnd < 0) {
                            indexEnd = list->values.count + indexEnd;
                        }
                    }

                    for (int i = indexStart; i < indexEnd; i++) {
                        writeValueArray(vm, &createdList->values, list->values.values[i]);
                    }

                    pop(vm);
                    returnVal = OBJ_VAL(createdList);

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
                        }  else if (indexEnd < 0) {
                            indexEnd = string->length + indexEnd;
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
                    RUNTIME_ERROR_TYPE("'%s' does not support item assignment", 2);
                }
            }

            pop(vm);
            pop(vm);
            pop(vm);

            push(vm, returnVal);
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

        CASE_CODE(CLASS): {
            ClassType type = READ_BYTE();
            createClass(vm, READ_STRING(), NULL, type);
            DISPATCH();
        }

        CASE_CODE(SUBCLASS): {
            ClassType type = READ_BYTE();

            Value superclass = peek(vm, 0);
            if (!IS_CLASS(superclass)) {
                RUNTIME_ERROR("Superclass must be a class.");
            }

            if (IS_TRAIT(superclass)) {
                RUNTIME_ERROR("Superclass can not be a trait.");
            }

            createClass(vm, READ_STRING(), AS_CLASS(superclass), type);
            DISPATCH();
        }

        CASE_CODE(END_CLASS): {
            ObjClass *klass = AS_CLASS(peek(vm, 0));

            // If super class is abstract, ensure we have defined all abstract methods
            for (int i = 0; i < klass->abstractMethods.capacityMask + 1; i++) {
                if (klass->abstractMethods.entries[i].key == NULL) {
                    continue;
                }

                Value _;
                if (!tableGet(&klass->methods, klass->abstractMethods.entries[i].key, &_)) {
                    RUNTIME_ERROR("Class %s does not implement abstract method %s", klass->name->chars, klass->abstractMethods.entries[i].key->chars);
                }
            }
            DISPATCH();
        }

        CASE_CODE(METHOD):
            defineMethod(vm, READ_STRING());
            DISPATCH();

        CASE_CODE(USE): {
            Value trait = peek(vm, 0);
            if (!IS_TRAIT(trait)) {
                RUNTIME_ERROR("Can only 'use' with a trait");
            }

            ObjClass *klass = AS_CLASS(peek(vm, 1));

            tableAddAll(vm, &AS_CLASS(trait)->methods, &klass->methods);
            pop(vm); // pop the trait

            DISPATCH();
        }

        CASE_CODE(OPEN_FILE): {
            Value openType = peek(vm, 0);
            Value fileName = peek(vm, 1);

            if (!IS_STRING(openType)) {
                RUNTIME_ERROR("File open type must be a string");
            }

            if (!IS_STRING(fileName)) {
                RUNTIME_ERROR("Filename must be a string");
            }

            ObjString *openTypeString = AS_STRING(openType);
            ObjString *fileNameString = AS_STRING(fileName);

            ObjFile *file = newFile(vm);
            file->file = fopen(fileNameString->chars, openTypeString->chars);
            file->path = fileNameString->chars;
            file->openType = openTypeString->chars;

            if (file->file == NULL) {
                RUNTIME_ERROR("Unable to open file '%s'", file->path);
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
#undef BINARY_OP_FUNCTION
#undef STORE_FRAME
#undef RUNTIME_ERROR

    return INTERPRET_RUNTIME_ERROR;
}

DictuInterpretResult dictuInterpret(DictuVM *vm, char *moduleName, char *source) {
    ObjString *name = copyString(vm, moduleName, strlen(moduleName));
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    pop(vm);

    push(vm, OBJ_VAL(module));
    module->path = getDirectory(vm, moduleName);
    pop(vm);

    ObjFunction *function = compile(vm, module, source);
    if (function == NULL) return INTERPRET_COMPILE_ERROR;
    push(vm, OBJ_VAL(function));
    ObjClosure *closure = newClosure(vm, function);
    pop(vm);
    push(vm, OBJ_VAL(closure));
    callValue(vm, OBJ_VAL(closure), 0);
    DictuInterpretResult result = run(vm);

    return result;
}

/*** EXTENSIONS ***/

Table *vm_get_globals(DictuVM *vm) {
    return &vm->globals;
}

size_t vm_sizeof(void) {
    return sizeof(DictuVM);
}

ObjModule *vm_module_get(DictuVM *vm, char *name, int len) {
    Value moduleVal;
    ObjString *string = copyString (vm, name, len);
    if (!tableGet(&vm->modules, string, &moduleVal))
        return NULL;
    return AS_MODULE(moduleVal);
}

Table *vm_get_module_table(DictuVM *vm, char *name, int len) {
    ObjModule *module = vm_module_get(vm, name, len);
    if (NULL == module)
        return NULL;
    return &module->values;
}

Value *vm_table_get_value(DictuVM *vm, Table *table, ObjString *obj, Value *value){
    UNUSED(vm);
    if (false == tableGet(table, obj, value))
        return NULL;
    return value;
}

/*** EXTENSIONS END ***/

    /* 8: vm/natives.c */


// Native functions
static Value typeNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "type() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    int length = 0;
    char *type = valueTypeToString(vm, args[0], &length);
    return OBJ_VAL(takeString(vm, type, length));
}

static Value setNative(DictuVM *vm, int argCount, Value *args) {
    ObjSet *set = newSet(vm);
    push(vm, OBJ_VAL(set));

    for (int i = 0; i < argCount; i++) {
        setInsert(vm, set, args[i]);
    }
    pop(vm);

    return OBJ_VAL(set);
}

static Value inputNative(DictuVM *vm, int argCount, Value *args) {
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
    char *line = ALLOCATE(vm, char, currentSize);

    if (line == NULL) {
        runtimeError(vm, "Memory error on input()!");
        return EMPTY_VAL;
    }

    int c = EOF;
    uint64_t length = 0;
    while ((c = getchar()) != '\n' && c != EOF) {
        line[length++] = (char) c;

        if (length + 1 == currentSize) {
            int oldSize = currentSize;
            currentSize = GROW_CAPACITY(currentSize);
            line = GROW_ARRAY(vm, line, char, oldSize, currentSize);

            if (line == NULL) {
                printf("Unable to allocate memory\n");
                exit(71);
            }
        }
    }

    // If length has changed, shrink
    if (length != currentSize) {
        line = SHRINK_ARRAY(vm, line, char, currentSize, length + 1);
    }

    line[length] = '\0';

    return OBJ_VAL(takeString(vm, line, length));
}

static Value printNative(DictuVM *vm, int argCount, Value *args) {
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

static Value assertNative(DictuVM *vm, int argCount, Value *args) {
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

static Value isDefinedNative(DictuVM *vm, int argCount, Value *args) {
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

static Value generateSuccessResult(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "Success() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    return newResultSuccess(vm, args[0]);
}

static Value generateErrorResult(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "Error() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "Error() only takes a string as an argument");
        return EMPTY_VAL;
    }

    return OBJ_VAL(newResult(vm, ERR, args[0]));
}

// End of natives

void defineAllNatives(DictuVM *vm) {
    char *nativeNames[] = {
            "input",
            "type",
            "set",
            "print",
            "assert",
            "isDefined",
            "Success",
            "Error"
    };

    NativeFn nativeFunctions[] = {
            inputNative,
            typeNative,
            setNative,
            printNative,
            assertNative,
            isDefinedNative,
            generateSuccessResult,
            generateErrorResult
    };


    for (uint8_t i = 0; i < sizeof(nativeNames) / sizeof(nativeNames[0]); ++i) {
        defineNative(vm, &vm->globals, nativeNames[i], nativeFunctions[i]);
    }
}

    /* 9: vm/memory.c */


#ifdef DEBUG_TRACE_GC
#endif

#define GC_HEAP_GROW_FACTOR 2

void *reallocate(DictuVM *vm, void *previous, size_t oldSize, size_t newSize) {
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

void grayObject(DictuVM *vm, Obj *object) {
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

void grayValue(DictuVM *vm, Value value) {
    if (!IS_OBJ(value)) return;
    grayObject(vm, AS_OBJ(value));
}

static void grayArray(DictuVM *vm, ValueArray *array) {
    for (int i = 0; i < array->count; i++) {
        grayValue(vm, array->values[i]);
    }
}

static void blackenObject(DictuVM *vm, Obj *object) {
#ifdef DEBUG_TRACE_GC
    printf("%p blacken ", (void *)object);
    printValue(OBJ_VAL(object));
    printf("\n");
#endif

    switch (object->type) {
        case OBJ_MODULE: {
            ObjModule *module = (ObjModule *) object;
            grayObject(vm, (Obj *) module->name);
            grayObject(vm, (Obj *) module->path);
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
            grayTable(vm, &klass->abstractMethods);
            grayTable(vm, &klass->properties);
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

        case OBJ_ABSTRACT: {
            ObjAbstract *abstract = (ObjAbstract *) object;
            grayTable(vm, &abstract->values);
            break;
        }

        case OBJ_RESULT: {
            ObjResult *result = (ObjResult *) object;
            grayValue(vm, result->value);
            break;
        }

        case OBJ_NATIVE:
        case OBJ_STRING:
        case OBJ_FILE:
            break;
    }
}

void freeObject(DictuVM *vm, Obj *object) {
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
            freeTable(vm, &klass->abstractMethods);
            freeTable(vm, &klass->properties);
            FREE(vm, ObjClass, object);
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
            if (function->type == TYPE_INITIALIZER) {
                FREE_ARRAY(vm, int, function->propertyNames, function->propertyCount);
                FREE_ARRAY(vm, int, function->propertyIndexes, function->propertyCount);
            }
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

        case OBJ_ABSTRACT: {
            ObjAbstract *abstract = (ObjAbstract*) object;
            abstract->func(vm, abstract);
            freeTable(vm, &abstract->values);
            FREE(vm, ObjAbstract, object);
            break;
        }

        case OBJ_RESULT: {
            FREE(vm, ObjResult, object);
            break;
        }
    }
}

void collectGarbage(DictuVM *vm) {
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
    grayTable(vm, &vm->resultMethods);
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

void freeObjects(DictuVM *vm) {
    Obj *object = vm->objects;
    while (object != NULL) {
        Obj *next = object->next;
        freeObject(vm, object);
        object = next;
    }

    free(vm->grayStack);
}

    /* 10: vm/util.c */


char *readFile(DictuVM *vm, const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char *buffer = ALLOCATE(vm, char, fileSize + 1);
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

ObjString *dirname(DictuVM *vm, char *path, int len) {
    if (!len) {
        return copyString(vm, ".", 1);
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

    if (sep == path && !IS_DIR_SEPARATOR(*sep)) {
        return copyString(vm, ".", 1);
    }

    len = sep - path + 1;

    return copyString(vm, path, len);
}

bool resolvePath(char *directory, char *path, char *ret) {
    char buf[PATH_MAX];
    if (*path == DIR_SEPARATOR)
        snprintf (buf, PATH_MAX, "%s", path);
    else
        snprintf(buf, PATH_MAX, "%s%c%s", directory, DIR_SEPARATOR, path);

#ifdef _WIN32
    _fullpath(ret, buf, PATH_MAX);
#else
    if (realpath(buf, ret) == NULL) {
        return false;
    }
#endif

    return true;
}

ObjString *getDirectory(DictuVM *vm, char *source) {
    // Slight workaround to ensure only .du files are the ones
    // attempted to be found.
    int len = strlen(source);
    if (vm->repl || len < 4 || source[len - 3] != '.') {
        source = "";
    }

    char res[PATH_MAX];
    if (!resolvePath(".", source, res)) {
        runtimeError(vm, "Unable to resolve path '%s'", source);
        exit(1);
    }

    if (vm->repl) {
        return copyString(vm, res, strlen(res));
    }

    return dirname(vm, res, strlen(res));
}

void defineNative(DictuVM *vm, Table *table, const char *name, NativeFn function) {
    ObjNative *native = newNative(vm, function);
    push(vm, OBJ_VAL(native));
    ObjString *methodName = copyString(vm, name, strlen(name));
    push(vm, OBJ_VAL(methodName));
    tableSet(vm, table, methodName, OBJ_VAL(native));
    pop(vm);
    pop(vm);
}

void defineNativeProperty(DictuVM *vm, Table *table, const char *name, Value value) {
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

Value boolNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "bool() takes no arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    return BOOL_VAL(!isFalsey(args[0]));
}
    /* 11: bool.c */

static Value toStringBool(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    int val = AS_BOOL(args[0]);
    return OBJ_VAL(copyString(vm, val ? "true" : "false", val ? 4 : 5));
}

void declareBoolMethods(DictuVM *vm) {
    defineNative(vm, &vm->boolMethods, "toString", toStringBool);
}

    /* 12: class.c */

static Value clas_toString(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "clas_toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = classToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}

void declareClassMethods(DictuVM *vm) {
    defineNative(vm, &vm->classMethods, "toString", clas_toString);
}

    /* 13: copy.c */

ObjList *copyList(DictuVM* vm, ObjList *oldList, bool shallow);

ObjDict *copyDict(DictuVM* vm, ObjDict *oldDict, bool shallow) {
    ObjDict *dict = newDict(vm);
    // Push to stack to avoid GC
    push(vm, OBJ_VAL(dict));

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
        dictSet(vm, dict, oldDict->entries[i].key, val);
        pop(vm);
    }

    pop(vm);
    return dict;
}

ObjList *copyList(DictuVM* vm, ObjList *oldList, bool shallow) {
    ObjList *list = newList(vm);
    // Push to stack to avoid GC
    push(vm, OBJ_VAL(list));

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
        writeValueArray(vm, &list->values, val);
        pop(vm);
    }

    pop(vm);
    return list;
}

ObjInstance *copyInstance(DictuVM* vm, ObjInstance *oldInstance, bool shallow) {
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

static Value toStringDict(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = dictToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}

static Value lenDict(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "len() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjDict *dict = AS_DICT(args[0]);
    return NUMBER_VAL(dict->count);
}

static Value keysDict(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "keys() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjDict *dict = AS_DICT(args[0]);

    ObjList *list = newList(vm);
    push(vm, OBJ_VAL(list));

    for (int i = 0; i < dict->capacityMask + 1; ++i) {
        if (IS_EMPTY(dict->entries[i].key)) {
            continue;
        }

        writeValueArray(vm, &list->values, dict->entries[i].key);
    }

    pop(vm);
    return OBJ_VAL(list);
}

static Value getDictItem(DictuVM *vm, int argCount, Value *args) {
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

static Value removeDictItem(DictuVM *vm, int argCount, Value *args) {
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

static Value dictItemExists(DictuVM *vm, int argCount, Value *args) {
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

static Value copyDictShallow(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "copy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjDict *oldDict = AS_DICT(args[0]);
    ObjDict *newDict = copyDict(vm, oldDict, true);

    return OBJ_VAL(newDict);
}

static Value copyDictDeep(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "deepCopy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjDict *oldDict = AS_DICT(args[0]);
    ObjDict *newDict = copyDict(vm, oldDict, false);

    return OBJ_VAL(newDict);
}

void declareDictMethods(DictuVM *vm) {
    defineNative(vm, &vm->dictMethods, "toString", toStringDict);
    defineNative(vm, &vm->dictMethods, "len", lenDict);
    defineNative(vm, &vm->dictMethods, "keys", keysDict);
    defineNative(vm, &vm->dictMethods, "get", getDictItem);
    defineNative(vm, &vm->dictMethods, "remove", removeDictItem);
    defineNative(vm, &vm->dictMethods, "exists", dictItemExists);
    defineNative(vm, &vm->dictMethods, "copy", copyDictShallow);
    defineNative(vm, &vm->dictMethods, "deepCopy", copyDictDeep);
    defineNative(vm, &vm->dictMethods, "toBool", boolNative); // Defined in util
}

    /* 15: files.c */

static Value writeFile(DictuVM *vm, int argCount, Value *args) {
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

static Value writeLineFile(DictuVM *vm, int argCount, Value *args) {
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

static Value readFullFile(DictuVM *vm, int argCount, Value *args) {
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

    char *buffer = ALLOCATE(vm, char, fileSize + 1);
    if (buffer == NULL) {
        runtimeError(vm, "Not enough memory to read \"%s\".\n", file->path);
        return EMPTY_VAL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file->file);
    if (bytesRead < fileSize && !feof(file->file)) {
        FREE_ARRAY(vm, char, buffer, fileSize + 1);
        runtimeError(vm, "Could not read file \"%s\".\n", file->path);
        return EMPTY_VAL;
    }

    if (bytesRead != fileSize) {
        buffer = SHRINK_ARRAY(vm, buffer, char, fileSize + 1, bytesRead + 1);
    }

    buffer[bytesRead] = '\0';
    return OBJ_VAL(takeString(vm, buffer, bytesRead));
}

static Value readLineFile(DictuVM *vm, int argCount, Value *args) {
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

static Value seekFile(DictuVM *vm, int argCount, Value *args) {
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

    if (offset != 0 && !strstr(file->openType, "b")) {
        runtimeError(vm, "seek() may not have non-zero offset if file is opened in text mode");
        return EMPTY_VAL;
    }

    fseek(file->file, offset, seekType);

    return NIL_VAL;
}

void declareFileMethods(DictuVM *vm) {
    defineNative(vm, &vm->fileMethods, "write", writeFile);
    defineNative(vm, &vm->fileMethods, "writeLine", writeLineFile);
    defineNative(vm, &vm->fileMethods, "read", readFullFile);
    defineNative(vm, &vm->fileMethods, "readLine", readLineFile);
    defineNative(vm, &vm->fileMethods, "seek", seekFile);
}

    /* 16: instance.c */

static Value toString(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = instanceToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}


static Value hasAttribute(DictuVM *vm, int argCount, Value *args) {
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

static Value getAttribute(DictuVM *vm, int argCount, Value *args) {
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

static Value setAttribute(DictuVM *vm, int argCount, Value *args) {
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

static Value isInstance(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "isInstance() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_CLASS(args[1])) {
        runtimeError(vm, "Argument passed to isInstance() must be a class");
        return EMPTY_VAL;
    }

    ObjInstance *object = AS_INSTANCE(args[0]);

    ObjClass *klass = AS_CLASS(args[1]);
    ObjClass *klassToFind = object->klass;

    while (klassToFind != NULL) {
        if (klass == klassToFind) {
            return BOOL_VAL(true);
        }

        klassToFind = klassToFind->superclass;
    }

    return BOOL_VAL(false);
}

static Value copyShallow(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "copy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjInstance *oldInstance = AS_INSTANCE(args[0]);
    ObjInstance *instance = copyInstance(vm, oldInstance, true);

    return OBJ_VAL(instance);
}

static Value copyDeep(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "deepCopy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjInstance *oldInstance = AS_INSTANCE(args[0]);
    ObjInstance *instance = copyInstance(vm, oldInstance, false);

    return OBJ_VAL(instance);
}

void declareInstanceMethods(DictuVM *vm) {
    defineNative(vm, &vm->instanceMethods, "toString", toString);
    defineNative(vm, &vm->instanceMethods, "hasAttribute", hasAttribute);
    defineNative(vm, &vm->instanceMethods, "getAttribute", getAttribute);
    defineNative(vm, &vm->instanceMethods, "setAttribute", setAttribute);
    defineNative(vm, &vm->instanceMethods, "isInstance", isInstance);
    defineNative(vm, &vm->instanceMethods, "copy", copyShallow);
    defineNative(vm, &vm->instanceMethods, "deepCopy", copyDeep);
}

    /* 17: result.c */

static Value unwrap(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "unwrap() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjResult *result = AS_RESULT(args[0]);

    if (result->status == ERR) {
        runtimeError(vm, "Attempted unwrap() on an error Result value '%s'", AS_CSTRING(result->value));
        return EMPTY_VAL;
    }

    return result->value;
}

static Value unwrapError(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "unwrapError() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjResult *result = AS_RESULT(args[0]);

    if (result->status == SUCCESS) {
        runtimeError(vm, "Attempted unwrapError() on a success Result value");
        return EMPTY_VAL;
    }

    return result->value;
}

static Value success(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "success() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjResult *result = AS_RESULT(args[0]);
    return BOOL_VAL(result->status == SUCCESS);
}

void declareResultMethods(DictuVM *vm) {
    defineNative(vm, &vm->resultMethods, "unwrap", unwrap);
    defineNative(vm, &vm->resultMethods, "unwrapError", unwrapError);
    defineNative(vm, &vm->resultMethods, "success", success);
}

    /* 18: lists/lists.c */


/*
 * Note: We should try to implement everything we can in C
 *       rather than in the host language as C will always
 *       be faster than Dictu, and there will be extra work
 *       at startup running the Dictu code, however compromises
 *       must be made due to the fact the VM is not re-enterable
 */


static Value toStringList(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = listToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}

static Value lenList(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "len() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    return NUMBER_VAL(list->values.count);
}

static Value extendList(DictuVM *vm, int argCount, Value *args) {
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

static Value pushListItem(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "push() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    writeValueArray(vm, &list->values, args[1]);

    return NIL_VAL;
}

static Value insertListItem(DictuVM *vm, int argCount, Value *args) {
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

static Value popListItem(DictuVM *vm, int argCount, Value *args) {
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

static Value removeListItem(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "remove() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    Value remove = args[1];
    bool found = false;

    if (list->values.count == 0) {
        runtimeError(vm, "Value passed to remove() does not exist within an empty list");
        return EMPTY_VAL;
    }

    if (list->values.count > 1) {
        for (int i = 0; i < list->values.count - 1; i++) {
            if (!found && valuesEqual(remove, list->values.values[i])) {
                found = true;
            }

            // If we have found the value, shuffle the array
            if (found) {
                list->values.values[i] = list->values.values[i + 1];
            }
        }

        // Check if it's the last element
        if (!found && valuesEqual(remove, list->values.values[list->values.count - 1])) {
            found = true;
        }
    } else {
        if (valuesEqual(remove, list->values.values[0])) {
            found = true;
        }
    }

    if (found) {
        list->values.count--;
        return NIL_VAL;
    }

    runtimeError(vm, "Value passed to remove() does not exist within the list");
    return EMPTY_VAL;
}

static Value containsListItem(DictuVM *vm, int argCount, Value *args) {
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

static Value joinListItem(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0 && argCount != 1) {
        runtimeError(vm, "join() takes 1 optional argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);

    if (list->values.count == 0) {
        return OBJ_VAL(copyString(vm, "", 0));
    }

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
    int length = 0;
    int delimiterLength = strlen(delimiter);

    for (int j = 0; j < list->values.count - 1; ++j) {
        if (IS_STRING(list->values.values[j])) {
            output = AS_CSTRING(list->values.values[j]);
        } else {
            output = valueToString(list->values.values[j]);
        }
        int elementLength = strlen(output);

        fullString = GROW_ARRAY(vm, fullString, char, length, length + elementLength + delimiterLength);

        memcpy(fullString + length, output, elementLength);
        if (!IS_STRING(list->values.values[j])) {
            free(output);
        }
        length += elementLength;
        memcpy(fullString + length, delimiter, delimiterLength);
        length += delimiterLength;
    }

    // Outside the loop as we do not want the append the delimiter on the last element
    if (IS_STRING(list->values.values[list->values.count - 1])) {
        output = AS_CSTRING(list->values.values[list->values.count - 1]);
    } else {
        output = valueToString(list->values.values[list->values.count - 1]);
    }

    int elementLength = strlen(output);
    fullString = GROW_ARRAY(vm, fullString, char, length, length + elementLength + 1);
    memcpy(fullString + length, output, elementLength);
    length += elementLength;

    fullString[length] = '\0';

    if (!IS_STRING(list->values.values[list->values.count - 1])) {
        free(output);
    }

    return OBJ_VAL(takeString(vm, fullString, length));
}

static Value copyListShallow(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "copy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *oldList = AS_LIST(args[0]);
    ObjList *list = copyList(vm, oldList, true);
    return OBJ_VAL(list);
}

static Value copyListDeep(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "deepCopy() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList *oldList = AS_LIST(args[0]);
    ObjList *list = copyList(vm, oldList, false);

    return OBJ_VAL(list);
}


static int partition(ObjList* arr, int start, int end) {
    int pivot_index = (int)floor(start + end) / 2;

    double pivot =  AS_NUMBER(arr->values.values[pivot_index]);

    int i = start - 1;
    int j = end + 1;

    for (;;) {
        do {
            i = i + 1;
        } while(AS_NUMBER(arr->values.values[i]) < pivot);

        do {
            j = j - 1;
        } while(AS_NUMBER(arr->values.values[j]) > pivot);

        if (i >= j) {
            return j;
        }

        // Swap arr[i] with arr[j]
        Value temp = arr->values.values[i];
        
        arr->values.values[i] = arr->values.values[j];
        arr->values.values[j] = temp;
    }
}

// Implementation of Quick Sort using the Hoare
// Partition scheme.
// Best Case O(n log n)
// Worst Case O(n^2) (If the list is already sorted.) 
static void quickSort(ObjList* arr, int start, int end) {
    while (start < end) {
        int part = partition(arr, start, end);

        // Recurse for the smaller halve.
        if (part - start < end - part) {
            quickSort(arr, start, part);
            
            start = start + 1;
        } else {
            quickSort(arr, part + 1, end);

            end = end - 1;
        }
    }
}

static Value sortList(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "sort() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjList* list = AS_LIST(args[0]);

    // Check if all the list elements are indeed numbers.
    for (int i = 0; i < list->values.count; i++) {
        if (!IS_NUMBER(list->values.values[i])) {
            runtimeError(vm, "sort() takes lists with numbers (index %d was not a number)", i);
            return EMPTY_VAL;
        }
    }

    quickSort(list, 0, list->values.count - 1);

    return NIL_VAL;
}

void declareListMethods(DictuVM *vm) {
    defineNative(vm, &vm->listMethods, "toString", toStringList);
    defineNative(vm, &vm->listMethods, "len", lenList);
    defineNative(vm, &vm->listMethods, "extend", extendList);
    defineNative(vm, &vm->listMethods, "push", pushListItem);
    defineNative(vm, &vm->listMethods, "insert", insertListItem);
    defineNative(vm, &vm->listMethods, "pop", popListItem);
    defineNative(vm, &vm->listMethods, "remove", removeListItem);
    defineNative(vm, &vm->listMethods, "contains", containsListItem);
    defineNative(vm, &vm->listMethods, "join", joinListItem);
    defineNative(vm, &vm->listMethods, "copy", copyListShallow);
    defineNative(vm, &vm->listMethods, "deepCopy", copyListDeep);
    defineNative(vm, &vm->listMethods, "toBool", boolNative); // Defined in util
    defineNative(vm, &vm->listMethods, "sort", sortList);

    dictuInterpret(vm, "List", DICTU_LIST_SOURCE);

    Value List;
    tableGet(&vm->modules, copyString(vm, "List", 4), &List);

    ObjModule *ListModule = AS_MODULE(List);
    push(vm, List);
    tableAddAll(vm, &ListModule->values, &vm->listMethods);
    pop(vm);
}

    /* 19: nil.c */

static Value toStringNil(DictuVM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    return OBJ_VAL(copyString(vm, "nil", 3));
}

void declareNilMethods(DictuVM *vm) {
    defineNative(vm, &vm->nilMethods, "toString", toStringNil);
    defineNative(vm, &vm->nilMethods, "toBool", boolNative); // Defined in util
}

    /* 20: number.c */

static Value toStringNumber(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    double number = AS_NUMBER(args[0]);
    int numberStringLength = snprintf(NULL, 0, "%.15g", number) + 1;
    
    char *numberString = ALLOCATE(vm, char, numberStringLength);

    if (numberString == NULL) {
        runtimeError(vm, "Memory error on toString()!");
        return EMPTY_VAL;
    }
    
    snprintf(numberString, numberStringLength, "%.15g", number);
    return OBJ_VAL(takeString(vm, numberString, numberStringLength - 1));
}

void declareNumberMethods(DictuVM *vm) {
    defineNative(vm, &vm->numberMethods, "toString", toStringNumber);
    defineNative(vm, &vm->numberMethods, "toBool", boolNative); // Defined in util
}

    /* 21: sets.c */

static Value toStringSet(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "toString() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *valueString = setToString(args[0]);

    ObjString *string = copyString(vm, valueString, strlen(valueString));
    free(valueString);

    return OBJ_VAL(string);
}

static Value lenSet(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "len() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjSet *set = AS_SET(args[0]);
    return NUMBER_VAL(set->count);
}

static Value addSetItem(DictuVM *vm, int argCount, Value *args) {
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

static Value removeSetItem(DictuVM *vm, int argCount, Value *args) {
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

static Value containsSetItem(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "contains() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjSet *set = AS_SET(args[0]);

    return setGet(set, args[1]) ? TRUE_VAL : FALSE_VAL;
}

void declareSetMethods(DictuVM *vm) {
    defineNative(vm, &vm->setMethods, "toString", toStringSet);
    defineNative(vm, &vm->setMethods, "len", lenSet);
    defineNative(vm, &vm->setMethods, "add", addSetItem);
    defineNative(vm, &vm->setMethods, "remove", removeSetItem);
    defineNative(vm, &vm->setMethods, "contains", containsSetItem);
    defineNative(vm, &vm->setMethods, "toBool", boolNative); // Defined in util
}


    /* 22: strings.c */


static Value lenString(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "len() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);
    return NUMBER_VAL(string->length);
}

static Value toNumberString(DictuVM *vm, int argCount, Value *args) {
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
        return newResultError(vm, "Can not convert to number");
    }

    return newResultSuccess(vm, NUMBER_VAL(number));
}

static Value formatString(DictuVM *vm, int argCount, Value *args) {
    if (argCount == 0) {
        runtimeError(vm, "format() takes at least 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    int length = 0;
    char **replaceStrings = ALLOCATE(vm, char*, argCount);

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
    char *tmp = ALLOCATE(vm, char, stringLen);
    char *tmpFree = tmp;
    memcpy(tmp, string->chars, stringLen);

    int count = 0;
    while ((tmp = strstr(tmp, "{}"))) {
        count++;
        tmp++;
    }

    tmp = tmpFree;

    if (count != argCount) {
        runtimeError(vm, "format() placeholders do not match arguments");

        for (int i = 0; i < argCount; ++i) {
            free(replaceStrings[i]);
        }

        FREE_ARRAY(vm, char, tmp , stringLen);
        FREE_ARRAY(vm, char*, replaceStrings, argCount);
        return EMPTY_VAL;
    }

    int fullLength = string->length - count * 2 + length + 1;
    char *pos;
    char *newStr = ALLOCATE(vm, char, fullLength);
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

    FREE_ARRAY(vm, char*, replaceStrings, argCount);
    memcpy(newStr + stringLength, tmp, strlen(tmp));
    newStr[fullLength - 1] = '\0';
    FREE_ARRAY(vm, char, tmpFree, stringLen);

    return OBJ_VAL(takeString(vm, newStr, fullLength - 1));
}

static Value splitString(DictuVM *vm, int argCount, Value *args) {
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

    char *tmp = ALLOCATE(vm, char, string->length + 1);
    char *tmpFree = tmp;
    memcpy(tmp, string->chars, string->length);
    tmp[string->length] = '\0';
    int delimiterLength = strlen(delimiter);
    char *token;

    ObjList *list = newList(vm);
    push(vm, OBJ_VAL(list));
    if (delimiterLength == 0) {
        for (int tokenCount = 0; tokenCount < string->length; tokenCount++) {
            *(tmp) = string->chars[tokenCount];
            *(tmp + 1) = '\0';
            Value str = OBJ_VAL(copyString(vm, tmp, 1));
            // Push to stack to avoid GC
            push(vm, str);
            writeValueArray(vm, &list->values, str);
            pop(vm);
        }
    } else {
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
    }
    pop(vm);

    FREE_ARRAY(vm, char, tmpFree, string->length + 1);
    return OBJ_VAL(list);
}

static Value containsString(DictuVM *vm, int argCount, Value *args) {
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

static Value findString(DictuVM *vm, int argCount, Value *args) {
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

static Value replaceString(DictuVM *vm, int argCount, Value *args) {
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
    char *tmp = ALLOCATE(vm, char, stringLen + 1);
    char *tmpFree = tmp;
    memcpy(tmp, string, stringLen);
    tmp[stringLen] = '\0';

    // Count the occurrences of the needle so we can determine the size
    // of the string we need to allocate
    while ((tmp = strstr(tmp, to_replace->chars)) != NULL) {
        count++;
        tmp += len;
    }

    // Reset the pointer
    tmp = tmpFree;

    if (count == 0) {
        FREE_ARRAY(vm, char, tmpFree, stringLen + 1);
        return stringValue;
    }

    int length = strlen(tmp) - count * (len - replaceLen) + 1;
    char *pos;
    char *newStr = ALLOCATE(vm, char, length);
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
    FREE_ARRAY(vm, char, tmpFree, stringLen + 1);
    newStr[length - 1] = '\0';

    return OBJ_VAL(takeString(vm, newStr, length - 1));
}

static Value lowerString(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "lower() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);
    char *temp = ALLOCATE(vm, char, string->length + 1);

    for (int i = 0; string->chars[i]; i++) {
        temp[i] = tolower(string->chars[i]);
    }
    temp[string->length] = '\0';

    return OBJ_VAL(takeString(vm, temp, string->length));
}

static Value upperString(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "upper() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);
    char *temp = ALLOCATE(vm, char, string->length + 1);

    for (int i = 0; string->chars[i]; i++) {
        temp[i] = toupper(string->chars[i]);
    }
    temp[string->length] = '\0';

    return OBJ_VAL(takeString(vm, temp, string->length));
}

static Value startsWithString(DictuVM *vm, int argCount, Value *args) {
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

static Value endsWithString(DictuVM *vm, int argCount, Value *args) {
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

static Value leftStripString(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "leftStrip() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);
    int i, count = 0;
    char *temp = ALLOCATE(vm, char, string->length + 1);

    for (i = 0; i < string->length; ++i) {
        if (!isspace(string->chars[i])) {
            break;
        }
        count++;
    }

    if (count != 0) {
        temp = SHRINK_ARRAY(vm, temp, char, string->length + 1, (string->length - count) + 1);
    }

    memcpy(temp, string->chars + count, string->length - count);
    temp[string->length - count] = '\0';
    return OBJ_VAL(takeString(vm, temp, string->length - count));
}

static Value rightStripString(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "rightStrip() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);
    int length;
    char *temp = ALLOCATE(vm, char, string->length + 1);

    for (length = string->length - 1; length > 0; --length) {
        if (!isspace(string->chars[length])) {
            break;
        }
    }

    // If characters were stripped resize the buffer
    if (length + 1 != string->length) {
        temp = SHRINK_ARRAY(vm, temp, char, string->length + 1, length + 2);
    }

    memcpy(temp, string->chars, length + 1);
    temp[length + 1] = '\0';
    return OBJ_VAL(takeString(vm, temp, length + 1));
}

static Value stripString(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "strip() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    Value string = leftStripString(vm, 0, args);
    push(vm, string);
    string = rightStripString(vm, 0, &string);
    pop(vm);
    return string;
}

static Value countString(DictuVM *vm, int argCount, Value *args) {
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
    while ((haystack = strstr(haystack, needle))) {
        count++;
        haystack++;
    }

    return NUMBER_VAL(count);
}

void declareStringMethods(DictuVM *vm) {
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

    /* 23: optionals.c */

BuiltinModules modules[] = {
        {"Math", &createMathsModule},
        {"Env", &createEnvModule},
        {"JSON", &createJSONModule},
        {"Path", &createPathModule},
        {"Datetime", &createDatetimeModule},
        {"Socket", &createSocketModule},
        {"Random", &createRandomModule},
        {"Base64", &createBase64Module},
        {"Hashlib", &createHashlibModule},
#ifndef DISABLE_SQLITE
        {"Sqlite", &createSqliteModule},
#endif /* DISABLE_SQLITE */
        {"Process", &createProcessModule},
#ifndef DISABLE_HTTP
        {"HTTP", &createHTTPModule},
#endif
        {NULL, NULL}
};

ObjModule *importBuiltinModule(DictuVM *vm, int index) {
    return modules[index].module(vm);
}

int findBuiltinModule(char *name, int length) {
    for (int i = 0; modules[i].module != NULL; ++i) {
        if (strncmp(modules[i].name, name, length) == 0) {
            return i;
        }
    }

    return -1;
}

    /* 24: base64/base64Lib.c */
/*
	base64.c - by Joe DF (joedf@ahkscript.org)
	Released under the MIT License

	See "base64.h", for more information.

	Thank you for inspiration:
	http://www.codeproject.com/Tips/813146/Fast-base-functions-for-encode-decode
*/


//Base64 char table - used internally for encoding
unsigned char b64_chr[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

unsigned int b64_int(unsigned int ch) {

    // ASCII to base64_int
    // 65-90  Upper Case  >>  0-25
    // 97-122 Lower Case  >>  26-51
    // 48-57  Numbers     >>  52-61
    // 43     Plus (+)    >>  62
    // 47     Slash (/)   >>  63
    // 61     Equal (=)   >>  64~
    if (ch==43)
        return 62;
    if (ch==47)
        return 63;
    if (ch==61)
        return 64;
    if ((ch>47) && (ch<58))
        return ch + 4;
    if ((ch>64) && (ch<91))
        return ch - 'A';
    if ((ch>96) && (ch<123))
        return (ch - 'a') + 26;
    return 0;
}

unsigned int b64e_size(unsigned int in_size) {

    // size equals 4*floor((1/3)*(in_size+2));
    unsigned int i, j = 0;
    for (i=0;i<in_size;i++) {
        if (i % 3 == 0)
            j += 1;
    }
    return (4*j);
}

unsigned int b64d_size(unsigned int in_size) {

    return ((3*in_size)/4);
}

unsigned int b64_encode(const unsigned char* in, unsigned int in_len, unsigned char* out) {

    unsigned int i=0, j=0, k=0, s[3];

    for (i=0;i<in_len;i++) {
        s[j++]=*(in+i);
        if (j==3) {
            out[k+0] = b64_chr[ (s[0]&255)>>2 ];
            out[k+1] = b64_chr[ ((s[0]&0x03)<<4)+((s[1]&0xF0)>>4) ];
            out[k+2] = b64_chr[ ((s[1]&0x0F)<<2)+((s[2]&0xC0)>>6) ];
            out[k+3] = b64_chr[ s[2]&0x3F ];
            j=0; k+=4;
        }
    }

    if (j) {
        if (j==1)
            s[1] = 0;
        out[k+0] = b64_chr[ (s[0]&255)>>2 ];
        out[k+1] = b64_chr[ ((s[0]&0x03)<<4)+((s[1]&0xF0)>>4) ];
        if (j==2)
            out[k+2] = b64_chr[ ((s[1]&0x0F)<<2) ];
        else
            out[k+2] = '=';
        out[k+3] = '=';
        k+=4;
    }

    out[k] = '\0';

    return k;
}

unsigned int b64_decode(const unsigned char* in, unsigned int in_len, unsigned char* out) {

    unsigned int i=0, j=0, k=0, s[4];

    for (i=0;i<in_len;i++) {
        s[j++]=b64_int(*(in+i));
        if (j==4) {
            out[k+0] = ((s[0]&255)<<2)+((s[1]&0x30)>>4);
            if (s[2]!=64) {
                out[k+1] = ((s[1]&0x0F)<<4)+((s[2]&0x3C)>>2);
                if ((s[3]!=64)) {
                    out[k+2] = ((s[2]&0x03)<<6)+(s[3]); k+=3;
                } else {
                    k+=2;
                }
            } else {
                k+=1;
            }
            j=0;
        }
    }

    return k;
}

unsigned int b64_encodef(char *InFile, char *OutFile) {

    FILE *pInFile = fopen(InFile,"rb");
    FILE *pOutFile = fopen(OutFile,"wb");

    unsigned int i=0;
    unsigned int j=0;
    unsigned int s[4];
    int c=0;

    if ((pInFile==NULL) || (pOutFile==NULL) ) {
        if (pInFile!=NULL){fclose(pInFile);}
        if (pOutFile!=NULL){fclose(pOutFile);}
        return 0;
    }

    while(c!=EOF) {
        c=fgetc(pInFile);
        if (c==EOF)
            break;
        s[j++]=c;
        if (j==3) {
            fputc(b64_chr[ (s[0]&255)>>2 ],pOutFile);
            fputc(b64_chr[ ((s[0]&0x03)<<4)+((s[1]&0xF0)>>4) ],pOutFile);
            fputc(b64_chr[ ((s[1]&0x0F)<<2)+((s[2]&0xC0)>>6) ],pOutFile);
            fputc(b64_chr[ s[2]&0x3F ],pOutFile);
            j=0; i+=4;
        }
    }

    if (j) {
        if (j==1)
            s[1] = 0;
        fputc(b64_chr[ (s[0]&255)>>2 ],pOutFile);
        fputc(b64_chr[ ((s[0]&0x03)<<4)+((s[1]&0xF0)>>4) ],pOutFile);
        if (j==2)
            fputc(b64_chr[ ((s[1]&0x0F)<<2) ],pOutFile);
        else
            fputc('=',pOutFile);
        fputc('=',pOutFile);
        i+=4;
    }

    fclose(pInFile);
    fclose(pOutFile);

    return i;
}

unsigned int b64_decodef(char *InFile, char *OutFile) {

    FILE *pInFile = fopen(InFile,"rb");
    FILE *pOutFile = fopen(OutFile,"wb");

    unsigned int j=0;
    unsigned int k=0;
    unsigned int s[4];
    int c=0;

    if ((pInFile==NULL) || (pOutFile==NULL) ) {
        if (pInFile!=NULL){fclose(pInFile);}
        if (pOutFile!=NULL){fclose(pOutFile);}
        return 0;
    }

    while(c!=EOF) {
        c=fgetc(pInFile);
        if (c==EOF)
            break;
        s[j++]=b64_int(c);
        if (j==4) {
            fputc(((s[0]&255)<<2)+((s[1]&0x30)>>4),pOutFile);
            if (s[2]!=64) {
                fputc(((s[1]&0x0F)<<4)+((s[2]&0x3C)>>2),pOutFile);
                if ((s[3]!=64)) {
                    fputc(((s[2]&0x03)<<6)+(s[3]),pOutFile); k+=3;
                } else {
                    k+=2;
                }
            } else {
                k+=1;
            }
            j=0;
        }
    }

    fclose(pInFile);
    fclose(pOutFile);

    return k;
}
    /* 25: base64.c */

static Value encode(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "encode() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "encode() argument must be a string");
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);

    int size = b64e_size(string->length) + 1;
    char *buffer = ALLOCATE(vm, char, size);

    int actualSize = b64_encode((unsigned char*)string->chars, string->length, (unsigned char*)buffer);

    ObjString *encodedString = copyString(vm, buffer, actualSize);
    FREE_ARRAY(vm, char, buffer, size);

    return OBJ_VAL(encodedString);
}

static Value decode(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "encode() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "decode() argument must be a string");
        return EMPTY_VAL;
    }

    ObjString *encodedString = AS_STRING(args[0]);

    int size = b64d_size(encodedString->length) + 1;
    char *buffer = ALLOCATE(vm, char, size);

    int actualSize = b64_decode((unsigned char*)encodedString->chars, encodedString->length, (unsigned char*)buffer);

    ObjString *string = copyString(vm, buffer, actualSize);
    FREE_ARRAY(vm, char, buffer, size);

    return OBJ_VAL(string);
}

ObjModule *createBase64Module(DictuVM *vm) {
    ObjString *name = copyString(vm, "Base64", 6);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Base64 methods
     */
    defineNative(vm, &module->values, "encode", encode);
    defineNative(vm, &module->values, "decode", decode);

    pop(vm);
    pop(vm);

    return module;
}
    /* 26: c.c */

#ifdef _WIN32
#define strerror_r(ERRNO, BUF, LEN) strerror_s(BUF, LEN, ERRNO)
#endif

void getStrerror(char *buf, int error) {
#ifdef POSIX_STRERROR
    int intval = strerror_r(error, buf, MAX_ERROR_LEN);

    if (intval == 0) {
        return;
    }

    /* it is safe to assume that we do not reach here */
    memcpy(buf, "Buffer is too small", 19);
    buf[19] = '\0';
#else
    strerror_r(error, buf, MAX_ERROR_LEN);
#endif
}

void createCModule(DictuVM *vm) {
    ObjString *name = copyString(vm, "C", 1);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define C properties
     */
    defineNativeProperty(vm, &module->values, "EPERM",  NUMBER_VAL(EPERM));
    defineNativeProperty(vm, &module->values, "ENOENT", NUMBER_VAL(ENOENT));
    defineNativeProperty(vm, &module->values, "ESRCH",  NUMBER_VAL(ESRCH));
    defineNativeProperty(vm, &module->values, "EINTR",  NUMBER_VAL(EINTR));
    defineNativeProperty(vm, &module->values, "EIO",    NUMBER_VAL(EIO));
    defineNativeProperty(vm, &module->values, "ENXIO",  NUMBER_VAL(ENXIO));
    defineNativeProperty(vm, &module->values, "E2BIG",  NUMBER_VAL(E2BIG));
    defineNativeProperty(vm, &module->values, "ENOEXEC",NUMBER_VAL(ENOEXEC));
    defineNativeProperty(vm, &module->values, "EAGAIN", NUMBER_VAL(EAGAIN));
    defineNativeProperty(vm, &module->values, "ENOMEM", NUMBER_VAL(ENOMEM));
    defineNativeProperty(vm, &module->values, "EACCES", NUMBER_VAL(EACCES));
    defineNativeProperty(vm, &module->values, "EFAULT", NUMBER_VAL(EFAULT));
#ifdef ENOTBLK
    defineNativeProperty(vm, &module->values, "ENOTBLK", NUMBER_VAL(ENOTBLK));
#endif
    defineNativeProperty(vm, &module->values, "EBUSY",  NUMBER_VAL(EBUSY));
    defineNativeProperty(vm, &module->values, "EEXIST", NUMBER_VAL(EEXIST));
    defineNativeProperty(vm, &module->values, "EXDEV",  NUMBER_VAL(EXDEV));
    defineNativeProperty(vm, &module->values, "ENODEV", NUMBER_VAL(ENODEV));
    defineNativeProperty(vm, &module->values, "ENOTDIR",NUMBER_VAL(ENOTDIR));
    defineNativeProperty(vm, &module->values, "EISDIR", NUMBER_VAL(EISDIR));
    defineNativeProperty(vm, &module->values, "EINVAL", NUMBER_VAL(EINVAL));
    defineNativeProperty(vm, &module->values, "ENFILE", NUMBER_VAL(ENFILE));
    defineNativeProperty(vm, &module->values, "EMFILE", NUMBER_VAL(EMFILE));
    defineNativeProperty(vm, &module->values, "ENOTTY", NUMBER_VAL(ENOTTY));
    defineNativeProperty(vm, &module->values, "ETXTBSY",NUMBER_VAL(ETXTBSY));
    defineNativeProperty(vm, &module->values, "EFBIG",  NUMBER_VAL(EFBIG));
    defineNativeProperty(vm, &module->values, "ENOSPC", NUMBER_VAL(ENOSPC));
    defineNativeProperty(vm, &module->values, "ESPIPE", NUMBER_VAL(ESPIPE));
    defineNativeProperty(vm, &module->values, "EROFS",  NUMBER_VAL(EROFS));
    defineNativeProperty(vm, &module->values, "EMLINK", NUMBER_VAL(EMLINK));
    defineNativeProperty(vm, &module->values, "EPIPE",  NUMBER_VAL(EPIPE));
    defineNativeProperty(vm, &module->values, "EDOM",   NUMBER_VAL(EDOM));
    defineNativeProperty(vm, &module->values, "ERANGE", NUMBER_VAL(ERANGE));
    defineNativeProperty(vm, &module->values, "EDEADLK",NUMBER_VAL(EDEADLK));
    defineNativeProperty(vm, &module->values, "ENAMETOOLONG", NUMBER_VAL(ENAMETOOLONG));
    defineNativeProperty(vm, &module->values, "ENOLCK", NUMBER_VAL(ENOLCK));
    defineNativeProperty(vm, &module->values, "ENOSYS", NUMBER_VAL(ENOSYS));
    defineNativeProperty(vm, &module->values, "ENOTEMPTY", NUMBER_VAL(ENOTEMPTY));
    defineNativeProperty(vm, &module->values, "ELOOP",  NUMBER_VAL(ELOOP));
    defineNativeProperty(vm, &module->values, "EWOULDBLOCK", NUMBER_VAL(EWOULDBLOCK));
    defineNativeProperty(vm, &module->values, "ENOMSG", NUMBER_VAL(ENOMSG));
    defineNativeProperty(vm, &module->values, "EIDRM", NUMBER_VAL(EIDRM));
#ifdef ECHRNG
    defineNativeProperty(vm, &module->values, "ECHRNG", NUMBER_VAL(ECHRNG));
#endif
#ifdef EL2NSYNC
    defineNativeProperty(vm, &module->values, "EL2NSYNC", NUMBER_VAL(EL2NSYNC));
#endif
#ifdef EL3HLT
    defineNativeProperty(vm, &module->values, "EL3HLT", NUMBER_VAL(EL3HLT));
#endif
#ifdef EL3RST
    defineNativeProperty(vm, &module->values, "EL3RST", NUMBER_VAL(EL3RST));
#endif
#ifdef ELNRNG
    defineNativeProperty(vm, &module->values, "ELNRNG", NUMBER_VAL(ELNRNG));
#endif
#ifdef EUNATCH
    defineNativeProperty(vm, &module->values, "EUNATCH", NUMBER_VAL(EUNATCH));
#endif
#ifdef ENOCSI
    defineNativeProperty(vm, &module->values, "ENOCSI", NUMBER_VAL(ENOCSI));
#endif
#ifdef EL2HLT
    defineNativeProperty(vm, &module->values, "EL2HLT", NUMBER_VAL(EL2HLT));
#endif
#ifdef EBADE
    defineNativeProperty(vm, &module->values, "EBADE", NUMBER_VAL(EBADE));
#endif
#ifdef EBADR
    defineNativeProperty(vm, &module->values, "EBADR", NUMBER_VAL(EBADR));
#endif
#ifdef EXFULL
    defineNativeProperty(vm, &module->values, "EXFULL", NUMBER_VAL(EXFULL));
#endif
#ifdef ENOANO
    defineNativeProperty(vm, &module->values, "ENOANO", NUMBER_VAL(ENOANO));
#endif
#ifdef EBADRQC
    defineNativeProperty(vm, &module->values, "EBADRQC", NUMBER_VAL(EBADRQC));
#endif
#ifdef EBADSLT
    defineNativeProperty(vm, &module->values, "EBADSLT", NUMBER_VAL(EBADSLT));
#endif
#ifdef EDEADLOCK
    defineNativeProperty(vm, &module->values, "EDEADLOCK", NUMBER_VAL(EDEADLOCK));
#endif
#ifdef EBFONT
    defineNativeProperty(vm, &module->values, "EBFONT", NUMBER_VAL(EBFONT));
#endif
    defineNativeProperty(vm, &module->values, "ENOSTR", NUMBER_VAL(ENOSTR));
    defineNativeProperty(vm, &module->values, "ENODATA", NUMBER_VAL(ENODATA));
    defineNativeProperty(vm, &module->values, "ETIME", NUMBER_VAL(ETIME));
    defineNativeProperty(vm, &module->values, "ENOSR", NUMBER_VAL(ENOSR));
#ifdef ENONET
    defineNativeProperty(vm, &module->values, "ENONET", NUMBER_VAL(ENONET));
#endif
#ifdef ENOPKG
    defineNativeProperty(vm, &module->values, "ENOPKG", NUMBER_VAL(ENOPKG));
#endif
#ifdef EREMOTE
    defineNativeProperty(vm, &module->values, "EREMOTE", NUMBER_VAL(EREMOTE));
#endif
    defineNativeProperty(vm, &module->values, "ENOLINK", NUMBER_VAL(ENOLINK));
#ifdef EADV
    defineNativeProperty(vm, &module->values, "EADV", NUMBER_VAL(EADV));
#endif
#ifdef ESRMNT
    defineNativeProperty(vm, &module->values, "ESRMNT", NUMBER_VAL(ESRMNT));
#endif
#ifdef ECOMM
    defineNativeProperty(vm, &module->values, "ECOMM", NUMBER_VAL(ECOMM));
#endif
    defineNativeProperty(vm, &module->values, "EPROTO", NUMBER_VAL(EPROTO));
#ifdef EMULTIHOP
    defineNativeProperty(vm, &module->values, "EMULTIHOP", NUMBER_VAL(EMULTIHOP));
#endif
#ifdef EDOTDOT
    defineNativeProperty(vm, &module->values, "EDOTDOT", NUMBER_VAL(EDOTDOT));
#endif
    defineNativeProperty(vm, &module->values, "EBADMSG", NUMBER_VAL(EBADMSG));
    defineNativeProperty(vm, &module->values, "EOVERFLOW", NUMBER_VAL(EOVERFLOW));
#ifdef ENOTUNIQ
    defineNativeProperty(vm, &module->values, "ENOTUNIQ", NUMBER_VAL(ENOTUNIQ));
#endif
#ifdef EBADFD
    defineNativeProperty(vm, &module->values, "EBADFD", NUMBER_VAL(EBADFD));
#endif
#ifdef EREMCHG
    defineNativeProperty(vm, &module->values, "EREMCHG", NUMBER_VAL(EREMCHG));
#endif
#ifdef ELIBACC
    defineNativeProperty(vm, &module->values, "ELIBACC", NUMBER_VAL(ELIBACC));
#endif
#ifdef ELIBBAD
    defineNativeProperty(vm, &module->values, "ELIBBAD", NUMBER_VAL(ELIBBAD));
#endif
#ifdef ELIBSCN
    defineNativeProperty(vm, &module->values, "ELIBSCN", NUMBER_VAL(ELIBSCN));
#endif
#ifdef ELIBMAX
    defineNativeProperty(vm, &module->values, "ELIBMAX", NUMBER_VAL(ELIBMAX));
#endif
#ifdef ELIBEXEC
    defineNativeProperty(vm, &module->values, "ELIBEXEC", NUMBER_VAL(ELIBEXEC));
#endif
    defineNativeProperty(vm, &module->values, "EILSEQ", NUMBER_VAL(EILSEQ));
#ifdef ERESTART
    defineNativeProperty(vm, &module->values, "ERESTART", NUMBER_VAL(ERESTART));
#endif
#ifdef ESTRPIPE
    defineNativeProperty(vm, &module->values, "ESTRPIPE", NUMBER_VAL(ESTRPIPE));
#endif
#ifdef EUSERS
    defineNativeProperty(vm, &module->values, "EUSERS", NUMBER_VAL(EUSERS));
#endif
    defineNativeProperty(vm, &module->values, "ENOTSOCK", NUMBER_VAL(ENOTSOCK));
    defineNativeProperty(vm, &module->values, "EDESTADDRREQ", NUMBER_VAL(EDESTADDRREQ));
    defineNativeProperty(vm, &module->values, "EMSGSIZE", NUMBER_VAL(EMSGSIZE));
    defineNativeProperty(vm, &module->values, "EPROTOTYPE", NUMBER_VAL(EPROTOTYPE));
    defineNativeProperty(vm, &module->values, "ENOPROTOOPT", NUMBER_VAL(ENOPROTOOPT));
    defineNativeProperty(vm, &module->values, "EPROTONOSUPPORT", NUMBER_VAL(EPROTONOSUPPORT));
#ifdef ESOCKTNOSUPPORT
    defineNativeProperty(vm, &module->values, "ESOCKTNOSUPPORT", NUMBER_VAL(ESOCKTNOSUPPORT));
#endif
    defineNativeProperty(vm, &module->values, "EOPNOTSUPP", NUMBER_VAL(EOPNOTSUPP));
#ifdef EPFNOSUPPORT
    defineNativeProperty(vm, &module->values, "EPFNOSUPPORT", NUMBER_VAL(EPFNOSUPPORT));
#endif
    defineNativeProperty(vm, &module->values, "EAFNOSUPPORT", NUMBER_VAL(EAFNOSUPPORT));
    defineNativeProperty(vm, &module->values, "EADDRINUSE", NUMBER_VAL(EADDRINUSE));
    defineNativeProperty(vm, &module->values, "EADDRNOTAVAIL", NUMBER_VAL(EADDRNOTAVAIL));
    defineNativeProperty(vm, &module->values, "ENETDOWN", NUMBER_VAL(ENETDOWN));
    defineNativeProperty(vm, &module->values, "ENETUNREACH", NUMBER_VAL(ENETUNREACH));
    defineNativeProperty(vm, &module->values, "ENETRESET", NUMBER_VAL(ENETRESET));
    defineNativeProperty(vm, &module->values, "ECONNABORTED", NUMBER_VAL(ECONNABORTED));
    defineNativeProperty(vm, &module->values, "ECONNRESET", NUMBER_VAL(ECONNRESET));
    defineNativeProperty(vm, &module->values, "ENOBUFS", NUMBER_VAL(ENOBUFS));
    defineNativeProperty(vm, &module->values, "EISCONN", NUMBER_VAL(EISCONN));
    defineNativeProperty(vm, &module->values, "ENOTCONN", NUMBER_VAL(ENOTCONN));
#ifdef ESHUTDOWN
    defineNativeProperty(vm, &module->values, "ESHUTDOWN", NUMBER_VAL(ESHUTDOWN));
#endif
#ifdef ETOOMANYREFS
    defineNativeProperty(vm, &module->values, "ETOOMANYREFS", NUMBER_VAL(ETOOMANYREFS));
#endif
    defineNativeProperty(vm, &module->values, "ETIMEDOUT", NUMBER_VAL(ETIMEDOUT));
    defineNativeProperty(vm, &module->values, "ECONNREFUSED", NUMBER_VAL(ECONNREFUSED));
#ifdef EHOSTDOWN
    defineNativeProperty(vm, &module->values, "EHOSTDOWN", NUMBER_VAL(EHOSTDOWN));
#endif
    defineNativeProperty(vm, &module->values, "EHOSTUNREACH", NUMBER_VAL(EHOSTUNREACH));
    defineNativeProperty(vm, &module->values, "EALREADY", NUMBER_VAL(EALREADY));
    defineNativeProperty(vm, &module->values, "EINPROGRESS", NUMBER_VAL(EINPROGRESS));
#ifdef ESTALE
    defineNativeProperty(vm, &module->values, "ESTALE", NUMBER_VAL(ESTALE));
#endif
#ifdef EUCLEAN
    defineNativeProperty(vm, &module->values, "EUCLEAN", NUMBER_VAL(EUCLEAN));
#endif
#ifdef ENOTNAM
    defineNativeProperty(vm, &module->values, "ENOTNAM", NUMBER_VAL(ENOTNAM));
#endif
#ifdef ENAVAIL
    defineNativeProperty(vm, &module->values, "ENAVAIL", NUMBER_VAL(ENAVAIL));
#endif
#ifdef EISNAM
    defineNativeProperty(vm, &module->values, "EISNAM", NUMBER_VAL(EISNAM));
#endif
#ifdef EREMOTEIO
    defineNativeProperty(vm, &module->values, "EREMOTEIO", NUMBER_VAL(EREMOTEIO));
#endif
#ifdef EDQUOT
    defineNativeProperty(vm, &module->values, "EDQUOT", NUMBER_VAL(EDQUOT));
#endif
#ifdef ENOMEDIUM
    defineNativeProperty(vm, &module->values, "ENOMEDIUM", NUMBER_VAL(ENOMEDIUM));
#endif
#ifdef EMEDIUMTYPE
    defineNativeProperty(vm, &module->values, "EMEDIUMTYPE", NUMBER_VAL(EMEDIUMTYPE));
#endif
    defineNativeProperty(vm, &module->values, "ECANCELED", NUMBER_VAL(ECANCELED));
#ifdef ENOKEY
    defineNativeProperty(vm, &module->values, "ENOKEY", NUMBER_VAL(ENOKEY));
#endif
#ifdef EKEYEXPIRED
    defineNativeProperty(vm, &module->values, "EKEYEXPIRED", NUMBER_VAL(EKEYEXPIRED));
#endif
#ifdef EKEYREVOKED
    defineNativeProperty(vm, &module->values, "EKEYREVOKED", NUMBER_VAL(EKEYREVOKED));
#endif
#ifdef EKEYREJECTED
    defineNativeProperty(vm, &module->values, "EKEYREJECTED", NUMBER_VAL(EKEYREJECTED));
#endif
    defineNativeProperty(vm, &module->values, "EOWNERDEAD", NUMBER_VAL(EOWNERDEAD));
    defineNativeProperty(vm, &module->values, "ENOTRECOVERABLE", NUMBER_VAL(ENOTRECOVERABLE));
#ifdef ERFKILL
    defineNativeProperty(vm, &module->values, "ERFKILL", NUMBER_VAL(ERFKILL));
#endif
#ifdef EHWPOISON
    defineNativeProperty(vm, &module->values, "EHWPOISON", NUMBER_VAL(EHWPOISON));
#endif

    tableSet(vm, &vm->globals, name, OBJ_VAL(module));
    pop(vm);
    pop(vm);
}


    /* 27: datetime.c */


#ifdef _WIN32
#define localtime_r(TIMER, BUF) localtime_s(BUF, TIMER)
// Assumes length of BUF is 26
#define asctime_r(TIME_PTR, BUF) (asctime_s(BUF, 26, TIME_PTR), BUF)
#define gmtime_r(TIMER, BUF) gmtime_s(BUF, TIMER)
#else
#define HAS_STRPTIME
#endif

static Value nowNative(DictuVM *vm, int argCount, Value *args) {
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

static Value nowUTCNative(DictuVM *vm, int argCount, Value *args) {
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

static Value strftimeNative(DictuVM *vm, int argCount, Value *args) {
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

    char *point = ALLOCATE(vm, char, len);
    if (point == NULL) {
        runtimeError(vm, "Memory error on strftime()!");
        return EMPTY_VAL;
    }

    gmtime_r(&t, &tictoc);

    /**
     * strtime returns 0 when it fails to write - this would be due to the buffer
     * not being large enough. In that instance we double the buffer length until
     * there is a big enough buffer.
     */

     /** however is not guaranteed that 0 indicates a failure (`man strftime' says so).
     * So we might want to catch up the eternal loop, by using a maximum iterator.
     */
    int max_iterations = 8;  // maximum 65536 bytes with the default 128 len,
                             // more if the given string is > 128
    int iterator = 0;
    while (strftime(point, sizeof(char) * len, fmt, &tictoc) == 0) {
        if (++iterator > max_iterations) {
            FREE_ARRAY(vm, char, point, len);
            return OBJ_VAL(copyString(vm, "", 0));
        }

        len *= 2;

        point = GROW_ARRAY(vm, point, char, len / 2, len);
        if (point == NULL) {
            runtimeError(vm, "Memory error on strftime()!");
            return EMPTY_VAL;
        }
    }

    int length = strlen(point);

    if (length != len) {
        point = SHRINK_ARRAY(vm, point, char, len, length + 1);
    }

    return OBJ_VAL(takeString(vm, point, length));
}

#ifdef HAS_STRPTIME
static Value strptimeNative(DictuVM *vm, int argCount, Value *args) {
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
#endif

ObjModule *createDatetimeModule(DictuVM *vm) {
    ObjString *name = copyString(vm, "Datetime", 8);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Datetime methods
     */
    defineNative(vm, &module->values, "now", nowNative);
    defineNative(vm, &module->values, "nowUTC", nowUTCNative);
    defineNative(vm, &module->values, "strftime", strftimeNative);
    #ifdef HAS_STRPTIME
    defineNative(vm, &module->values, "strptime", strptimeNative);
    #endif

    pop(vm);
    pop(vm);

    return module;
}

    /* 28: env.c */

#ifdef _WIN32
#define unsetenv(NAME) _putenv_s(NAME, "")
int setenv(const char *name, const char *value, int overwrite) {
    if (!overwrite && getenv(name) == NULL) {
        return 0;
    }

    if (_putenv_s(name, value)) {
        return -1;
    } else {
        return 0;
    }
}
#endif

static Value env_get(DictuVM *vm, int argCount, Value *args) {
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
        return newResultSuccess(vm, OBJ_VAL(copyString(vm, value, strlen(value))));
    }

    return newResultError(vm, "No environment variable set");
}

static Value set(DictuVM *vm, int argCount, Value *args) {
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
        return newResultError(vm, "Failed to set environment variable");
    }

    return newResultSuccess(vm, NIL_VAL);
}

ObjModule *createEnvModule(DictuVM *vm) {
    ObjString *name = copyString(vm, "Env", 3);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Env methods
     */
    defineNative(vm, &module->values, "get", env_get);
    defineNative(vm, &module->values, "set", set);

    pop(vm);
    pop(vm);

    return module;
}

    /* 29: hashlib/utils.c */
/* utils.c - TinyCrypt platform-dependent run-time operations */

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



#define MASK_TWENTY_SEVEN 0x1b

unsigned int _copy(uint8_t *to, unsigned int to_len,
                   const uint8_t *from, unsigned int from_len)
{
    if (from_len <= to_len) {
        (void)memcpy(to, from, from_len);
        return from_len;
    } else {
        return TC_CRYPTO_FAIL;
    }
}

void _set(void *to, uint8_t val, unsigned int len)
{
    (void)memset(to, val, len);
}

/*
 * Doubles the value of a byte for values up to 127.
 */
uint8_t _double_byte(uint8_t a)
{
    return ((a<<1) ^ ((a>>7) * MASK_TWENTY_SEVEN));
}

int _compare(const uint8_t *a, const uint8_t *b, size_t size)
{
    const uint8_t *tempa = a;
    const uint8_t *tempb = b;
    uint8_t result = 0;

    for (unsigned int i = 0; i < size; i++) {
        result |= tempa[i] ^ tempb[i];
    }
    return result;
}
    /* 30: hashlib/sha256.c */
/* sha256.c - TinyCrypt SHA-256 crypto hash algorithm implementation */

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


static void compress(unsigned int *iv, const uint8_t *data);

int tc_sha256_init(TCSha256State_t s)
{
    /* input sanity check: */
    if (s == (TCSha256State_t) 0) {
        return TC_CRYPTO_FAIL;
    }

    /*
     * Setting the initial state values.
     * These values correspond to the first 32 bits of the fractional parts
     * of the square roots of the first 8 primes: 2, 3, 5, 7, 11, 13, 17
     * and 19.
     */
    _set((uint8_t *) s, 0x00, sizeof(*s));
    s->iv[0] = 0x6a09e667;
    s->iv[1] = 0xbb67ae85;
    s->iv[2] = 0x3c6ef372;
    s->iv[3] = 0xa54ff53a;
    s->iv[4] = 0x510e527f;
    s->iv[5] = 0x9b05688c;
    s->iv[6] = 0x1f83d9ab;
    s->iv[7] = 0x5be0cd19;

    return TC_CRYPTO_SUCCESS;
}

int tc_sha256_update(TCSha256State_t s, const uint8_t *data, size_t datalen)
{
    /* input sanity check: */
    if (s == (TCSha256State_t) 0 ||
        data == (void *) 0) {
        return TC_CRYPTO_FAIL;
    } else if (datalen == 0) {
        return TC_CRYPTO_SUCCESS;
    }

    while (datalen-- > 0) {
        s->leftover[s->leftover_offset++] = *(data++);
        if (s->leftover_offset >= TC_SHA256_BLOCK_SIZE) {
            compress(s->iv, s->leftover);
            s->leftover_offset = 0;
            s->bits_hashed += (TC_SHA256_BLOCK_SIZE << 3);
        }
    }

    return TC_CRYPTO_SUCCESS;
}

int tc_sha256_final(uint8_t *digest, TCSha256State_t s)
{
    unsigned int i;

    /* input sanity check: */
    if (digest == (uint8_t *) 0 ||
        s == (TCSha256State_t) 0) {
        return TC_CRYPTO_FAIL;
    }

    s->bits_hashed += (s->leftover_offset << 3);

    s->leftover[s->leftover_offset++] = 0x80; /* always room for one byte */
    if (s->leftover_offset > (sizeof(s->leftover) - 8)) {
        /* there is not room for all the padding in this block */
        _set(s->leftover + s->leftover_offset, 0x00,
             sizeof(s->leftover) - s->leftover_offset);
        compress(s->iv, s->leftover);
        s->leftover_offset = 0;
    }

    /* add the padding and the length in big-Endian format */
    _set(s->leftover + s->leftover_offset, 0x00,
         sizeof(s->leftover) - 8 - s->leftover_offset);
    s->leftover[sizeof(s->leftover) - 1] = (uint8_t)(s->bits_hashed);
    s->leftover[sizeof(s->leftover) - 2] = (uint8_t)(s->bits_hashed >> 8);
    s->leftover[sizeof(s->leftover) - 3] = (uint8_t)(s->bits_hashed >> 16);
    s->leftover[sizeof(s->leftover) - 4] = (uint8_t)(s->bits_hashed >> 24);
    s->leftover[sizeof(s->leftover) - 5] = (uint8_t)(s->bits_hashed >> 32);
    s->leftover[sizeof(s->leftover) - 6] = (uint8_t)(s->bits_hashed >> 40);
    s->leftover[sizeof(s->leftover) - 7] = (uint8_t)(s->bits_hashed >> 48);
    s->leftover[sizeof(s->leftover) - 8] = (uint8_t)(s->bits_hashed >> 56);

    /* hash the padding and length */
    compress(s->iv, s->leftover);

    /* copy the iv out to digest */
    for (i = 0; i < TC_SHA256_STATE_BLOCKS; ++i) {
        unsigned int t = *((unsigned int *) &s->iv[i]);
        *digest++ = (uint8_t)(t >> 24);
        *digest++ = (uint8_t)(t >> 16);
        *digest++ = (uint8_t)(t >> 8);
        *digest++ = (uint8_t)(t);
    }

    /* destroy the current state */
    _set(s, 0, sizeof(*s));

    return TC_CRYPTO_SUCCESS;
}

/*
 * Initializing SHA-256 Hash constant words K.
 * These values correspond to the first 32 bits of the fractional parts of the
 * cube roots of the first 64 primes between 2 and 311.
 */
static const unsigned int k256[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static inline unsigned int ROTR(unsigned int a, unsigned int n)
{
    return (((a) >> n) | ((a) << (32 - n)));
}

#define Sigma0(a)(ROTR((a), 2) ^ ROTR((a), 13) ^ ROTR((a), 22))
#define Sigma1(a)(ROTR((a), 6) ^ ROTR((a), 11) ^ ROTR((a), 25))
#define sigma0(a)(ROTR((a), 7) ^ ROTR((a), 18) ^ ((a) >> 3))
#define sigma1(a)(ROTR((a), 17) ^ ROTR((a), 19) ^ ((a) >> 10))

#define Ch(a, b, c)(((a) & (b)) ^ ((~(a)) & (c)))
#define Maj(a, b, c)(((a) & (b)) ^ ((a) & (c)) ^ ((b) & (c)))

static inline unsigned int BigEndian(const uint8_t **c)
{
    unsigned int n = 0;

    n = (((unsigned int)(*((*c)++))) << 24);
    n |= ((unsigned int)(*((*c)++)) << 16);
    n |= ((unsigned int)(*((*c)++)) << 8);
    n |= ((unsigned int)(*((*c)++)));
    return n;
}

static void compress(unsigned int *iv, const uint8_t *data)
{
    unsigned int a, b, c, d, e, f, g, h;
    unsigned int s0, s1;
    unsigned int t1, t2;
    unsigned int work_space[16];
    unsigned int n;
    unsigned int i;

    a = iv[0]; b = iv[1]; c = iv[2]; d = iv[3];
    e = iv[4]; f = iv[5]; g = iv[6]; h = iv[7];

    for (i = 0; i < 16; ++i) {
        n = BigEndian(&data);
        t1 = work_space[i] = n;
        t1 += h + Sigma1(e) + Ch(e, f, g) + k256[i];
        t2 = Sigma0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    for ( ; i < 64; ++i) {
        s0 = work_space[(i+1)&0x0f];
        s0 = sigma0(s0);
        s1 = work_space[(i+14)&0x0f];
        s1 = sigma1(s1);

        t1 = work_space[i&0xf] += s0 + s1 + work_space[(i+9)&0xf];
        t1 += h + Sigma1(e) + Ch(e, f, g) + k256[i];
        t2 = Sigma0(a) + Maj(a, b, c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    iv[0] += a; iv[1] += b; iv[2] += c; iv[3] += d;
    iv[4] += e; iv[5] += f; iv[6] += g; iv[7] += h;
}
    /* 31: hashlib/hmac.c */
/* hmac.c - TinyCrypt implementation of the HMAC algorithm */

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


static void rekey(uint8_t *key, const uint8_t *new_key, unsigned int key_size)
{
    const uint8_t inner_pad = (uint8_t) 0x36;
    const uint8_t outer_pad = (uint8_t) 0x5c;
    unsigned int i;

    for (i = 0; i < key_size; ++i) {
        key[i] = inner_pad ^ new_key[i];
        key[i + TC_SHA256_BLOCK_SIZE] = outer_pad ^ new_key[i];
    }
    for (; i < TC_SHA256_BLOCK_SIZE; ++i) {
        key[i] = inner_pad; key[i + TC_SHA256_BLOCK_SIZE] = outer_pad;
    }
}

int tc_hmac_set_key(TCHmacState_t ctx, const uint8_t *key,
                    unsigned int key_size)
{
    /* Input sanity check */
    if (ctx == (TCHmacState_t) 0 ||
        key == (const uint8_t *) 0 ||
        key_size == 0) {
        return TC_CRYPTO_FAIL;
    }

    const uint8_t dummy_key[TC_SHA256_BLOCK_SIZE];
    struct tc_hmac_state_struct dummy_state;

    if (key_size <= TC_SHA256_BLOCK_SIZE) {
        /*
         * The next three calls are dummy calls just to avoid
         * certain timing attacks. Without these dummy calls,
         * adversaries would be able to learn whether the key_size is
         * greater than TC_SHA256_BLOCK_SIZE by measuring the time
         * consumed in this process.
         */
        (void)tc_sha256_init(&dummy_state.hash_state);
        (void)tc_sha256_update(&dummy_state.hash_state,
                               dummy_key,
                               key_size);
        (void)tc_sha256_final(&dummy_state.key[TC_SHA256_DIGEST_SIZE],
                              &dummy_state.hash_state);

        /* Actual code for when key_size <= TC_SHA256_BLOCK_SIZE: */
        rekey(ctx->key, key, key_size);
    } else {
        (void)tc_sha256_init(&ctx->hash_state);
        (void)tc_sha256_update(&ctx->hash_state, key, key_size);
        (void)tc_sha256_final(&ctx->key[TC_SHA256_DIGEST_SIZE],
                              &ctx->hash_state);
        rekey(ctx->key,
              &ctx->key[TC_SHA256_DIGEST_SIZE],
              TC_SHA256_DIGEST_SIZE);
    }

    return TC_CRYPTO_SUCCESS;
}

int tc_hmac_init(TCHmacState_t ctx)
{

    /* input sanity check: */
    if (ctx == (TCHmacState_t) 0) {
        return TC_CRYPTO_FAIL;
    }

    (void) tc_sha256_init(&ctx->hash_state);
    (void) tc_sha256_update(&ctx->hash_state, ctx->key, TC_SHA256_BLOCK_SIZE);

    return TC_CRYPTO_SUCCESS;
}

int tc_hmac_update(TCHmacState_t ctx,
                   const void *data,
                   unsigned int data_length)
{

    /* input sanity check: */
    if (ctx == (TCHmacState_t) 0) {
        return TC_CRYPTO_FAIL;
    }

    (void)tc_sha256_update(&ctx->hash_state, data, data_length);

    return TC_CRYPTO_SUCCESS;
}

int tc_hmac_final(uint8_t *tag, unsigned int taglen, TCHmacState_t ctx)
{

    /* input sanity check: */
    if (tag == (uint8_t *) 0 ||
        taglen != TC_SHA256_DIGEST_SIZE ||
        ctx == (TCHmacState_t) 0) {
        return TC_CRYPTO_FAIL;
    }

    (void) tc_sha256_final(tag, &ctx->hash_state);

    (void)tc_sha256_init(&ctx->hash_state);
    (void)tc_sha256_update(&ctx->hash_state,
                           &ctx->key[TC_SHA256_BLOCK_SIZE],
                           TC_SHA256_BLOCK_SIZE);
    (void)tc_sha256_update(&ctx->hash_state, tag, TC_SHA256_DIGEST_SIZE);
    (void)tc_sha256_final(tag, &ctx->hash_state);

    /* destroy the current state */
    _set(ctx, 0, sizeof(*ctx));

    return TC_CRYPTO_SUCCESS;
}
    /* 32: hashlib/bcrypt/bcrypt.c */
/*	$OpenBSD: bcrypt.c,v 1.58 2020/07/06 13:33:05 pirofti Exp $	*/

/*
 * Copyright (c) 2014 Ted Unangst <tedu@openbsd.org>
 * Copyright (c) 1997 Niels Provos <provos@umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/* This password hashing algorithm was designed by David Mazieres
 * <dm@lcs.mit.edu> and works as follows:
 *
 * 1. state := InitState ()
 * 2. state := ExpandKey (state, salt, password)
 * 3. REPEAT rounds:
 *      state := ExpandKey (state, 0, password)
 *	state := ExpandKey (state, 0, salt)
 * 4. ctext := "OrpheanBeholderScryDoubt"
 * 5. REPEAT 64:
 * 	ctext := Encrypt_ECB (state, ctext);
 * 6. RETURN Concatenate (salt, ctext);
 *
 */


#ifdef _WIN32
#endif

#ifdef __linux__
    #if __GLIBC__ > 2 || __GLIBC_MINOR__ > 24
        #include <sys/random.h>
    #elif defined(__GLIBC__)
        #include <unistd.h>
        #include <sys/syscall.h>
        #include <errno.h>
    #else
        #include <unistd.h>
        #include <sys/types.h>
        #include <sys/stat.h>
        #include <fcntl.h>
    #endif
#endif


/* This implementation is adaptable to current computing power.
 * You can have up to 2^31 rounds which should be enough for some
 * time to come.
 */

#define BCRYPT_VERSION '2'
#define BCRYPT_MAXSALT 16	/* Precomputation is just so nice */
#define BCRYPT_WORDS 6		/* Ciphertext words */
#define BCRYPT_MINLOGROUNDS 4	/* we have log2(rounds) in salt */

#define	BCRYPT_SALTSPACE	(7 + (BCRYPT_MAXSALT * 4 + 2) / 3 + 1)
#define	BCRYPT_HASHSPACE	61

char   *bcrypt_gensalt(u_int8_t);

static int encode_base64(char *, const u_int8_t *, size_t);
static int decode_base64(u_int8_t *, size_t, const char *);
/*
 * Generates a salt for this version of crypt.
 */
static int
bcrypt_initsalt(int log_rounds, uint8_t *salt, size_t saltbuflen)
{
    uint8_t csalt[BCRYPT_MAXSALT];

    if (saltbuflen < BCRYPT_SALTSPACE) {
        errno = EINVAL;
        return -1;
    }

#ifdef _WIN32
    NTSTATUS Status = BCryptGenRandom(NULL, csalt, sizeof(csalt), BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (Status < 0) {
        return -1;
    }
#elif defined __linux__
    #if __GLIBC__ > 2 || __GLIBC_MINOR__ > 24
        if (getentropy(csalt, sizeof(csalt)) != 0) {
            return -1;
        }
    #elif defined(__GLIBC__)
        if (syscall(SYS_getrandom, csalt, sizeof(csalt), 0) == -1) {
            return -1;
        }
    #else
        int randomData = open("/dev/urandom", O_RDONLY);
        if (randomData > 0) {
            ssize_t result = read(randomData, csalt, sizeof(csalt));
            if (result < 0) return -1;
        } else {
            return -1;
        }
    #endif
#else
    arc4random_buf(csalt, sizeof(csalt));
#endif // _WIN32

    if (log_rounds < 4)
        log_rounds = 4;
    else if (log_rounds > 31)
        log_rounds = 31;

    snprintf((char*)salt, saltbuflen, "$2b$%2.2u$", log_rounds);
    encode_base64((char*)salt + 7, csalt, sizeof(csalt));

    return 0;
}

/*
 * the core bcrypt function
 */
static int
bcrypt_hashpass(const char *key, const char *salt, char *encrypted,
                size_t encryptedlen)
{
    blf_ctx state;
    u_int32_t rounds, i, k;
    u_int16_t j;
    size_t key_len;
    u_int8_t salt_len, logr, minor;
    u_int8_t ciphertext[4 * BCRYPT_WORDS] = "OrpheanBeholderScryDoubt";
    u_int8_t csalt[BCRYPT_MAXSALT];
    u_int32_t cdata[BCRYPT_WORDS];

    if (encryptedlen < BCRYPT_HASHSPACE)
        goto inval;

    /* Check and discard "$" identifier */
    if (salt[0] != '$')
        goto inval;
    salt += 1;

    if (salt[0] != BCRYPT_VERSION)
        goto inval;

    /* Check for minor versions */
    switch ((minor = salt[1])) {
        case 'a':
            key_len = (u_int8_t)(strlen(key) + 1);
            break;
        case 'b':
            /* strlen() returns a size_t, but the function calls
             * below result in implicit casts to a narrower integer
             * type, so cap key_len at the actual maximum supported
             * length here to avoid integer wraparound */
            key_len = strlen(key);
            if (key_len > 72)
                key_len = 72;
            key_len++; /* include the NUL */
            break;
        default:
            goto inval;
    }
    if (salt[2] != '$')
        goto inval;
    /* Discard version + "$" identifier */
    salt += 3;

    /* Check and parse num rounds */
    if (!isdigit((unsigned char)salt[0]) ||
        !isdigit((unsigned char)salt[1]) || salt[2] != '$')
        goto inval;
    logr = (salt[1] - '0') + ((salt[0] - '0') * 10);
    if (logr < BCRYPT_MINLOGROUNDS || logr > 31)
        goto inval;
    /* Computer power doesn't increase linearly, 2^x should be fine */
    rounds = 1U << logr;

    /* Discard num rounds + "$" identifier */
    salt += 3;

    if (strlen(salt) * 3 / 4 < BCRYPT_MAXSALT)
        goto inval;

    /* We dont want the base64 salt but the raw data */
    if (decode_base64(csalt, BCRYPT_MAXSALT, salt))
        goto inval;
    salt_len = BCRYPT_MAXSALT;

    /* Setting up S-Boxes and Subkeys */
    Blowfish_initstate(&state);
    Blowfish_expandstate(&state, csalt, salt_len,
                         (u_int8_t *) key, key_len);
    for (k = 0; k < rounds; k++) {
        Blowfish_expand0state(&state, (u_int8_t *) key, key_len);
        Blowfish_expand0state(&state, csalt, salt_len);
    }

    /* This can be precomputed later */
    j = 0;
    for (i = 0; i < BCRYPT_WORDS; i++)
        cdata[i] = Blowfish_stream2word(ciphertext, 4 * BCRYPT_WORDS, &j);

    /* Now do the encryption */
    for (k = 0; k < 64; k++)
        blf_enc(&state, cdata, BCRYPT_WORDS / 2);

    for (i = 0; i < BCRYPT_WORDS; i++) {
        ciphertext[4 * i + 3] = cdata[i] & 0xff;
        cdata[i] = cdata[i] >> 8;
        ciphertext[4 * i + 2] = cdata[i] & 0xff;
        cdata[i] = cdata[i] >> 8;
        ciphertext[4 * i + 1] = cdata[i] & 0xff;
        cdata[i] = cdata[i] >> 8;
        ciphertext[4 * i + 0] = cdata[i] & 0xff;
    }


    snprintf(encrypted, 8, "$2%c$%2.2u$", minor, logr);
    encode_base64(encrypted + 7, csalt, BCRYPT_MAXSALT);
    encode_base64(encrypted + 7 + 22, ciphertext, 4 * BCRYPT_WORDS - 1);
    explicit_bzero(&state, sizeof(state));
    explicit_bzero(ciphertext, sizeof(ciphertext));
    explicit_bzero(csalt, sizeof(csalt));
    explicit_bzero(cdata, sizeof(cdata));
    return 0;

    inval:
    errno = EINVAL;
    return -1;
}

/*
 * user friendly functions
 */
int
bcrypt_newhash(const char *pass, int log_rounds, char *hash, size_t hashlen)
{
    char salt[BCRYPT_SALTSPACE];

    if (bcrypt_initsalt(log_rounds, (u_int8_t*)salt, sizeof(salt)) != 0)
        return -1;

    if (bcrypt_hashpass(pass, salt, hash, hashlen) != 0)
        return -1;

    explicit_bzero(salt, sizeof(salt));
    return 0;
}
DEF_WEAK(bcrypt_newhash);

int
bcrypt_checkpass(const char *pass, const char *goodhash)
{
    char hash[BCRYPT_HASHSPACE];

    if (bcrypt_hashpass(pass, goodhash, hash, sizeof(hash)) != 0)
        return -1;
    if (strlen(hash) != strlen(goodhash) ||
        timingsafe_bcmp(hash, goodhash, strlen(goodhash)) != 0) {
        errno = EACCES;
        return -1;
    }

    explicit_bzero(hash, sizeof(hash));
    return 0;
}
DEF_WEAK(bcrypt_checkpass);

/*
 * internal utilities
 */
static const u_int8_t Base64Code[] =
        "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

static const u_int8_t index_64[128] = {
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
        255, 255, 255, 255, 255, 255, 0, 1, 54, 55,
        56, 57, 58, 59, 60, 61, 62, 63, 255, 255,
        255, 255, 255, 255, 255, 2, 3, 4, 5, 6,
        7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
        255, 255, 255, 255, 255, 255, 28, 29, 30,
        31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50,
        51, 52, 53, 255, 255, 255, 255, 255
};
#define CHAR64(c)  ( (c) > 127 ? 255 : index_64[(c)])

/*
 * read buflen (after decoding) bytes of data from b64data
 */
static int
decode_base64(u_int8_t *buffer, size_t len, const char *b64data)
{
    u_int8_t *bp = buffer;
    const u_int8_t *p = (u_int8_t*)b64data;
    u_int8_t c1, c2, c3, c4;

    while (bp < buffer + len) {
        c1 = CHAR64(*p);
        /* Invalid data */
        if (c1 == 255)
            return -1;

        c2 = CHAR64(*(p + 1));
        if (c2 == 255)
            return -1;

        *bp++ = (c1 << 2) | ((c2 & 0x30) >> 4);
        if (bp >= buffer + len)
            break;

        c3 = CHAR64(*(p + 2));
        if (c3 == 255)
            return -1;

        *bp++ = ((c2 & 0x0f) << 4) | ((c3 & 0x3c) >> 2);
        if (bp >= buffer + len)
            break;

        c4 = CHAR64(*(p + 3));
        if (c4 == 255)
            return -1;
        *bp++ = ((c3 & 0x03) << 6) | c4;

        p += 4;
    }
    return 0;
}

/*
 * Turn len bytes of data into base64 encoded data.
 * This works without = padding.
 */
static int
encode_base64(char *b64buffer, const u_int8_t *data, size_t len)
{
    u_int8_t *bp = (u_int8_t*)b64buffer;
    const u_int8_t *p = data;
    u_int8_t c1, c2;

    while (p < data + len) {
        c1 = *p++;
        *bp++ = Base64Code[(c1 >> 2)];
        c1 = (c1 & 0x03) << 4;
        if (p >= data + len) {
            *bp++ = Base64Code[c1];
            break;
        }
        c2 = *p++;
        c1 |= (c2 >> 4) & 0x0f;
        *bp++ = Base64Code[c1];
        c1 = (c2 & 0x0f) << 2;
        if (p >= data + len) {
            *bp++ = Base64Code[c1];
            break;
        }
        c2 = *p++;
        c1 |= (c2 >> 6) & 0x03;
        *bp++ = Base64Code[c1];
        *bp++ = Base64Code[c2 & 0x3f];
    }
    *bp = '\0';
    return 0;
}

/*
 * classic interface
 */
char *
bcrypt_gensalt(u_int8_t log_rounds)
{
    static char    gsalt[BCRYPT_SALTSPACE];

    bcrypt_initsalt(log_rounds, (u_int8_t*)gsalt, sizeof(gsalt));

    return gsalt;
}

char *
bcrypt_pass(const char *pass, const char *salt)
{
    static char    gencrypted[BCRYPT_HASHSPACE];

    if (bcrypt_hashpass(pass, salt, gencrypted, sizeof(gencrypted)) != 0)
        return NULL;

    return gencrypted;
}
DEF_WEAK(bcrypt);
    /* 33: hashlib/bcrypt/blf.c */
/*	$OpenBSD: blf.c,v 1.7 2007/11/26 09:28:34 martynas Exp $	*/

/*
 * Blowfish block cipher for OpenBSD
 * Copyright 1997 Niels Provos <provos@physnet.uni-hamburg.de>
 * All rights reserved.
 *
 * Implementation advice by David Mazieres <dm@lcs.mit.edu>.
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

/*
 * This code is derived from section 14.3 and the given source
 * in section V of Applied Cryptography, second edition.
 * Blowfish is an unpatented fast block cipher designed by
 * Bruce Schneier.
 */



/* Function for Feistel Networks */

#define F(s, x) ((((s)[        (((x)>>24)&0xFF)]  \
		 + (s)[0x100 + (((x)>>16)&0xFF)]) \
		 ^ (s)[0x200 + (((x)>> 8)&0xFF)]) \
		 + (s)[0x300 + ( (x)     &0xFF)])

#define BLFRND(s,p,i,j,n) (i ^= F(s,j) ^ (p)[n])

void
Blowfish_encipher(blf_ctx *c, u_int32_t *x)
{
	u_int32_t Xl;
	u_int32_t Xr;
	u_int32_t *s = c->S[0];
	u_int32_t *p = c->P;

	Xl = x[0];
	Xr = x[1];

	Xl ^= p[0];
	BLFRND(s, p, Xr, Xl, 1); BLFRND(s, p, Xl, Xr, 2);
	BLFRND(s, p, Xr, Xl, 3); BLFRND(s, p, Xl, Xr, 4);
	BLFRND(s, p, Xr, Xl, 5); BLFRND(s, p, Xl, Xr, 6);
	BLFRND(s, p, Xr, Xl, 7); BLFRND(s, p, Xl, Xr, 8);
	BLFRND(s, p, Xr, Xl, 9); BLFRND(s, p, Xl, Xr, 10);
	BLFRND(s, p, Xr, Xl, 11); BLFRND(s, p, Xl, Xr, 12);
	BLFRND(s, p, Xr, Xl, 13); BLFRND(s, p, Xl, Xr, 14);
	BLFRND(s, p, Xr, Xl, 15); BLFRND(s, p, Xl, Xr, 16);

	x[0] = Xr ^ p[17];
	x[1] = Xl;
}

void
Blowfish_decipher(blf_ctx *c, u_int32_t *x)
{
	u_int32_t Xl;
	u_int32_t Xr;
	u_int32_t *s = c->S[0];
	u_int32_t *p = c->P;

	Xl = x[0];
	Xr = x[1];

	Xl ^= p[17];
	BLFRND(s, p, Xr, Xl, 16); BLFRND(s, p, Xl, Xr, 15);
	BLFRND(s, p, Xr, Xl, 14); BLFRND(s, p, Xl, Xr, 13);
	BLFRND(s, p, Xr, Xl, 12); BLFRND(s, p, Xl, Xr, 11);
	BLFRND(s, p, Xr, Xl, 10); BLFRND(s, p, Xl, Xr, 9);
	BLFRND(s, p, Xr, Xl, 8); BLFRND(s, p, Xl, Xr, 7);
	BLFRND(s, p, Xr, Xl, 6); BLFRND(s, p, Xl, Xr, 5);
	BLFRND(s, p, Xr, Xl, 4); BLFRND(s, p, Xl, Xr, 3);
	BLFRND(s, p, Xr, Xl, 2); BLFRND(s, p, Xl, Xr, 1);

	x[0] = Xr ^ p[0];
	x[1] = Xl;
}

void
Blowfish_initstate(blf_ctx *c)
{
	/* P-box and S-box tables initialized with digits of Pi */

	static const blf_ctx initstate =

	{ {
		{
			0xd1310ba6, 0x98dfb5ac, 0x2ffd72db, 0xd01adfb7,
			0xb8e1afed, 0x6a267e96, 0xba7c9045, 0xf12c7f99,
			0x24a19947, 0xb3916cf7, 0x0801f2e2, 0x858efc16,
			0x636920d8, 0x71574e69, 0xa458fea3, 0xf4933d7e,
			0x0d95748f, 0x728eb658, 0x718bcd58, 0x82154aee,
			0x7b54a41d, 0xc25a59b5, 0x9c30d539, 0x2af26013,
			0xc5d1b023, 0x286085f0, 0xca417918, 0xb8db38ef,
			0x8e79dcb0, 0x603a180e, 0x6c9e0e8b, 0xb01e8a3e,
			0xd71577c1, 0xbd314b27, 0x78af2fda, 0x55605c60,
			0xe65525f3, 0xaa55ab94, 0x57489862, 0x63e81440,
			0x55ca396a, 0x2aab10b6, 0xb4cc5c34, 0x1141e8ce,
			0xa15486af, 0x7c72e993, 0xb3ee1411, 0x636fbc2a,
			0x2ba9c55d, 0x741831f6, 0xce5c3e16, 0x9b87931e,
			0xafd6ba33, 0x6c24cf5c, 0x7a325381, 0x28958677,
			0x3b8f4898, 0x6b4bb9af, 0xc4bfe81b, 0x66282193,
			0x61d809cc, 0xfb21a991, 0x487cac60, 0x5dec8032,
			0xef845d5d, 0xe98575b1, 0xdc262302, 0xeb651b88,
			0x23893e81, 0xd396acc5, 0x0f6d6ff3, 0x83f44239,
			0x2e0b4482, 0xa4842004, 0x69c8f04a, 0x9e1f9b5e,
			0x21c66842, 0xf6e96c9a, 0x670c9c61, 0xabd388f0,
			0x6a51a0d2, 0xd8542f68, 0x960fa728, 0xab5133a3,
			0x6eef0b6c, 0x137a3be4, 0xba3bf050, 0x7efb2a98,
			0xa1f1651d, 0x39af0176, 0x66ca593e, 0x82430e88,
			0x8cee8619, 0x456f9fb4, 0x7d84a5c3, 0x3b8b5ebe,
			0xe06f75d8, 0x85c12073, 0x401a449f, 0x56c16aa6,
			0x4ed3aa62, 0x363f7706, 0x1bfedf72, 0x429b023d,
			0x37d0d724, 0xd00a1248, 0xdb0fead3, 0x49f1c09b,
			0x075372c9, 0x80991b7b, 0x25d479d8, 0xf6e8def7,
			0xe3fe501a, 0xb6794c3b, 0x976ce0bd, 0x04c006ba,
			0xc1a94fb6, 0x409f60c4, 0x5e5c9ec2, 0x196a2463,
			0x68fb6faf, 0x3e6c53b5, 0x1339b2eb, 0x3b52ec6f,
			0x6dfc511f, 0x9b30952c, 0xcc814544, 0xaf5ebd09,
			0xbee3d004, 0xde334afd, 0x660f2807, 0x192e4bb3,
			0xc0cba857, 0x45c8740f, 0xd20b5f39, 0xb9d3fbdb,
			0x5579c0bd, 0x1a60320a, 0xd6a100c6, 0x402c7279,
			0x679f25fe, 0xfb1fa3cc, 0x8ea5e9f8, 0xdb3222f8,
			0x3c7516df, 0xfd616b15, 0x2f501ec8, 0xad0552ab,
			0x323db5fa, 0xfd238760, 0x53317b48, 0x3e00df82,
			0x9e5c57bb, 0xca6f8ca0, 0x1a87562e, 0xdf1769db,
			0xd542a8f6, 0x287effc3, 0xac6732c6, 0x8c4f5573,
			0x695b27b0, 0xbbca58c8, 0xe1ffa35d, 0xb8f011a0,
			0x10fa3d98, 0xfd2183b8, 0x4afcb56c, 0x2dd1d35b,
			0x9a53e479, 0xb6f84565, 0xd28e49bc, 0x4bfb9790,
			0xe1ddf2da, 0xa4cb7e33, 0x62fb1341, 0xcee4c6e8,
			0xef20cada, 0x36774c01, 0xd07e9efe, 0x2bf11fb4,
			0x95dbda4d, 0xae909198, 0xeaad8e71, 0x6b93d5a0,
			0xd08ed1d0, 0xafc725e0, 0x8e3c5b2f, 0x8e7594b7,
			0x8ff6e2fb, 0xf2122b64, 0x8888b812, 0x900df01c,
			0x4fad5ea0, 0x688fc31c, 0xd1cff191, 0xb3a8c1ad,
			0x2f2f2218, 0xbe0e1777, 0xea752dfe, 0x8b021fa1,
			0xe5a0cc0f, 0xb56f74e8, 0x18acf3d6, 0xce89e299,
			0xb4a84fe0, 0xfd13e0b7, 0x7cc43b81, 0xd2ada8d9,
			0x165fa266, 0x80957705, 0x93cc7314, 0x211a1477,
			0xe6ad2065, 0x77b5fa86, 0xc75442f5, 0xfb9d35cf,
			0xebcdaf0c, 0x7b3e89a0, 0xd6411bd3, 0xae1e7e49,
			0x00250e2d, 0x2071b35e, 0x226800bb, 0x57b8e0af,
			0x2464369b, 0xf009b91e, 0x5563911d, 0x59dfa6aa,
			0x78c14389, 0xd95a537f, 0x207d5ba2, 0x02e5b9c5,
			0x83260376, 0x6295cfa9, 0x11c81968, 0x4e734a41,
			0xb3472dca, 0x7b14a94a, 0x1b510052, 0x9a532915,
			0xd60f573f, 0xbc9bc6e4, 0x2b60a476, 0x81e67400,
			0x08ba6fb5, 0x571be91f, 0xf296ec6b, 0x2a0dd915,
			0xb6636521, 0xe7b9f9b6, 0xff34052e, 0xc5855664,
		0x53b02d5d, 0xa99f8fa1, 0x08ba4799, 0x6e85076a},
		{
			0x4b7a70e9, 0xb5b32944, 0xdb75092e, 0xc4192623,
			0xad6ea6b0, 0x49a7df7d, 0x9cee60b8, 0x8fedb266,
			0xecaa8c71, 0x699a17ff, 0x5664526c, 0xc2b19ee1,
			0x193602a5, 0x75094c29, 0xa0591340, 0xe4183a3e,
			0x3f54989a, 0x5b429d65, 0x6b8fe4d6, 0x99f73fd6,
			0xa1d29c07, 0xefe830f5, 0x4d2d38e6, 0xf0255dc1,
			0x4cdd2086, 0x8470eb26, 0x6382e9c6, 0x021ecc5e,
			0x09686b3f, 0x3ebaefc9, 0x3c971814, 0x6b6a70a1,
			0x687f3584, 0x52a0e286, 0xb79c5305, 0xaa500737,
			0x3e07841c, 0x7fdeae5c, 0x8e7d44ec, 0x5716f2b8,
			0xb03ada37, 0xf0500c0d, 0xf01c1f04, 0x0200b3ff,
			0xae0cf51a, 0x3cb574b2, 0x25837a58, 0xdc0921bd,
			0xd19113f9, 0x7ca92ff6, 0x94324773, 0x22f54701,
			0x3ae5e581, 0x37c2dadc, 0xc8b57634, 0x9af3dda7,
			0xa9446146, 0x0fd0030e, 0xecc8c73e, 0xa4751e41,
			0xe238cd99, 0x3bea0e2f, 0x3280bba1, 0x183eb331,
			0x4e548b38, 0x4f6db908, 0x6f420d03, 0xf60a04bf,
			0x2cb81290, 0x24977c79, 0x5679b072, 0xbcaf89af,
			0xde9a771f, 0xd9930810, 0xb38bae12, 0xdccf3f2e,
			0x5512721f, 0x2e6b7124, 0x501adde6, 0x9f84cd87,
			0x7a584718, 0x7408da17, 0xbc9f9abc, 0xe94b7d8c,
			0xec7aec3a, 0xdb851dfa, 0x63094366, 0xc464c3d2,
			0xef1c1847, 0x3215d908, 0xdd433b37, 0x24c2ba16,
			0x12a14d43, 0x2a65c451, 0x50940002, 0x133ae4dd,
			0x71dff89e, 0x10314e55, 0x81ac77d6, 0x5f11199b,
			0x043556f1, 0xd7a3c76b, 0x3c11183b, 0x5924a509,
			0xf28fe6ed, 0x97f1fbfa, 0x9ebabf2c, 0x1e153c6e,
			0x86e34570, 0xeae96fb1, 0x860e5e0a, 0x5a3e2ab3,
			0x771fe71c, 0x4e3d06fa, 0x2965dcb9, 0x99e71d0f,
			0x803e89d6, 0x5266c825, 0x2e4cc978, 0x9c10b36a,
			0xc6150eba, 0x94e2ea78, 0xa5fc3c53, 0x1e0a2df4,
			0xf2f74ea7, 0x361d2b3d, 0x1939260f, 0x19c27960,
			0x5223a708, 0xf71312b6, 0xebadfe6e, 0xeac31f66,
			0xe3bc4595, 0xa67bc883, 0xb17f37d1, 0x018cff28,
			0xc332ddef, 0xbe6c5aa5, 0x65582185, 0x68ab9802,
			0xeecea50f, 0xdb2f953b, 0x2aef7dad, 0x5b6e2f84,
			0x1521b628, 0x29076170, 0xecdd4775, 0x619f1510,
			0x13cca830, 0xeb61bd96, 0x0334fe1e, 0xaa0363cf,
			0xb5735c90, 0x4c70a239, 0xd59e9e0b, 0xcbaade14,
			0xeecc86bc, 0x60622ca7, 0x9cab5cab, 0xb2f3846e,
			0x648b1eaf, 0x19bdf0ca, 0xa02369b9, 0x655abb50,
			0x40685a32, 0x3c2ab4b3, 0x319ee9d5, 0xc021b8f7,
			0x9b540b19, 0x875fa099, 0x95f7997e, 0x623d7da8,
			0xf837889a, 0x97e32d77, 0x11ed935f, 0x16681281,
			0x0e358829, 0xc7e61fd6, 0x96dedfa1, 0x7858ba99,
			0x57f584a5, 0x1b227263, 0x9b83c3ff, 0x1ac24696,
			0xcdb30aeb, 0x532e3054, 0x8fd948e4, 0x6dbc3128,
			0x58ebf2ef, 0x34c6ffea, 0xfe28ed61, 0xee7c3c73,
			0x5d4a14d9, 0xe864b7e3, 0x42105d14, 0x203e13e0,
			0x45eee2b6, 0xa3aaabea, 0xdb6c4f15, 0xfacb4fd0,
			0xc742f442, 0xef6abbb5, 0x654f3b1d, 0x41cd2105,
			0xd81e799e, 0x86854dc7, 0xe44b476a, 0x3d816250,
			0xcf62a1f2, 0x5b8d2646, 0xfc8883a0, 0xc1c7b6a3,
			0x7f1524c3, 0x69cb7492, 0x47848a0b, 0x5692b285,
			0x095bbf00, 0xad19489d, 0x1462b174, 0x23820e00,
			0x58428d2a, 0x0c55f5ea, 0x1dadf43e, 0x233f7061,
			0x3372f092, 0x8d937e41, 0xd65fecf1, 0x6c223bdb,
			0x7cde3759, 0xcbee7460, 0x4085f2a7, 0xce77326e,
			0xa6078084, 0x19f8509e, 0xe8efd855, 0x61d99735,
			0xa969a7aa, 0xc50c06c2, 0x5a04abfc, 0x800bcadc,
			0x9e447a2e, 0xc3453484, 0xfdd56705, 0x0e1e9ec9,
			0xdb73dbd3, 0x105588cd, 0x675fda79, 0xe3674340,
			0xc5c43465, 0x713e38d8, 0x3d28f89e, 0xf16dff20,
		0x153e21e7, 0x8fb03d4a, 0xe6e39f2b, 0xdb83adf7},
		{
			0xe93d5a68, 0x948140f7, 0xf64c261c, 0x94692934,
			0x411520f7, 0x7602d4f7, 0xbcf46b2e, 0xd4a20068,
			0xd4082471, 0x3320f46a, 0x43b7d4b7, 0x500061af,
			0x1e39f62e, 0x97244546, 0x14214f74, 0xbf8b8840,
			0x4d95fc1d, 0x96b591af, 0x70f4ddd3, 0x66a02f45,
			0xbfbc09ec, 0x03bd9785, 0x7fac6dd0, 0x31cb8504,
			0x96eb27b3, 0x55fd3941, 0xda2547e6, 0xabca0a9a,
			0x28507825, 0x530429f4, 0x0a2c86da, 0xe9b66dfb,
			0x68dc1462, 0xd7486900, 0x680ec0a4, 0x27a18dee,
			0x4f3ffea2, 0xe887ad8c, 0xb58ce006, 0x7af4d6b6,
			0xaace1e7c, 0xd3375fec, 0xce78a399, 0x406b2a42,
			0x20fe9e35, 0xd9f385b9, 0xee39d7ab, 0x3b124e8b,
			0x1dc9faf7, 0x4b6d1856, 0x26a36631, 0xeae397b2,
			0x3a6efa74, 0xdd5b4332, 0x6841e7f7, 0xca7820fb,
			0xfb0af54e, 0xd8feb397, 0x454056ac, 0xba489527,
			0x55533a3a, 0x20838d87, 0xfe6ba9b7, 0xd096954b,
			0x55a867bc, 0xa1159a58, 0xcca92963, 0x99e1db33,
			0xa62a4a56, 0x3f3125f9, 0x5ef47e1c, 0x9029317c,
			0xfdf8e802, 0x04272f70, 0x80bb155c, 0x05282ce3,
			0x95c11548, 0xe4c66d22, 0x48c1133f, 0xc70f86dc,
			0x07f9c9ee, 0x41041f0f, 0x404779a4, 0x5d886e17,
			0x325f51eb, 0xd59bc0d1, 0xf2bcc18f, 0x41113564,
			0x257b7834, 0x602a9c60, 0xdff8e8a3, 0x1f636c1b,
			0x0e12b4c2, 0x02e1329e, 0xaf664fd1, 0xcad18115,
			0x6b2395e0, 0x333e92e1, 0x3b240b62, 0xeebeb922,
			0x85b2a20e, 0xe6ba0d99, 0xde720c8c, 0x2da2f728,
			0xd0127845, 0x95b794fd, 0x647d0862, 0xe7ccf5f0,
			0x5449a36f, 0x877d48fa, 0xc39dfd27, 0xf33e8d1e,
			0x0a476341, 0x992eff74, 0x3a6f6eab, 0xf4f8fd37,
			0xa812dc60, 0xa1ebddf8, 0x991be14c, 0xdb6e6b0d,
			0xc67b5510, 0x6d672c37, 0x2765d43b, 0xdcd0e804,
			0xf1290dc7, 0xcc00ffa3, 0xb5390f92, 0x690fed0b,
			0x667b9ffb, 0xcedb7d9c, 0xa091cf0b, 0xd9155ea3,
			0xbb132f88, 0x515bad24, 0x7b9479bf, 0x763bd6eb,
			0x37392eb3, 0xcc115979, 0x8026e297, 0xf42e312d,
			0x6842ada7, 0xc66a2b3b, 0x12754ccc, 0x782ef11c,
			0x6a124237, 0xb79251e7, 0x06a1bbe6, 0x4bfb6350,
			0x1a6b1018, 0x11caedfa, 0x3d25bdd8, 0xe2e1c3c9,
			0x44421659, 0x0a121386, 0xd90cec6e, 0xd5abea2a,
			0x64af674e, 0xda86a85f, 0xbebfe988, 0x64e4c3fe,
			0x9dbc8057, 0xf0f7c086, 0x60787bf8, 0x6003604d,
			0xd1fd8346, 0xf6381fb0, 0x7745ae04, 0xd736fccc,
			0x83426b33, 0xf01eab71, 0xb0804187, 0x3c005e5f,
			0x77a057be, 0xbde8ae24, 0x55464299, 0xbf582e61,
			0x4e58f48f, 0xf2ddfda2, 0xf474ef38, 0x8789bdc2,
			0x5366f9c3, 0xc8b38e74, 0xb475f255, 0x46fcd9b9,
			0x7aeb2661, 0x8b1ddf84, 0x846a0e79, 0x915f95e2,
			0x466e598e, 0x20b45770, 0x8cd55591, 0xc902de4c,
			0xb90bace1, 0xbb8205d0, 0x11a86248, 0x7574a99e,
			0xb77f19b6, 0xe0a9dc09, 0x662d09a1, 0xc4324633,
			0xe85a1f02, 0x09f0be8c, 0x4a99a025, 0x1d6efe10,
			0x1ab93d1d, 0x0ba5a4df, 0xa186f20f, 0x2868f169,
			0xdcb7da83, 0x573906fe, 0xa1e2ce9b, 0x4fcd7f52,
			0x50115e01, 0xa70683fa, 0xa002b5c4, 0x0de6d027,
			0x9af88c27, 0x773f8641, 0xc3604c06, 0x61a806b5,
			0xf0177a28, 0xc0f586e0, 0x006058aa, 0x30dc7d62,
			0x11e69ed7, 0x2338ea63, 0x53c2dd94, 0xc2c21634,
			0xbbcbee56, 0x90bcb6de, 0xebfc7da1, 0xce591d76,
			0x6f05e409, 0x4b7c0188, 0x39720a3d, 0x7c927c24,
			0x86e3725f, 0x724d9db9, 0x1ac15bb4, 0xd39eb8fc,
			0xed545578, 0x08fca5b5, 0xd83d7cd3, 0x4dad0fc4,
			0x1e50ef5e, 0xb161e6f8, 0xa28514d9, 0x6c51133c,
			0x6fd5c7e7, 0x56e14ec4, 0x362abfce, 0xddc6c837,
		0xd79a3234, 0x92638212, 0x670efa8e, 0x406000e0},
		{
			0x3a39ce37, 0xd3faf5cf, 0xabc27737, 0x5ac52d1b,
			0x5cb0679e, 0x4fa33742, 0xd3822740, 0x99bc9bbe,
			0xd5118e9d, 0xbf0f7315, 0xd62d1c7e, 0xc700c47b,
			0xb78c1b6b, 0x21a19045, 0xb26eb1be, 0x6a366eb4,
			0x5748ab2f, 0xbc946e79, 0xc6a376d2, 0x6549c2c8,
			0x530ff8ee, 0x468dde7d, 0xd5730a1d, 0x4cd04dc6,
			0x2939bbdb, 0xa9ba4650, 0xac9526e8, 0xbe5ee304,
			0xa1fad5f0, 0x6a2d519a, 0x63ef8ce2, 0x9a86ee22,
			0xc089c2b8, 0x43242ef6, 0xa51e03aa, 0x9cf2d0a4,
			0x83c061ba, 0x9be96a4d, 0x8fe51550, 0xba645bd6,
			0x2826a2f9, 0xa73a3ae1, 0x4ba99586, 0xef5562e9,
			0xc72fefd3, 0xf752f7da, 0x3f046f69, 0x77fa0a59,
			0x80e4a915, 0x87b08601, 0x9b09e6ad, 0x3b3ee593,
			0xe990fd5a, 0x9e34d797, 0x2cf0b7d9, 0x022b8b51,
			0x96d5ac3a, 0x017da67d, 0xd1cf3ed6, 0x7c7d2d28,
			0x1f9f25cf, 0xadf2b89b, 0x5ad6b472, 0x5a88f54c,
			0xe029ac71, 0xe019a5e6, 0x47b0acfd, 0xed93fa9b,
			0xe8d3c48d, 0x283b57cc, 0xf8d56629, 0x79132e28,
			0x785f0191, 0xed756055, 0xf7960e44, 0xe3d35e8c,
			0x15056dd4, 0x88f46dba, 0x03a16125, 0x0564f0bd,
			0xc3eb9e15, 0x3c9057a2, 0x97271aec, 0xa93a072a,
			0x1b3f6d9b, 0x1e6321f5, 0xf59c66fb, 0x26dcf319,
			0x7533d928, 0xb155fdf5, 0x03563482, 0x8aba3cbb,
			0x28517711, 0xc20ad9f8, 0xabcc5167, 0xccad925f,
			0x4de81751, 0x3830dc8e, 0x379d5862, 0x9320f991,
			0xea7a90c2, 0xfb3e7bce, 0x5121ce64, 0x774fbe32,
			0xa8b6e37e, 0xc3293d46, 0x48de5369, 0x6413e680,
			0xa2ae0810, 0xdd6db224, 0x69852dfd, 0x09072166,
			0xb39a460a, 0x6445c0dd, 0x586cdecf, 0x1c20c8ae,
			0x5bbef7dd, 0x1b588d40, 0xccd2017f, 0x6bb4e3bb,
			0xdda26a7e, 0x3a59ff45, 0x3e350a44, 0xbcb4cdd5,
			0x72eacea8, 0xfa6484bb, 0x8d6612ae, 0xbf3c6f47,
			0xd29be463, 0x542f5d9e, 0xaec2771b, 0xf64e6370,
			0x740e0d8d, 0xe75b1357, 0xf8721671, 0xaf537d5d,
			0x4040cb08, 0x4eb4e2cc, 0x34d2466a, 0x0115af84,
			0xe1b00428, 0x95983a1d, 0x06b89fb4, 0xce6ea048,
			0x6f3f3b82, 0x3520ab82, 0x011a1d4b, 0x277227f8,
			0x611560b1, 0xe7933fdc, 0xbb3a792b, 0x344525bd,
			0xa08839e1, 0x51ce794b, 0x2f32c9b7, 0xa01fbac9,
			0xe01cc87e, 0xbcc7d1f6, 0xcf0111c3, 0xa1e8aac7,
			0x1a908749, 0xd44fbd9a, 0xd0dadecb, 0xd50ada38,
			0x0339c32a, 0xc6913667, 0x8df9317c, 0xe0b12b4f,
			0xf79e59b7, 0x43f5bb3a, 0xf2d519ff, 0x27d9459c,
			0xbf97222c, 0x15e6fc2a, 0x0f91fc71, 0x9b941525,
			0xfae59361, 0xceb69ceb, 0xc2a86459, 0x12baa8d1,
			0xb6c1075e, 0xe3056a0c, 0x10d25065, 0xcb03a442,
			0xe0ec6e0e, 0x1698db3b, 0x4c98a0be, 0x3278e964,
			0x9f1f9532, 0xe0d392df, 0xd3a0342b, 0x8971f21e,
			0x1b0a7441, 0x4ba3348c, 0xc5be7120, 0xc37632d8,
			0xdf359f8d, 0x9b992f2e, 0xe60b6f47, 0x0fe3f11d,
			0xe54cda54, 0x1edad891, 0xce6279cf, 0xcd3e7e6f,
			0x1618b166, 0xfd2c1d05, 0x848fd2c5, 0xf6fb2299,
			0xf523f357, 0xa6327623, 0x93a83531, 0x56cccd02,
			0xacf08162, 0x5a75ebb5, 0x6e163697, 0x88d273cc,
			0xde966292, 0x81b949d0, 0x4c50901b, 0x71c65614,
			0xe6c6c7bd, 0x327a140a, 0x45e1d006, 0xc3f27b9a,
			0xc9aa53fd, 0x62a80f00, 0xbb25bfe2, 0x35bdd2f6,
			0x71126905, 0xb2040222, 0xb6cbcf7c, 0xcd769c2b,
			0x53113ec0, 0x1640e3d3, 0x38abbd60, 0x2547adf0,
			0xba38209c, 0xf746ce76, 0x77afa1c5, 0x20756060,
			0x85cbfe4e, 0x8ae88dd8, 0x7aaaf9b0, 0x4cf9aa7e,
			0x1948c25c, 0x02fb8a8c, 0x01c36ae4, 0xd6ebe1f9,
			0x90d4f869, 0xa65cdea0, 0x3f09252d, 0xc208e69f,
		0xb74e6132, 0xce77e25b, 0x578fdfe3, 0x3ac372e6}
	},
	{
		0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344,
		0xa4093822, 0x299f31d0, 0x082efa98, 0xec4e6c89,
		0x452821e6, 0x38d01377, 0xbe5466cf, 0x34e90c6c,
		0xc0ac29b7, 0xc97c50dd, 0x3f84d5b5, 0xb5470917,
		0x9216d5d9, 0x8979fb1b
	} };

	*c = initstate;
}

u_int32_t
Blowfish_stream2word(const u_int8_t *data, u_int16_t databytes,
    u_int16_t *current)
{
	u_int8_t i;
	u_int16_t j;
	u_int32_t temp;

	temp = 0x00000000;
	j = *current;

	for (i = 0; i < 4; i++, j++) {
		if (j >= databytes)
			j = 0;
		temp = (temp << 8) | data[j];
	}

	*current = j;
	return temp;
}

void
Blowfish_expand0state(blf_ctx *c, const u_int8_t *key, u_int16_t keybytes)
{
	u_int16_t i;
	u_int16_t j;
	u_int16_t k;
	u_int32_t temp;
	u_int32_t data[2];

	j = 0;
	for (i = 0; i < BLF_N + 2; i++) {
		/* Extract 4 int8 to 1 int32 from keystream */
		temp = Blowfish_stream2word(key, keybytes, &j);
		c->P[i] = c->P[i] ^ temp;
	}

	j = 0;
	data[0] = 0x00000000;
	data[1] = 0x00000000;
	for (i = 0; i < BLF_N + 2; i += 2) {
		Blowfish_encipher(c, data);

		c->P[i] = data[0];
		c->P[i + 1] = data[1];
	}

	for (i = 0; i < 4; i++) {
		for (k = 0; k < 256; k += 2) {
			Blowfish_encipher(c, data);

			c->S[i][k] = data[0];
			c->S[i][k + 1] = data[1];
		}
	}
}


void
Blowfish_expandstate(blf_ctx *c, const u_int8_t *data, u_int16_t databytes,
    const u_int8_t *key, u_int16_t keybytes)
{
	u_int16_t i;
	u_int16_t j;
	u_int16_t k;
	u_int32_t temp;
	u_int32_t d[2];

	j = 0;
	for (i = 0; i < BLF_N + 2; i++) {
		/* Extract 4 int8 to 1 int32 from keystream */
		temp = Blowfish_stream2word(key, keybytes, &j);
		c->P[i] = c->P[i] ^ temp;
	}

	j = 0;
	d[0] = 0x00000000;
	d[1] = 0x00000000;
	for (i = 0; i < BLF_N + 2; i += 2) {
		d[0] ^= Blowfish_stream2word(data, databytes, &j);
		d[1] ^= Blowfish_stream2word(data, databytes, &j);
		Blowfish_encipher(c, d);

		c->P[i] = d[0];
		c->P[i + 1] = d[1];
	}

	for (i = 0; i < 4; i++) {
		for (k = 0; k < 256; k += 2) {
			d[0]^= Blowfish_stream2word(data, databytes, &j);
			d[1] ^= Blowfish_stream2word(data, databytes, &j);
			Blowfish_encipher(c, d);

			c->S[i][k] = d[0];
			c->S[i][k + 1] = d[1];
		}
	}

}

void
blf_key(blf_ctx *c, const u_int8_t *k, u_int16_t len)
{
	/* Initialize S-boxes and subkeys with Pi */
	Blowfish_initstate(c);

	/* Transform S-boxes and subkeys with key */
	Blowfish_expand0state(c, k, len);
}

void
blf_enc(blf_ctx *c, u_int32_t *data, u_int16_t blocks)
{
	u_int32_t *d;
	u_int16_t i;

	d = data;
	for (i = 0; i < blocks; i++) {
		Blowfish_encipher(c, d);
		d += 2;
	}
}

void
blf_dec(blf_ctx *c, u_int32_t *data, u_int16_t blocks)
{
	u_int32_t *d;
	u_int16_t i;

	d = data;
	for (i = 0; i < blocks; i++) {
		Blowfish_decipher(c, d);
		d += 2;
	}
}

void
blf_ecb_encrypt(blf_ctx *c, u_int8_t *data, u_int32_t len)
{
	u_int32_t l, r, d[2];
	u_int32_t i;

	for (i = 0; i < len; i += 8) {
		l = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
		r = data[4] << 24 | data[5] << 16 | data[6] << 8 | data[7];
		d[0] = l;
		d[1] = r;
		Blowfish_encipher(c, d);
		l = d[0];
		r = d[1];
		data[0] = l >> 24 & 0xff;
		data[1] = l >> 16 & 0xff;
		data[2] = l >> 8 & 0xff;
		data[3] = l & 0xff;
		data[4] = r >> 24 & 0xff;
		data[5] = r >> 16 & 0xff;
		data[6] = r >> 8 & 0xff;
		data[7] = r & 0xff;
		data += 8;
	}
}

void
blf_ecb_decrypt(blf_ctx *c, u_int8_t *data, u_int32_t len)
{
	u_int32_t l, r, d[2];
	u_int32_t i;

	for (i = 0; i < len; i += 8) {
		l = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
		r = data[4] << 24 | data[5] << 16 | data[6] << 8 | data[7];
		d[0] = l;
		d[1] = r;
		Blowfish_decipher(c, d);
		l = d[0];
		r = d[1];
		data[0] = l >> 24 & 0xff;
		data[1] = l >> 16 & 0xff;
		data[2] = l >> 8 & 0xff;
		data[3] = l & 0xff;
		data[4] = r >> 24 & 0xff;
		data[5] = r >> 16 & 0xff;
		data[6] = r >> 8 & 0xff;
		data[7] = r & 0xff;
		data += 8;
	}
}

void
blf_cbc_encrypt(blf_ctx *c, u_int8_t *iv, u_int8_t *data, u_int32_t len)
{
	u_int32_t l, r, d[2];
	u_int32_t i, j;

	for (i = 0; i < len; i += 8) {
		for (j = 0; j < 8; j++)
			data[j] ^= iv[j];
		l = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
		r = data[4] << 24 | data[5] << 16 | data[6] << 8 | data[7];
		d[0] = l;
		d[1] = r;
		Blowfish_encipher(c, d);
		l = d[0];
		r = d[1];
		data[0] = l >> 24 & 0xff;
		data[1] = l >> 16 & 0xff;
		data[2] = l >> 8 & 0xff;
		data[3] = l & 0xff;
		data[4] = r >> 24 & 0xff;
		data[5] = r >> 16 & 0xff;
		data[6] = r >> 8 & 0xff;
		data[7] = r & 0xff;
		iv = data;
		data += 8;
	}
}

void
blf_cbc_decrypt(blf_ctx *c, u_int8_t *iva, u_int8_t *data, u_int32_t len)
{
	u_int32_t l, r, d[2];
	u_int8_t *iv;
	u_int32_t i, j;

	iv = data + len - 16;
	data = data + len - 8;
	for (i = len - 8; i >= 8; i -= 8) {
		l = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
		r = data[4] << 24 | data[5] << 16 | data[6] << 8 | data[7];
		d[0] = l;
		d[1] = r;
		Blowfish_decipher(c, d);
		l = d[0];
		r = d[1];
		data[0] = l >> 24 & 0xff;
		data[1] = l >> 16 & 0xff;
		data[2] = l >> 8 & 0xff;
		data[3] = l & 0xff;
		data[4] = r >> 24 & 0xff;
		data[5] = r >> 16 & 0xff;
		data[6] = r >> 8 & 0xff;
		data[7] = r & 0xff;
		for (j = 0; j < 8; j++)
			data[j] ^= iv[j];
		iv -= 8;
		data -= 8;
	}
	l = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
	r = data[4] << 24 | data[5] << 16 | data[6] << 8 | data[7];
	d[0] = l;
	d[1] = r;
	Blowfish_decipher(c, d);
	l = d[0];
	r = d[1];
	data[0] = l >> 24 & 0xff;
	data[1] = l >> 16 & 0xff;
	data[2] = l >> 8 & 0xff;
	data[3] = l & 0xff;
	data[4] = r >> 24 & 0xff;
	data[5] = r >> 16 & 0xff;
	data[6] = r >> 8 & 0xff;
	data[7] = r & 0xff;
	for (j = 0; j < 8; j++)
		data[j] ^= iva[j];
}

    /* 34: hashlib/bcrypt/timingsafe_bcmp.c */
/*	$OpenBSD: timingsafe_bcmp.c,v 1.1 2010/09/24 13:33:00 matthew Exp $	*/
/*
 * Copyright (c) 2010 Damien Miller.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


int
timingsafe_bcmp(const void *b1, const void *b2, size_t n)
{
	const unsigned char *p1 = b1, *p2 = b2;
	int ret = 0;

	for (; n > 0; n--)
		ret |= *p1++ ^ *p2++;
	return (ret != 0);
}

    /* 35: hashlib.c */

static Value sha256(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "sha256() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "Argument passed to sha256() must be a string.");
        return EMPTY_VAL;
    }

    ObjString *string = AS_STRING(args[0]);

    uint8_t digest[32];
    struct tc_sha256_state_struct s;

    if (!tc_sha256_init(&s)) {
        return NIL_VAL;
    }

    if (!tc_sha256_update(&s, (const uint8_t *) string->chars, string->length)) {
        return NIL_VAL;
    }

    if (!tc_sha256_final(digest, &s)) {
        return NIL_VAL;
    }

    char buffer[65];

    for (int i = 0; i < 32; i++ ) {
        sprintf( buffer + i * 2, "%02x", digest[i] );
    }

    return OBJ_VAL(copyString(vm, buffer, 64));
}

static Value hmac(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 2 && argCount != 3) {
        runtimeError(vm, "hmac() takes 2 or 3 arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError(vm, "Arguments passed to hmac() must be a string.");
        return EMPTY_VAL;
    }

    bool raw = false;

    if (argCount == 3) {
        if (!IS_BOOL(args[2])) {
            runtimeError(vm, "Optional argument passed to hmac() must be a boolean.");
            return EMPTY_VAL;
        }

        raw = AS_BOOL(args[2]);
    }

    ObjString *key = AS_STRING(args[0]);
    ObjString *string = AS_STRING(args[1]);

    uint8_t digest[32];
    struct tc_hmac_state_struct h;
    (void)tc_hmac_set_key(&h, (const uint8_t *) key->chars, key->length);
    tc_hmac_init(&h);
    tc_hmac_update(&h, string->chars, string->length);
    tc_hmac_final(digest, TC_SHA256_DIGEST_SIZE, &h);

    if (!raw) {
        char buffer[65];

        for (int i = 0; i < 32; i++ ) {
            sprintf( buffer + i * 2, "%02x", digest[i] );
        }

        return OBJ_VAL(copyString(vm, buffer, 64));
    }

    return OBJ_VAL(copyString(vm, (const char *)digest, 32));
}

static Value bcrypt(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError(vm, "bcrypt() takes 1 or 2 arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "Argument passed to bcrypt() must be a string.");
        return EMPTY_VAL;
    }

    int rounds = 8;

    if (argCount == 2) {
        if (!IS_NUMBER(args[1])) {
            runtimeError(vm, "Optional argument passed to bcrypt() must be a number.");
            return EMPTY_VAL;
        }

        rounds = AS_NUMBER(args[1]);
    }

    char *salt = bcrypt_gensalt(rounds);
    char *hash = bcrypt_pass(AS_CSTRING(args[0]), salt);

    return OBJ_VAL(copyString(vm, hash, strlen(hash)));
}

static Value bcryptVerify(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "bcryptVerify() takes 2 arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError(vm, "Arguments passed to bcryptVerify() must be a string.");
        return EMPTY_VAL;
    }

    ObjString *stringA = AS_STRING(args[0]);
    ObjString *stringB = AS_STRING(args[1]);

    return BOOL_VAL(bcrypt_checkpass(stringA->chars, stringB->chars) == 0);
}

static Value verify(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "verify() takes 2 arguments (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0]) || !IS_STRING(args[1])) {
        runtimeError(vm, "Arguments passed to verify() must be a string.");
        return EMPTY_VAL;
    }

    ObjString *stringA = AS_STRING(args[0]);
    ObjString *stringB = AS_STRING(args[1]);

    if (stringA->length != stringB->length) {
        return FALSE_VAL;
    }

    return BOOL_VAL(_compare((const uint8_t *) stringA->chars, (const uint8_t *) stringB->chars, stringA->length) == 0);
}

ObjModule *createHashlibModule(DictuVM *vm) {
    ObjString *name = copyString(vm, "Hashlib", 7);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Http methods
     */
    defineNative(vm, &module->values, "sha256", sha256);
    defineNative(vm, &module->values, "hmac", hmac);
    defineNative(vm, &module->values, "bcrypt", bcrypt);
    defineNative(vm, &module->values, "verify", verify);
    defineNative(vm, &module->values, "bcryptVerify", bcryptVerify);

    pop(vm);
    pop(vm);

    return module;
}

#ifndef DISABLE_HTTP

    /* 36: http.c */

static void createResponse(DictuVM *vm, Response *response) {
    response->vm = vm;
    response->headers = newList(vm);
    // Push to stack to avoid GC
    push(vm, OBJ_VAL(response->headers));

    response->len = 0;
    response->res = NULL;
}

static size_t writeResponse(char *ptr, size_t size, size_t nmemb, void *data) {
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

static size_t writeHeaders(char *ptr, size_t size, size_t nitems, void *data) {
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

static ObjDict *endRequest(DictuVM *vm, CURL *curl, Response response) {
    // Get status code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.statusCode);
    ObjString *content;
    if (response.res != NULL) {
        content = takeString(vm, response.res, response.len);
    } else {
        content = copyString(vm, "", 0);
    }

    // Push to stack to avoid GC
    push(vm, OBJ_VAL(content));

    ObjDict *responseVal = newDict(vm);
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

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return responseVal;
}

static Value get(DictuVM *vm, int argCount, Value *args) {
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
            /* always cleanup */
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            pop(vm);

            char *errorString = (char *) curl_easy_strerror(curlResponse);
            return newResultError(vm, errorString);
        }

        return newResultSuccess(vm, OBJ_VAL(endRequest(vm, curl, response)));
    }

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    pop(vm);

    char *errorString = (char *) curl_easy_strerror(CURLE_FAILED_INIT);
    return newResultError(vm, errorString);
}

static Value post(DictuVM *vm, int argCount, Value *args) {
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

        timeout = (long) AS_NUMBER(args[2]);
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
            /* always cleanup */
            curl_easy_cleanup(curl);
            curl_global_cleanup();
            pop(vm);

            char *errorString = (char *) curl_easy_strerror(curlResponse);
            return newResultError(vm, errorString);
        }

        return newResultSuccess(vm, OBJ_VAL(endRequest(vm, curl, response)));
    }

    /* always cleanup */
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    pop(vm);

    char *errorString = (char *) curl_easy_strerror(CURLE_FAILED_INIT);
    return newResultError(vm, errorString);
}

ObjModule *createHTTPModule(DictuVM *vm) {
    ObjString *name = copyString(vm, "HTTP", 4);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Http methods
     */
    defineNative(vm, &module->values, "get", get);
    defineNative(vm, &module->values, "post", post);

    pop(vm);
    pop(vm);

    return module;
}

#endif /* DISABLE_HTTP */

    /* 37: json/jsonParseLib.c */
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
                        (state, values_size + ((uintptr_t) value->u.object.values), 0)) )
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

    /* 38: json/jsonBuilderLib.c */

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
    /* 39: json.c */

static Value parseJson(DictuVM *vm, json_value *json) {
    switch (json->type) {
        case json_none:
        case json_null: {
            return NIL_VAL;
        }

        case json_object: {
            ObjDict *dict = newDict(vm);
            // Push value to stack to avoid GC
            push(vm, OBJ_VAL(dict));

            for (unsigned int i = 0; i < json->u.object.length; i++) {
                Value val = parseJson(vm, json->u.object.values[i].value);
                push(vm, val);
                Value key = OBJ_VAL(copyString(vm, json->u.object.values[i].name, json->u.object.values[i].name_length));
                push(vm, key);
                dictSet(vm, dict, key, val);
                pop(vm);
                pop(vm);
            }

            pop(vm);

            return OBJ_VAL(dict);
        }

        case json_array: {
            ObjList *list = newList(vm);
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
            return EMPTY_VAL;
        }
    }
}

static Value parse(DictuVM *vm, int argCount, Value *args) {
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
        return newResultError(vm, "Invalid JSON object");
    }

    Value val = parseJson(vm, json_obj);

    if (val == EMPTY_VAL) {
        return newResultError(vm, "Invalid JSON object");
    }

    json_value_free(json_obj);

    return newResultSuccess(vm, val);
}

json_value* stringifyJson(DictuVM *vm, Value value) {
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
                    json_array_push(json, stringifyJson(vm, list->values.values[i]));
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
                            stringifyJson(vm, entry->value)
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

static Value stringify(DictuVM *vm, int argCount, Value *args) {
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

    json_value *json = stringifyJson(vm, args[0]);

    if (json == NULL) {
        return newResultError(vm, "Object is not serializable");
    }

    json_serialize_opts default_opts =
    {
        lineType,
        json_serialize_opt_pack_brackets,
        indent
    };


    int length = json_measure_ex(json, default_opts);
    char *buf = ALLOCATE(vm, char, length);
    json_serialize_ex(buf, json, default_opts);
    int actualLength = strlen(buf);

    // json_measure_ex can produce a length larger than the actual string returned
    // so we need to cater for this case
    if (actualLength != length) {
        buf = SHRINK_ARRAY(vm, buf, char, length, actualLength + 1);
    }

    ObjString *string = takeString(vm, buf, actualLength);
    json_builder_free(json);

    return newResultSuccess(vm, OBJ_VAL(string));
}

ObjModule *createJSONModule(DictuVM *vm) {
    ObjString *name = copyString(vm, "JSON", 4);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Json methods
     */
    defineNative(vm, &module->values, "parse", parse);
    defineNative(vm, &module->values, "stringify", stringify);

    pop(vm);
    pop(vm);

    return module;
}

    /* 40: math.c */

static Value averageNative(DictuVM *vm, int argCount, Value *args) {
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

static Value floorNative(DictuVM *vm, int argCount, Value *args) {
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

static Value roundNative(DictuVM *vm, int argCount, Value *args) {
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

static Value ceilNative(DictuVM *vm, int argCount, Value *args) {
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

static Value absNative(DictuVM *vm, int argCount, Value *args) {
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

static Value maxNative(DictuVM *vm, int argCount, Value *args) {
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

static Value minNative(DictuVM *vm, int argCount, Value *args) {
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

static Value sumNative(DictuVM *vm, int argCount, Value *args) {
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

static Value sqrtNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "sqrt() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "A non-number value passed to sqrt()");
        return EMPTY_VAL;
    }

    return NUMBER_VAL(sqrt(AS_NUMBER(args[0])));
}

static Value sinNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "sin() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "A non-number value passed to sin()");
        return EMPTY_VAL;
    }

    return NUMBER_VAL(sin(AS_NUMBER(args[0])));
}

static Value cosNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "cos() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "A non-number value passed to cos()");
        return EMPTY_VAL;
    }

    return NUMBER_VAL(cos(AS_NUMBER(args[0])));
}

static Value tanNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "tan() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0])) {
        runtimeError(vm, "A non-number value passed to tan()");
        return EMPTY_VAL;
    }

    return NUMBER_VAL(tan(AS_NUMBER(args[0])));
}

ObjModule *createMathsModule(DictuVM *vm) {
    ObjString *name = copyString(vm, "Math", 4);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Math methods
     */
    defineNative(vm, &module->values, "average", averageNative);
    defineNative(vm, &module->values, "floor", floorNative);
    defineNative(vm, &module->values, "round", roundNative);
    defineNative(vm, &module->values, "ceil", ceilNative);
    defineNative(vm, &module->values, "abs", absNative);
    defineNative(vm, &module->values, "max", maxNative);
    defineNative(vm, &module->values, "min", minNative);
    defineNative(vm, &module->values, "sum", sumNative);
    defineNative(vm, &module->values, "sqrt", sqrtNative);
    defineNative(vm, &module->values, "sin", sinNative);
    defineNative(vm, &module->values, "cos", cosNative);
    defineNative(vm, &module->values, "tan", tanNative);

    /**
     * Define Math properties
     */
    defineNativeProperty(vm, &module->values, "PI", NUMBER_VAL(3.14159265358979));
    defineNativeProperty(vm, &module->values, "e", NUMBER_VAL(2.71828182845905));
    pop(vm);
    pop(vm);

    return module;
}

    /* 41: path.c */

#if defined(_WIN32) && !defined(S_ISDIR)
#define S_ISDIR(M) (((M) & _S_IFDIR) == _S_IFDIR)
#endif

#ifdef HAS_REALPATH
static Value realpathNative(DictuVM *vm, int argCount, Value *args) {
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
        ERROR_RESULT;
    }

    return newResultSuccess(vm, OBJ_VAL(copyString(vm, tmp, strlen (tmp))));
}
#endif

static Value isAbsoluteNative(DictuVM *vm, int argCount, Value *args) {
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

static Value basenameNative(DictuVM *vm, int argCount, Value *args) {
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

    if (!len || (len == 1 && !IS_DIR_SEPARATOR(*path))) {
        return OBJ_VAL(copyString(vm, "", 0));
    }

    char *p = path + len - 1;
    while (p > path && !IS_DIR_SEPARATOR(*(p - 1))) --p;

    return OBJ_VAL(copyString(vm, p, (len - (p - path))));
}

static Value extnameNative(DictuVM *vm, int argCount, Value *args) {
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

static Value dirnameNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "dirname() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "dirname() argument must be a string");
        return EMPTY_VAL;
    }

    ObjString *PathString = AS_STRING(args[0]);
    return OBJ_VAL(dirname(vm, PathString->chars, PathString->length));
}

static Value existsNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "exists() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "exists() argument must be a string");
        return EMPTY_VAL;
    }

    char *path = AS_CSTRING(args[0]);

    struct stat buffer;

    return BOOL_VAL(stat(path, &buffer) == 0);
}

static Value isdirNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "isdir() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "isdir() argument must be a string");
        return EMPTY_VAL;
    }

    char *path = AS_CSTRING(args[0]);
    struct stat path_stat;
    stat(path, &path_stat);

    if (S_ISDIR(path_stat.st_mode))
        return TRUE_VAL;

    return FALSE_VAL;

}

static Value listdirNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount > 1) {
        runtimeError(vm, "listdir() takes 0 or 1 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    char *path;
    if (argCount == 0) {
        path = ".";
    } else {
        if (!IS_STRING(args[0])) {
            runtimeError(vm, "listdir() argument must be a string");
            return EMPTY_VAL;
        }
        path = AS_CSTRING(args[0]);
    }

    ObjList *dir_contents = newList(vm);
    push(vm, OBJ_VAL(dir_contents));

    #ifdef _WIN32
    int length = strlen(path) + 4;
    char *searchPath = ALLOCATE(vm, char, length);
    if (searchPath == NULL) {
        runtimeError(vm, "Memory error on listdir()!");
        return EMPTY_VAL;
    }
    strcpy(searchPath, path);
    strcat(searchPath, "\\*");

    WIN32_FIND_DATAA file;
    HANDLE dir = FindFirstFile(searchPath, &file);
    if (dir == INVALID_HANDLE_VALUE) {
        runtimeError(vm, "%s is not a path!", path);
        free(searchPath);
        return EMPTY_VAL;
    }

    do {
        if (strcmp(file.cFileName, ".") == 0 || strcmp(file.cFileName, "..") == 0) {
            continue;
        }

        Value fileName = OBJ_VAL(copyString(vm, file.cFileName, strlen(file.cFileName)));
        push(vm, fileName);
        writeValueArray(vm, &dir_contents->values, fileName);
        pop(vm);
    } while (FindNextFile(dir, &file) != 0);

    FindClose(dir);
    FREE_ARRAY(vm, char, searchPath, length);
    #else
    struct dirent *dir;
    DIR *d;
    d = opendir(path);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            char *inode_name = dir->d_name;
            if (strcmp(inode_name, ".") == 0 || strcmp(inode_name, "..") == 0)
                continue;
            Value inode_value = OBJ_VAL(copyString(vm, inode_name, strlen(inode_name)));
            push(vm, inode_value);
            writeValueArray(vm, &dir_contents->values, inode_value);
            pop(vm);
        }
    } else {
        runtimeError(vm, "%s is not a path!", path);
        return EMPTY_VAL;
    }

    closedir(d);
    #endif

    pop(vm);

    return OBJ_VAL(dir_contents);
}

ObjModule *createPathModule(DictuVM *vm) {
    ObjString *name = copyString(vm, "Path", 4);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Path methods
     */
#ifdef HAS_REALPATH
    defineNative(vm, &module->values, "realpath", realpathNative);
#endif
    defineNative(vm, &module->values, "isAbsolute", isAbsoluteNative);
    defineNative(vm, &module->values, "basename", basenameNative);
    defineNative(vm, &module->values, "extname", extnameNative);
    defineNative(vm, &module->values, "dirname", dirnameNative);
    defineNative(vm, &module->values, "exists", existsNative);
    defineNative(vm, &module->values, "isdir", isdirNative);
    defineNative(vm, &module->values, "listdir", listdirNative);

    /**
     * Define Path properties
     */
    defineNativeProperty(vm, &module->values, "delimiter", OBJ_VAL(
        copyString(vm, PATH_DELIMITER_AS_STRING, PATH_DELIMITER_STRLEN)));
    defineNativeProperty(vm, &module->values, "dirSeparator", OBJ_VAL(
        copyString(vm, DIR_SEPARATOR_AS_STRING, DIR_SEPARATOR_STRLEN)));
    pop(vm);
    pop(vm);

    return module;
}

    /* 42: process.c */

#ifdef _WIN32
static char* buildArgs(DictuVM *vm, ObjList* list, int *size) {
    // 3 for 1st arg escape + null terminator
    int length = 3;

    for (int i = 0; i < list->values.count; i++) {
        if (!IS_STRING(list->values.values[i])) {
            return NULL;
        }

        // + 1 for space
        length += AS_STRING(list->values.values[i])->length + 1;
    }

    int len = AS_STRING(list->values.values[0])->length;

    char* string = ALLOCATE(vm, char, length);
    memcpy(string, "\"", 1);
    memcpy(string + 1, AS_CSTRING(list->values.values[0]), len);
    memcpy(string + 1 + len, "\"", 1);
    memcpy(string + 2 + len, " ", 1);

    int pointer = 3 + len;
    for (int i = 1; i < list->values.count; i++) {
        len = AS_STRING(list->values.values[i])->length;
        memcpy(string + pointer, AS_CSTRING(list->values.values[i]), len);
        pointer += len;
        memcpy(string + pointer, " ", 1);
        pointer += 1;
    }
    string[pointer] = '\0';

    *size = length;
    return string;
}

static Value execute(DictuVM* vm, ObjList* argList, bool wait) {
    PROCESS_INFORMATION ProcessInfo;

    STARTUPINFO StartupInfo;

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof StartupInfo;

    int len;
    char* args = buildArgs(vm, argList, &len);

    if (CreateProcess(NULL, args,
        NULL, NULL, TRUE, 0, NULL,
        NULL, &StartupInfo, &ProcessInfo))
    {
        if (wait) {
            WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
        }
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);

        FREE_ARRAY(vm, char, args, len);
        return newResultSuccess(vm, NIL_VAL);
    }

    return newResultError(vm, "Unable to start process");
}

static Value executeReturnOutput(DictuVM* vm, ObjList* argList) {
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFO StartupInfo;

    HANDLE childOutRead = NULL;
    HANDLE childOutWrite = NULL;
    SECURITY_ATTRIBUTES saAttr;

    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = true;
    saAttr.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&childOutRead, &childOutWrite, &saAttr, 0)) {
        return newResultError(vm, "Unable to start process");
    }

    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof StartupInfo;
    StartupInfo.hStdError = childOutWrite;
    StartupInfo.hStdOutput = childOutWrite;
    StartupInfo.dwFlags |= STARTF_USESTDHANDLES;

    int len;
    char* args = buildArgs(vm, argList, &len);

    if (!CreateProcess(NULL, args,
        NULL, NULL, TRUE, 0, NULL,
        NULL, &StartupInfo, &ProcessInfo))
    {
        FREE_ARRAY(vm, char, args, len);
        return newResultError(vm, "Unable to start process2");
    }

    WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
    CloseHandle(ProcessInfo.hThread);
    CloseHandle(ProcessInfo.hProcess);
    CloseHandle(childOutWrite);
    FREE_ARRAY(vm, char, args, len);

    int dwRead;
    int size = 1024;
    char* output = ALLOCATE(vm, char, size);
    char buffer[1024];
    int total = 0;

    for (;;) {
        bool ret = ReadFile(childOutRead, buffer, 1024, &dwRead, NULL);

        if (!ret || dwRead == 0)
            break;

        if (total >= size) {
            output = GROW_ARRAY(vm, output, char, size, size * 3);
            size *= 3;
        }

        memcpy(output + total, buffer, dwRead);
        total += dwRead;
    }

    output = SHRINK_ARRAY(vm, output, char, size, total + 1);
    output[total] = '\0';

    return newResultSuccess(vm, OBJ_VAL(takeString(vm, output, total)));
}
#else
static Value execute(DictuVM* vm, ObjList* argList, bool wait) {
    char** arguments = ALLOCATE(vm, char*, argList->values.count + 1);
    for (int i = 0; i < argList->values.count; ++i) {
        if (!IS_STRING(argList->values.values[i])) {
            return newResultError(vm, "Arguments passed must all be strings");
        }

        arguments[i] = AS_CSTRING(argList->values.values[i]);
    }

    arguments[argList->values.count] = NULL;
    pid_t pid = fork();
    if (pid == 0) {
        execvp(arguments[0], arguments);
        exit(errno);
    }

    FREE_ARRAY(vm, char*, arguments, argList->values.count + 1);

    if (wait) {
        int status = 0;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && (status = WEXITSTATUS(status)) != 0) {
            ERROR_RESULT;
        }
    }

    return newResultSuccess(vm, NIL_VAL);
}

static Value executeReturnOutput(DictuVM* vm, ObjList* argList) {
    char** arguments = ALLOCATE(vm, char*, argList->values.count + 1);
    for (int i = 0; i < argList->values.count; ++i) {
        if (!IS_STRING(argList->values.values[i])) {
            return newResultError(vm, "Arguments passed must all be strings");
        }

        arguments[i] = AS_CSTRING(argList->values.values[i]);
    }

    arguments[argList->values.count] = NULL;

    int fd[2];
    pipe(fd);
    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        close(fd[1]);

        execvp(arguments[0], arguments);
        exit(errno);
    }

    FREE_ARRAY(vm, char*, arguments, argList->values.count + 1);

    close(fd[1]);

    int status = 0;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && (status = WEXITSTATUS(status)) != 0) {
        ERROR_RESULT;
    }

    int size = 1024;
    char* output = ALLOCATE(vm, char, size);
    char buffer[1024];
    int total = 0;
    int numRead;

    while ((numRead = read(fd[0], buffer, 1024)) != 0) {
        if (total >= size) {
            output = GROW_ARRAY(vm, output, char, size, size * 3);
            size *= 3;
        }

        memcpy(output + total, buffer, numRead);
        total += numRead;
    }

    output = SHRINK_ARRAY(vm, output, char, size, total + 1);
    output[total] = '\0';

    return newResultSuccess(vm, OBJ_VAL(takeString(vm, output, total)));
}
#endif

static Value execNative(DictuVM* vm, int argCount, Value* args) {
    if (argCount != 1) {
        runtimeError(vm, "exec() takes 1 argument (%d given).", argCount);
        return EMPTY_VAL;
    }

    if (!IS_LIST(args[0])) {
        runtimeError(vm, "Argument passed to exec() must be a list");
        return EMPTY_VAL;
    }

    ObjList* argList = AS_LIST(args[0]);
    return execute(vm, argList, false);
}

static Value runNative(DictuVM* vm, int argCount, Value* args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError(vm, "run() takes 1 or 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_LIST(args[0])) {
        runtimeError(vm, "Argument passed to run() must be a list");
        return EMPTY_VAL;
    }

    bool getOutput = false;

    if (argCount == 2) {
        if (!IS_BOOL(args[1])) {
            runtimeError(vm, "Optional argument passed to run() must be a boolean");
            return EMPTY_VAL;
        }

        getOutput = AS_BOOL(args[1]);
    }

    ObjList* argList = AS_LIST(args[0]);

    if (getOutput) {
        return executeReturnOutput(vm, argList);
    }

    return execute(vm, argList, true);
}

ObjModule* createProcessModule(DictuVM* vm) {
    ObjString* name = copyString(vm, "Process", 7);
    push(vm, OBJ_VAL(name));
    ObjModule* module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define process methods
     */
    defineNative(vm, &module->values, "exec", execNative);
    defineNative(vm, &module->values, "run", runNative);

    /**
     * Define process properties
     */

    pop(vm);
    pop(vm);

    return module;
}
    /* 43: random.c */

static Value randomRandom(DictuVM *vm, int argCount, Value *args)
{
    UNUSED(args);
    if (argCount > 0)
    {
        runtimeError(vm, "random() takes 0 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    int high = 1;
    int low = 0;
    double random_double = ((double)rand() * (high - low)) / (double)RAND_MAX + low;
    return NUMBER_VAL(random_double);
}

static Value randomRange(DictuVM *vm, int argCount, Value *args)
{
    if (argCount != 2)
    {
        runtimeError(vm, "range() takes 2 arguments (%0d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1]))
    {
        runtimeError(vm, "range() arguments must be numbers");
        return EMPTY_VAL;
    }

    int upper = AS_NUMBER(args[1]);
    int lower = AS_NUMBER(args[0]);
    int random_val = (rand() % (upper - lower + 1)) + lower;
    return NUMBER_VAL(random_val);
}

static Value randomSelect(DictuVM *vm, int argCount, Value *args)
{
    if (argCount == 0)
    {
        runtimeError(vm, "select() takes one argument (%0d provided)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_LIST(args[0]))
    {
        runtimeError(vm, "select() argument must be a list");
        return EMPTY_VAL;
    }

    ObjList *list = AS_LIST(args[0]);
    argCount = list->values.count;
    args = list->values.values;

    for (int i = 0; i < argCount; ++i)
    {
        Value value = args[i];
        if (!IS_NUMBER(value))
        {
            runtimeError(vm, "A non-number value passed to select()");
            return EMPTY_VAL;
        }
    }

    int index = rand() % argCount;
    return args[index];
}

ObjModule *createRandomModule(DictuVM *vm)
{
    ObjString *name = copyString(vm, "Random", 6);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    srand(time(NULL));

    /**
     * Define Random methods
     */
    defineNative(vm, &module->values, "random", randomRandom);
    defineNative(vm, &module->values, "range", randomRange);
    defineNative(vm, &module->values, "select", randomSelect);

    pop(vm);
    pop(vm);

    return module;
}
    /* 44: socket.c */


#ifdef _WIN32
#define setsockopt(S, LEVEL, OPTNAME, OPTVAL, OPTLEN) setsockopt(S, LEVEL, OPTNAME, (char*)(OPTVAL), OPTLEN)

#ifndef __MINGW32__
// Fixes deprecation warning
unsigned long inet_addr_new(const char* cp) {
    unsigned long S_addr;
    inet_pton(AF_INET, cp, &S_addr);
    return S_addr;
}
#define inet_addr(cp) inet_addr_new(cp)
#endif

#define write(fd, buffer, count) _write(fd, buffer, count)
#define close(fd) closesocket(fd)
#else
#endif

typedef struct {
    int socket;
    int socketFamily;    /* Address family, e.g., AF_INET */
    int socketType;      /* Socket type, e.g., SOCK_STREAM */
    int socketProtocol;  /* Protocol type, usually 0 */
} SocketData;

#define AS_SOCKET(v) ((SocketData*)AS_ABSTRACT(v)->data)

ObjAbstract *newSocket(DictuVM *vm, int sock, int socketFamily, int socketType, int socketProtocol);

static Value createSocket(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "create() takes 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[0]) || !IS_NUMBER(args[1])) {
        runtimeError(vm, "create() arguments must be a numbers");
        return EMPTY_VAL;
    }

    int socketFamily = AS_NUMBER(args[0]);
    int socketType = AS_NUMBER(args[1]);

    int sock = socket(socketFamily, socketType, 0);
    if (sock == -1) {
        ERROR_RESULT;
    }

    ObjAbstract *s = newSocket(vm, sock, socketFamily, socketType, 0);
    return newResultSuccess(vm, OBJ_VAL(s));
}

static Value bindSocket(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "bind() takes 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "host passed to bind() must be a string");
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[2])) {
        runtimeError(vm, "port passed to bind() must be a number");
        return EMPTY_VAL;
    }

    SocketData *sock = AS_SOCKET(args[0]);
    char *host = AS_CSTRING(args[1]);
    int port = AS_NUMBER(args[2]);

    struct sockaddr_in server;

    server.sin_family = sock->socketFamily;
    server.sin_addr.s_addr = inet_addr(host);
    server.sin_port = htons(port);

    if (bind(sock->socket, (struct sockaddr *)&server , sizeof(server)) < 0) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NIL_VAL);
}

static Value listenSocket(DictuVM *vm, int argCount, Value *args) {
    if (argCount > 1) {
        runtimeError(vm, "listen() takes 0 or 1 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    int backlog = SOMAXCONN;

    if (argCount == 1) {
        if (!IS_NUMBER(args[1])) {
            runtimeError(vm, "listen() argument must be a number");
            return EMPTY_VAL;
        }

        backlog = AS_NUMBER(args[1]);
    }

    SocketData *sock = AS_SOCKET(args[0]);
    if (listen(sock->socket, backlog) == -1) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NIL_VAL);
}

static Value acceptSocket(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "accept() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    SocketData *sock = AS_SOCKET(args[0]);

    struct sockaddr_in client;
    int c = sizeof(struct sockaddr_in);
    int newSockId = accept(sock->socket, (struct sockaddr *)&client, (socklen_t*)&c);

    if (newSockId < 0) {
        ERROR_RESULT;
    }

    ObjList *list = newList(vm);
    push(vm, OBJ_VAL(list));

    ObjAbstract *newSock = newSocket(vm, newSockId, sock->socketFamily, sock->socketProtocol, 0);

    push(vm, OBJ_VAL(newSock));
    writeValueArray(vm, &list->values, OBJ_VAL(newSock));
    pop(vm);

    // IPv6 is 39 chars
    char ip[40];
    inet_ntop(sock->socketFamily, &client.sin_addr, ip, 40);
    ObjString *string = copyString(vm, ip, strlen(ip));

    push(vm, OBJ_VAL(string));
    writeValueArray(vm, &list->values, OBJ_VAL(string));
    pop(vm);

    pop(vm);

    return newResultSuccess(vm, OBJ_VAL(list));
}

static Value writeSocket(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "write() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "write() argument must be a string");
        return EMPTY_VAL;
    }

    SocketData *sock = AS_SOCKET(args[0]);
    ObjString *message = AS_STRING(args[1]);

    int writeRet = write(sock->socket , message->chars, message->length);

    if (writeRet == -1) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NUMBER_VAL(writeRet));
}

static Value recvSocket(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "recv() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[1])) {
        runtimeError(vm, "recv() argument must be a number");
        return EMPTY_VAL;
    }

    SocketData *sock = AS_SOCKET(args[0]);
    int bufferSize = AS_NUMBER(args[1]) + 1;

    if (bufferSize < 1) {
        runtimeError(vm, "recv() argument must be greater than 1");
        return EMPTY_VAL;
    }

    char *buffer = ALLOCATE(vm, char, bufferSize);
    int readSize = recv(sock->socket, buffer, bufferSize - 1, 0);

    if (readSize == -1) {
        FREE_ARRAY(vm, char, buffer, bufferSize);
        ERROR_RESULT;
    }

    // Resize string
    if (readSize != bufferSize) {
        buffer = SHRINK_ARRAY(vm, buffer, char, bufferSize, readSize + 1);
    }

    buffer[readSize] = '\0';
    ObjString *rString = takeString(vm, buffer, readSize);

    return newResultSuccess(vm, OBJ_VAL(rString));
}

static Value connectSocket(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "connect() takes two arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "host passed to bind() must be a string");
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[2])) {
        runtimeError(vm, "port passed to bind() must be a number");
        return EMPTY_VAL;
    }

    SocketData *sock = AS_SOCKET(args[0]);

    struct sockaddr_in server;

    server.sin_family = sock->socketFamily;
    server.sin_addr.s_addr = inet_addr(AS_CSTRING(args[1]));
    server.sin_port = htons(AS_NUMBER(args[2]));

    if (connect(sock->socket, (struct sockaddr *)&server, sizeof(server)) < 0) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NIL_VAL);
}

static Value closeSocket(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "close() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    SocketData *sock = AS_SOCKET(args[0]);
    close(sock->socket);

    return NIL_VAL;
}

static Value setSocketOpt(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "setsocketopt() takes 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_NUMBER(args[1]) || !IS_NUMBER(args[2])) {
        runtimeError(vm, "setsocketopt() arguments must be numbers");
        return EMPTY_VAL;
    }

    SocketData *sock = AS_SOCKET(args[0]);
    int level = AS_NUMBER(args[1]);
    int option = AS_NUMBER(args[2]);

    if (setsockopt(sock->socket, level, option, &(int){1}, sizeof(int)) == -1) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NIL_VAL);
}

void freeSocket(DictuVM *vm, ObjAbstract *abstract) {
    FREE(vm, SocketData, abstract->data);
}

ObjAbstract *newSocket(DictuVM *vm, int sock, int socketFamily, int socketType, int socketProtocol) {
    ObjAbstract *abstract = newAbstract(vm, freeSocket);
    push(vm, OBJ_VAL(abstract));

    SocketData *socket = ALLOCATE(vm, SocketData, 1);
    socket->socket = sock;
    socket->socketFamily = socketFamily;
    socket->socketType = socketType;
    socket->socketProtocol = socketProtocol;

    abstract->data = socket;

    /**
     * Setup Socket object methods
     */
    defineNative(vm, &abstract->values, "bind", bindSocket);
    defineNative(vm, &abstract->values, "listen", listenSocket);
    defineNative(vm, &abstract->values, "accept", acceptSocket);
    defineNative(vm, &abstract->values, "write", writeSocket);
    defineNative(vm, &abstract->values, "recv", recvSocket);
    defineNative(vm, &abstract->values, "connect", connectSocket);
    defineNative(vm, &abstract->values, "close", closeSocket);
    defineNative(vm, &abstract->values, "setsockopt", setSocketOpt);
    pop(vm);

    return abstract;
}

#ifdef _WIN32
void cleanupSockets(void) {
    // Calls WSACleanup until an error occurs.
    // Avoids issues if WSAStartup is called multiple times.
    while (!WSACleanup());
}
#endif

ObjModule *createSocketModule(DictuVM *vm) {
    #ifdef _WIN32
    #include "windowsapi.h"

    atexit(cleanupSockets);
    WORD versionWanted = MAKEWORD(2, 2);
    WSADATA wsaData;
    WSAStartup(versionWanted, &wsaData);
    #endif

    ObjString *name = copyString(vm, "Socket", 6);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Socket methods
     */
    defineNative(vm, &module->values, "create", createSocket);

    /**
     * Define Socket properties
     */
    defineNativeProperty(vm, &module->values, "AF_INET", NUMBER_VAL(AF_INET));
    defineNativeProperty(vm, &module->values, "SOCK_STREAM", NUMBER_VAL(SOCK_STREAM));
    defineNativeProperty(vm, &module->values, "SOL_SOCKET", NUMBER_VAL(SOL_SOCKET));
    defineNativeProperty(vm, &module->values, "SO_REUSEADDR", NUMBER_VAL(SO_REUSEADDR));

    pop(vm);
    pop(vm);

    return module;
}

#ifndef DISABLE_SQLITE

    /* 45: sqlite.c */

typedef struct {
    sqlite3 *db;
    bool open;
} Database;

typedef struct {
    sqlite3 *db;
    sqlite3_stmt *stmt;
} Result;

#define AS_SQLITE_DATABASE(v) ((Database*)AS_ABSTRACT(v)->data)

ObjAbstract *newSqlite(DictuVM *vm);

static int countParameters(char *query) {
    int length = strlen(query);
    int count = 0;

    for (int i = 0; i < length; ++i) {
        if (query[i] == '?') count++;
    }

    return count;
}

void bindValue(sqlite3_stmt *stmt, int index, Value value) {
    if (IS_NUMBER(value)) {
        sqlite3_bind_double(stmt, index, AS_NUMBER(value));
        return;
    }

    if (IS_NIL(value)) {
        sqlite3_bind_null(stmt, index);
        return;
    }

    if (IS_STRING(value)) {
        ObjString *string = AS_STRING(value);
        sqlite3_bind_text(stmt, index, string->chars, string->length, SQLITE_TRANSIENT);
    }
}

static Value sqlite_execute(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1 && argCount != 2) {
        runtimeError(vm, "sqlite_execute() takes 1 or 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[1])) {
        runtimeError(vm, "sqlite_execute() first argument must be a string.");
        return EMPTY_VAL;
    }

    Database *db = AS_SQLITE_DATABASE(args[0]);

    if (!db->open) {
        return newResultError(vm, "Database connection is closed");
    }

    char *sql = AS_CSTRING(args[1]);
    ObjList *list = NULL;
    int parameterCount = countParameters(sql);
    int argumentCount = 0;

    if (argCount == 2) {
        if (!IS_LIST(args[2])) {
            runtimeError(vm, "sqlite_execute() second argument must be a list.");
            return EMPTY_VAL;
        }

        list = AS_LIST(args[2]);
        argumentCount = list->values.count;
    }

    if (parameterCount != argumentCount) {
        runtimeError(vm, "sqlite_execute() has %d parameters but %d were given", parameterCount, argumentCount);
        return EMPTY_VAL;
    }

    Result result;

    int err = sqlite3_prepare_v2(db->db, sql, -1, &result.stmt, NULL);
    if (err != SQLITE_OK) {
        char *error = (char *)sqlite3_errmsg(db->db);
        return newResultError(vm, error);
    }

    if (parameterCount != 0 && list != NULL) {
        for (int i = 0; i < parameterCount; ++i) {
            bindValue(result.stmt, i + 1, list->values.values[i]);
        }
    }

    ObjList *finalList = newList(vm);
    push(vm, OBJ_VAL(finalList));
    bool returnValue = false;

    for (;;) {
        err = sqlite3_step(result.stmt);
        if (err != SQLITE_ROW) {
            if (err == SQLITE_DONE) {
                if (sql[0] == 'S' || sql[0] == 's') {
                    // If a select statement returns no results SQLITE_ROW is not used.
                    returnValue = true;
                }

                break;
            }

            sqlite3_finalize(result.stmt);
            char *error = (char *)sqlite3_errmsg(db->db);
            pop(vm);
            return newResultError(vm, error);
        }

        returnValue = true;

        ObjList *rowList = newList(vm);
        push(vm, OBJ_VAL(rowList));

        for (int i = 0; i < sqlite3_column_count(result.stmt); i++) {
            switch (sqlite3_column_type(result.stmt, i)) {
                case SQLITE_NULL: {
                    writeValueArray(vm, &rowList->values, NIL_VAL);
                    break;
                }

                case SQLITE_INTEGER:
                case SQLITE_FLOAT: {
                    writeValueArray(vm, &rowList->values, NUMBER_VAL(sqlite3_column_double(result.stmt, i)));
                    break;
                }

                case SQLITE_TEXT: {
                    char *s = (char *)sqlite3_column_text(result.stmt, i);
                    ObjString *string = copyString(vm, s, strlen(s));
                    push(vm, OBJ_VAL(string));
                    writeValueArray(vm, &rowList->values, OBJ_VAL(string));
                    pop(vm);
                    break;
                }
            }
        }

        writeValueArray(vm, &finalList->values, OBJ_VAL(rowList));
        pop(vm);
    }

    sqlite3_finalize(result.stmt);
    pop(vm);

    if (returnValue) {
        return newResultSuccess(vm, OBJ_VAL(finalList));
    }

    return newResultSuccess(vm, NIL_VAL);
}

static Value closeConnection(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 0) {
        runtimeError(vm, "close() takes no arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    Database *db = AS_SQLITE_DATABASE(args[0]);

    if (db->open) {
        sqlite3_close(db->db);
        db->open = false;
    }

    return NIL_VAL;
}

static Value connectSqlite(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 1) {
        runtimeError(vm, "connect() takes 1 argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "connect() argument must be a string");
        return EMPTY_VAL;
    }

    ObjAbstract *abstract = newSqlite(vm);
    Database *db = abstract->data;
    char *name = AS_CSTRING(args[0]);

    /* Open database */
    int err = sqlite3_open(name, &db->db);

    if (err) {
        char *error = (char *)sqlite3_errmsg(db->db);
        return newResultError(vm, error);
    }

    sqlite3_stmt *res;
    err = sqlite3_prepare_v2(db->db, "PRAGMA foreign_keys = ON;", -1, &res, 0);

    if (err) {
        char *error = (char *)sqlite3_errmsg(db->db);
        return newResultError(vm, error);
    }

    sqlite3_finalize(res);

    return newResultSuccess(vm, OBJ_VAL(abstract));
}

void freeSqlite(DictuVM *vm, ObjAbstract *abstract) {
    Database *db = (Database*)abstract->data;
    if (db->open) {
        sqlite3_close(db->db);
        db->open = false;
    }
    FREE(vm, Database, abstract->data);
}

ObjAbstract *newSqlite(DictuVM *vm) {
    ObjAbstract *abstract = newAbstract(vm, freeSqlite);
    push(vm, OBJ_VAL(abstract));

    Database *db = ALLOCATE(vm, Database, 1);
    db->open = true;

    /**
     * Setup Sqlite object methods
     */
    defineNative(vm, &abstract->values, "sqlite_execute", sqlite_execute);
    defineNative(vm, &abstract->values, "close", closeConnection);

    abstract->data = db;
    pop(vm);

    return abstract;
}

ObjModule *createSqliteModule(DictuVM *vm) {
    ObjString *name = copyString(vm, "Sqlite", 6);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define Sqlite methods
     */
    defineNative(vm, &module->values, "connect", connectSqlite);

    pop(vm);
    pop(vm);

    return module;
}
#endif /* DISABLE_SQLITE */

    /* 46: system.c */

#ifdef _WIN32
#define rmdir(DIRNAME) _rmdir(DIRNAME)
#define chdir(DIRNAME) _chdir(DIRNAME)
#define getcwd(BUFFER, MAXLEN) _getcwd(BUFFER, MAXLEN)
#endif

#ifndef _WIN32
static Value getgidNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getgid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getgid());
}

static Value getegidNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getegid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getegid());
}

static Value getuidNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getuid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getuid());
}

static Value geteuidNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "geteuid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(geteuid());
}

static Value getppidNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getppid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getppid());
}

static Value getpidNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(args);

    if (argCount != 0) {
        runtimeError(vm, "getpid() doesn't take any argument (%d given)", argCount);
        return EMPTY_VAL;
    }

    return NUMBER_VAL(getpid());
}
#endif

static Value rmdirNative(DictuVM *vm, int argCount, Value *args) {
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

    if (retval < 0) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NIL_VAL);
}

static Value mkdirNative(DictuVM *vm, int argCount, Value *args) {
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

    if (retval < 0) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NIL_VAL);
}

#ifdef HAS_ACCESS
static Value accessNative(DictuVM *vm, int argCount, Value *args) {
    if (argCount != 2) {
        runtimeError(vm, "access() takes 2 arguments (%d given)", argCount);
        return EMPTY_VAL;
    }

    if (!IS_STRING(args[0])) {
        runtimeError(vm, "access() first argument must be a string");
        return EMPTY_VAL;
    }

    char *file = AS_CSTRING(args[0]);

    if (!IS_NUMBER(args[1])) {
        runtimeError(vm, "access() second argument must be a number");
        return EMPTY_VAL;
    }

    int mode = AS_NUMBER(args[1]);

    int retval = access(file, mode);

    if (retval < 0) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NIL_VAL);
}
#endif

static Value removeNative(DictuVM *vm, int argCount, Value *args) {
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

    if (retval < 0) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NIL_VAL);
}

static Value setCWDNative(DictuVM *vm, int argCount, Value *args) {
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

    if (retval < 0) {
        ERROR_RESULT;
    }

    return newResultSuccess(vm, NIL_VAL);
}

static Value getCWDNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(argCount); UNUSED(args);

    char cwd[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        return newResultSuccess(vm, OBJ_VAL(copyString(vm, cwd, strlen(cwd))));
    }

    ERROR_RESULT;
}

static Value timeNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(vm); UNUSED(argCount); UNUSED(args);

    return NUMBER_VAL((double) time(NULL));
}

static Value clockNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(vm); UNUSED(argCount); UNUSED(args);

    return NUMBER_VAL((double) clock() / CLOCKS_PER_SEC);
}

static Value collectNative(DictuVM *vm, int argCount, Value *args) {
    UNUSED(argCount); UNUSED(args);

    collectGarbage(vm);
    return NIL_VAL;
}

static Value sleepNative(DictuVM *vm, int argCount, Value *args) {
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

static Value exitNative(DictuVM *vm, int argCount, Value *args) {
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

void initArgv(DictuVM *vm, Table *table, int argc, char *argv[]) {
    ObjList *list = newList(vm);
    push(vm, OBJ_VAL(list));

    for (int i = 0; i < argc; i++) {
        Value arg = OBJ_VAL(copyString(vm, argv[i], strlen(argv[i])));
        push(vm, arg);
        writeValueArray(vm, &list->values, arg);
        pop(vm);
    }

    defineNativeProperty(vm, table, "argv", OBJ_VAL(list));
    pop(vm);
}

void initPlatform(DictuVM *vm, Table *table) {
#ifdef _WIN32
    defineNativeProperty(vm, table, "platform", OBJ_VAL(copyString(vm, "windows", 7)));
#else
    struct utsname u;
    if (-1 == uname(&u)) {
        defineNativeProperty(vm, table, "platform", OBJ_VAL(copyString(vm,
            "unknown", 7)));
        return;
    }

    u.sysname[0] = tolower(u.sysname[0]);
    defineNativeProperty(vm, table, "platform", OBJ_VAL(copyString(vm, u.sysname,
        strlen(u.sysname))));
#endif
}

void createSystemModule(DictuVM *vm, int argc, char *argv[]) {
    ObjString *name = copyString(vm, "System", 6);
    push(vm, OBJ_VAL(name));
    ObjModule *module = newModule(vm, name);
    push(vm, OBJ_VAL(module));

    /**
     * Define System methods
     */
#ifndef _WIN32
    defineNative(vm, &module->values, "getgid", getgidNative);
    defineNative(vm, &module->values, "getegid", getegidNative);
    defineNative(vm, &module->values, "getuid", getuidNative);
    defineNative(vm, &module->values, "geteuid", geteuidNative);
    defineNative(vm, &module->values, "getppid", getppidNative);
    defineNative(vm, &module->values, "getpid", getpidNative);
#endif
    defineNative(vm, &module->values, "rmdir", rmdirNative);
    defineNative(vm, &module->values, "mkdir", mkdirNative);
#ifdef HAS_ACCESS
    defineNative(vm, &module->values, "access", accessNative);
#endif
    defineNative(vm, &module->values, "remove", removeNative);
    defineNative(vm, &module->values, "setCWD", setCWDNative);
    defineNative(vm, &module->values, "getCWD", getCWDNative);
    defineNative(vm, &module->values, "time", timeNative);
    defineNative(vm, &module->values, "clock", clockNative);
    defineNative(vm, &module->values, "collect", collectNative);
    defineNative(vm, &module->values, "sleep", sleepNative);
    defineNative(vm, &module->values, "exit", exitNative);

    /**
     * Define System properties
     */
    if (!vm->repl) {
        // Set argv variable
        initArgv(vm, &module->values, argc, argv);
    }

    initPlatform(vm, &module->values);

    defineNativeProperty(vm, &module->values, "S_IRWXU", NUMBER_VAL(448));
    defineNativeProperty(vm, &module->values, "S_IRUSR", NUMBER_VAL(256));
    defineNativeProperty(vm, &module->values, "S_IWUSR", NUMBER_VAL(128));
    defineNativeProperty(vm, &module->values, "S_IXUSR", NUMBER_VAL(64));
    defineNativeProperty(vm, &module->values, "S_IRWXG", NUMBER_VAL(56));
    defineNativeProperty(vm, &module->values, "S_IRGRP", NUMBER_VAL(32));
    defineNativeProperty(vm, &module->values, "S_IWGRP", NUMBER_VAL(16));
    defineNativeProperty(vm, &module->values, "S_IXGRP", NUMBER_VAL(8));
    defineNativeProperty(vm, &module->values, "S_IRWXO", NUMBER_VAL(7));
    defineNativeProperty(vm, &module->values, "S_IROTH", NUMBER_VAL(4));
    defineNativeProperty(vm, &module->values, "S_IWOTH", NUMBER_VAL(2));
    defineNativeProperty(vm, &module->values, "S_IXOTH", NUMBER_VAL(1));
    defineNativeProperty(vm, &module->values, "S_ISUID", NUMBER_VAL(2048));
    defineNativeProperty(vm, &module->values, "S_ISGID", NUMBER_VAL(1024));
#ifdef HAS_ACCESS
    defineNativeProperty(vm, &module->values, "F_OK", NUMBER_VAL(F_OK));
    defineNativeProperty(vm, &module->values, "X_OK", NUMBER_VAL(X_OK));
    defineNativeProperty(vm, &module->values, "W_OK", NUMBER_VAL(W_OK));
    defineNativeProperty(vm, &module->values, "R_OK", NUMBER_VAL(R_OK));
#endif

    tableSet(vm, &vm->globals, name, OBJ_VAL(module));
    pop(vm);
    pop(vm);
}
