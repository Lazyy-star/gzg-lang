#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define ALLOCATE_OBJ(type, objectType) (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;
    object->marked = false;
    object->next = vm.objects;
    vm.objects = object;
    return object;
}

/* ---------------- UTF-8 ---------------- */

int utf8CharLen(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;   /* 非法字节按单字节处理 */
}

int utf8Count(const char* s, int len) {
    int n = 0;
    for (int i = 0; i < len; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) n++;
    return n;
}

int utf8Offset(ObjString* s, int charIndex) {
    if (s->charCount == s->length) return charIndex;
    int off = 0;
    for (int i = 0; i < charIndex && off < s->length; i++) off += utf8CharLen((unsigned char)s->chars[off]);
    return off;
}

/* ---------------- 字符串 ---------------- */

/* 每次处理 8 字节的乘法-异或哈希：比逐字节的 FNV-1a 快数倍，长字符串拼接时差别明显 */
static uint32_t hashString(const char* key, int length) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ (uint64_t)length;
    int i = 0;
    for (; i + 8 <= length; i += 8) {
        uint64_t w;
        memcpy(&w, key + i, 8);
        h = (h ^ w) * 0xff51afd7ed558ccdULL;
        h ^= h >> 32;
    }
    uint64_t tail = 0;
    for (int k = 0; i < length; i++, k += 8) tail |= (uint64_t)(uint8_t)key[i] << k;
    h = (h ^ tail) * 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 29;
    return (uint32_t)h;
}

static ObjString* allocRaw(int length) {
    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString) + length + 1, OBJ_STRING);
    string->length = length;
    string->chars[length] = '\0';
    return string;
}

ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);
    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) return interned;
    ObjString* string = allocRaw(length);
    memcpy(string->chars, chars, length);
    string->hash = hash;
    string->charCount = utf8Count(chars, length);
    push(OBJ_VAL(string));   /* 插入驻留表时可能触发 GC */
    tableSet(&vm.strings, string, NIL_VAL);
    pop();
    return string;
}

/* 拼接：直接在新对象里构造，只拷贝一次、只哈希一次。若已有相同内容的驻留字符串则丢弃新对象。 */
ObjString* concatStrings(ObjString* a, ObjString* b) {
    int length = a->length + b->length;
    ObjString* string = allocRaw(length);   /* 可能触发 GC：调用者须保证 a、b 仍在栈上 */
    memcpy(string->chars, a->chars, a->length);
    memcpy(string->chars + a->length, b->chars, b->length);
    string->hash = hashString(string->chars, length);
    ObjString* interned = tableFindString(&vm.strings, string->chars, length, string->hash);
    if (interned != NULL) return interned;   /* 新对象没有被引用，下次 GC 回收 */
    string->charCount = a->charCount + b->charCount;
    push(OBJ_VAL(string));
    tableSet(&vm.strings, string, NIL_VAL);
    pop();
    return string;
}

ObjString* takeString(char* chars, int length) {
    ObjString* s = copyString(chars, length);
    free(chars);
    return s;
}

ObjString* cstr(const char* s) { return copyString(s, (int)strlen(s)); }

/* ---------------- 函数与闭包 ---------------- */

ObjFunction* newFunction(void) {
    ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
    function->arity = 0;
    function->minArity = 0;
    function->upvalueCount = 0;
    function->isMethod = false;
    function->name = NULL;
    function->file = NULL;
    initChunk(&function->chunk);
    initValueArray(&function->params);
    return function;
}

ObjNative* newNative(NativeFn fn, const char* name, int minArgs, int maxArgs) {
    ObjNative* native = ALLOCATE_OBJ(ObjNative, OBJ_NATIVE);
    native->fn = fn;
    native->name = name;
    native->minArgs = minArgs;
    native->maxArgs = maxArgs;
    native->isMethod = false;
    return native;
}

ObjClosure* newClosure(ObjFunction* function) {
    ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
    for (int i = 0; i < function->upvalueCount; i++) upvalues[i] = NULL;
    ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
    closure->function = function;
    closure->upvalues = upvalues;
    closure->upvalueCount = function->upvalueCount;
    return closure;
}

ObjUpvalue* newUpvalue(Value* slot) {
    ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
    upvalue->closed = NIL_VAL;
    upvalue->location = slot;
    upvalue->next = NULL;
    return upvalue;
}

/* ---------------- 容器 ---------------- */

ObjArray* newArray(void) {
    ObjArray* array = ALLOCATE_OBJ(ObjArray, OBJ_ARRAY);
    initValueArray(&array->items);
    return array;
}

ObjMap* newMap(void) {
    ObjMap* map = ALLOCATE_OBJ(ObjMap, OBJ_MAP);
    map->count = 0;
    map->entryCount = 0;
    map->entryCap = 0;
    map->entries = NULL;
    map->indexCap = 0;
    map->index = NULL;
    return map;
}

ObjStruct* newStruct(ObjString* name) {
    ObjStruct* s = ALLOCATE_OBJ(ObjStruct, OBJ_STRUCT);
    s->name = name;
    s->fieldCount = 0;
    s->fieldNames = NULL;
    s->defaults = NULL;
    initTable(&s->fieldIndex);
    initTable(&s->methods);
    return s;
}

ObjInstance* newInstance(ObjStruct* type) {
    ObjInstance* inst = (ObjInstance*)allocateObject(
        sizeof(ObjInstance) + sizeof(Value) * type->fieldCount, OBJ_INSTANCE);
    inst->type = type;
    inst->fieldCount = type->fieldCount;
    for (int i = 0; i < type->fieldCount; i++) inst->fields[i] = NIL_VAL;
    return inst;
}

ObjBound* newBound(Value receiver, Value method) {
    ObjBound* b = ALLOCATE_OBJ(ObjBound, OBJ_BOUND);
    b->receiver = receiver;
    b->method = method;
    return b;
}

ObjRange* newRange(int64_t start, int64_t end, int64_t step) {
    ObjRange* r = ALLOCATE_OBJ(ObjRange, OBJ_RANGE);
    r->start = start;
    r->end = end;
    r->step = step;
    return r;
}

int64_t rangeLength(ObjRange* r) {
    if (r->step > 0) return r->end > r->start ? (r->end - r->start + r->step - 1) / r->step : 0;
    if (r->step < 0) return r->start > r->end ? (r->start - r->end - r->step - 1) / (-r->step) : 0;
    return 0;
}

/* ---------------- 有序字典 ---------------- */

static inline bool keyEqual(Value a, Value b) {
    if (IS_OBJ(a) && IS_OBJ(b)) return AS_OBJ(a) == AS_OBJ(b);   /* 只有字符串能做键，已驻留 */
    return valuesEqual(a, b);
}

/* 返回 index 数组中的槽位：命中时为该槽，未命中时为可插入的槽 */
static int findSlot(ObjMap* map, Value key, uint32_t hash, bool* found) {
    uint32_t mask = (uint32_t)map->indexCap - 1;
    uint32_t i = hash & mask;
    int tomb = -1;
    for (;;) {
        int32_t s = map->index[i];
        if (s == 0) {
            *found = false;
            return tomb >= 0 ? tomb : (int)i;
        }
        if (s < 0) {
            if (tomb < 0) tomb = (int)i;
        } else {
            MapEntry* e = &map->entries[s - 1];
            if (e->hash == hash && keyEqual(e->key, key)) {
                *found = true;
                return (int)i;
            }
        }
        i = (i + 1) & mask;
    }
}

static void rebuildIndex(ObjMap* map, int newIndexCap) {
    int32_t* index = ALLOCATE(int32_t, newIndexCap);
    for (int i = 0; i < newIndexCap; i++) index[i] = 0;
    FREE_ARRAY(int32_t, map->index, map->indexCap);
    map->index = index;
    map->indexCap = newIndexCap;
    uint32_t mask = (uint32_t)newIndexCap - 1;
    for (int e = 0; e < map->entryCount; e++) {
        if (!map->entries[e].live) continue;
        uint32_t i = map->entries[e].hash & mask;
        while (index[i] != 0) i = (i + 1) & mask;
        index[i] = e + 1;
    }
}

bool mapGet(ObjMap* map, Value key, Value* out) {
    if (map->count == 0) return false;
    bool found;
    int slot = findSlot(map, key, hashValue(key), &found);
    if (!found) return false;
    *out = map->entries[map->index[slot] - 1].value;
    return true;
}

void mapSet(ObjMap* map, Value key, Value value) {
    uint32_t hash = hashValue(key);
    bool found = false;
    if (map->indexCap > 0) {
        int slot = findSlot(map, key, hash, &found);
        if (found) {
            map->entries[map->index[slot] - 1].value = value;
            return;
        }
    }
    if (map->entryCount == map->entryCap) {
        if (map->count < map->entryCount * 3 / 4) {
            /* 已删除的条目较多：原地压缩 */
            int w = 0;
            for (int r = 0; r < map->entryCount; r++)
                if (map->entries[r].live) map->entries[w++] = map->entries[r];
            map->entryCount = w;
        } else {
            int oldCap = map->entryCap;
            map->entryCap = GROW_CAPACITY(oldCap);
            map->entries = GROW_ARRAY(MapEntry, map->entries, oldCap, map->entryCap);
        }
        rebuildIndex(map, map->entryCap * 2);
    }
    MapEntry* e = &map->entries[map->entryCount];
    e->key = key;
    e->value = value;
    e->hash = hash;
    e->live = true;
    map->entryCount++;
    map->count++;
    int slot = findSlot(map, key, hash, &found);
    map->index[slot] = map->entryCount;
}

bool mapDelete(ObjMap* map, Value key) {
    if (map->count == 0) return false;
    bool found;
    int slot = findSlot(map, key, hashValue(key), &found);
    if (!found) return false;
    MapEntry* e = &map->entries[map->index[slot] - 1];
    e->live = false;
    e->key = NIL_VAL;
    e->value = NIL_VAL;
    map->index[slot] = -1;
    map->count--;
    return true;
}
