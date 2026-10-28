/* 标记-清除 GC：从根集合出发标记可达对象（三色标记，灰栈），然后清扫未标记对象。
 * 根集合：值栈、调用帧闭包、开放 upvalue、全局变量、内建方法表、编译器正在构造的函数、临时根。
 * 字符串驻留池是弱引用：清扫前把未标记的字符串从池中删除。 */
#include "memory.h"
#include "compiler.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;
    if (newSize > oldSize) {
#ifdef GC_STRESS
        collectGarbage();
#else
        if (vm.bytesAllocated > vm.nextGC) collectGarbage();
#endif
    }
    if (newSize == 0) {
        free(pointer);
        return NULL;
    }
    void* result = realloc(pointer, newSize);
    if (result == NULL) {
        fprintf(stderr, "致命错误: 内存不足\n");
        exit(EXIT_RUNTIME_ERROR);
    }
    return result;
}

void markObject(Obj* object) {
    if (object == NULL || object->marked) return;
    object->marked = true;
    if (vm.grayCapacity < vm.grayCount + 1) {
        vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
        /* 灰栈用系统 realloc，避免在 GC 过程中递归触发 GC */
        vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
        if (vm.grayStack == NULL) exit(EXIT_RUNTIME_ERROR);
    }
    vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
    if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markArray(ValueArray* array) {
    for (int i = 0; i < array->count; i++) markValue(array->values[i]);
}

static void blackenObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING:
        case OBJ_NATIVE:
        case OBJ_RANGE:
            break;
        case OBJ_FUNCTION: {
            ObjFunction* f = (ObjFunction*)object;
            markObject((Obj*)f->name);
            markObject((Obj*)f->file);
            markArray(&f->chunk.constants);
            markArray(&f->params);
            break;
        }
        case OBJ_CLOSURE: {
            ObjClosure* c = (ObjClosure*)object;
            markObject((Obj*)c->function);
            for (int i = 0; i < c->upvalueCount; i++) markObject((Obj*)c->upvalues[i]);
            break;
        }
        case OBJ_UPVALUE:
            markValue(((ObjUpvalue*)object)->closed);
            break;
        case OBJ_ARRAY:
            markArray(&((ObjArray*)object)->items);
            break;
        case OBJ_MAP: {
            ObjMap* m = (ObjMap*)object;
            for (int i = 0; i < m->entryCount; i++) {
                if (!m->entries[i].live) continue;
                markValue(m->entries[i].key);
                markValue(m->entries[i].value);
            }
            break;
        }
        case OBJ_STRUCT: {
            ObjStruct* s = (ObjStruct*)object;
            markObject((Obj*)s->name);
            for (int i = 0; i < s->fieldCount; i++) {
                markObject((Obj*)s->fieldNames[i]);
                markValue(s->defaults[i]);
            }
            markTable(&s->fieldIndex);
            markTable(&s->methods);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* inst = (ObjInstance*)object;
            markObject((Obj*)inst->type);
            for (int i = 0; i < inst->fieldCount; i++) markValue(inst->fields[i]);
            break;
        }
        case OBJ_BOUND:
            markValue(((ObjBound*)object)->receiver);
            markValue(((ObjBound*)object)->method);
            break;
    }
}

static void freeObject(Obj* object) {
    switch (object->type) {
        case OBJ_STRING: {
            ObjString* s = (ObjString*)object;
            reallocate(object, sizeof(ObjString) + s->length + 1, 0);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction* f = (ObjFunction*)object;
            freeChunk(&f->chunk);
            freeValueArray(&f->params);
            FREE(ObjFunction, object);
            break;
        }
        case OBJ_NATIVE: FREE(ObjNative, object); break;
        case OBJ_CLOSURE: {
            ObjClosure* c = (ObjClosure*)object;
            FREE_ARRAY(ObjUpvalue*, c->upvalues, c->upvalueCount);
            FREE(ObjClosure, object);
            break;
        }
        case OBJ_UPVALUE: FREE(ObjUpvalue, object); break;
        case OBJ_ARRAY:
            freeValueArray(&((ObjArray*)object)->items);
            FREE(ObjArray, object);
            break;
        case OBJ_MAP: {
            ObjMap* m = (ObjMap*)object;
            FREE_ARRAY(MapEntry, m->entries, m->entryCap);
            FREE_ARRAY(int32_t, m->index, m->indexCap);
            FREE(ObjMap, object);
            break;
        }
        case OBJ_STRUCT: {
            ObjStruct* s = (ObjStruct*)object;
            FREE_ARRAY(ObjString*, s->fieldNames, s->fieldCount);
            FREE_ARRAY(Value, s->defaults, s->fieldCount);
            freeTable(&s->fieldIndex);
            freeTable(&s->methods);
            FREE(ObjStruct, object);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance* inst = (ObjInstance*)object;
            reallocate(object, sizeof(ObjInstance) + sizeof(Value) * inst->fieldCount, 0);
            break;
        }
        case OBJ_BOUND: FREE(ObjBound, object); break;
        case OBJ_RANGE: FREE(ObjRange, object); break;
    }
}

static void markRoots(void) {
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) markValue(*slot);
    for (int i = 0; i < vm.frameCount; i++) markObject((Obj*)vm.frames[i].closure);
    for (ObjUpvalue* u = vm.openUpvalues; u != NULL; u = u->next) markObject((Obj*)u);
    markArray(&vm.globalValues);
    markArray(&vm.globalNameList);
    markTable(&vm.globalNames);
    markTable(&vm.imports);
    markTable(&vm.stringMethods);
    markTable(&vm.arrayMethods);
    markTable(&vm.mapMethods);
    markTable(&vm.rangeMethods);
    markObject((Obj*)vm.initString);
    markObject((Obj*)vm.emptyString);
    markObject((Obj*)vm.scriptDir);
    markObject((Obj*)vm.scriptArgs);
    markValue(vm.pendingError);
    for (int i = 0; i < vm.tempRootCount; i++) markValue(*vm.tempRoots[i]);
    markCompilerRoots();
}

static void traceReferences(void) {
    while (vm.grayCount > 0) {
        Obj* object = vm.grayStack[--vm.grayCount];
        blackenObject(object);
    }
}

static void sweep(void) {
    Obj* previous = NULL;
    Obj* object = vm.objects;
    while (object != NULL) {
        if (object->marked) {
            object->marked = false;
            previous = object;
            object = object->next;
        } else {
            Obj* unreached = object;
            object = object->next;
            if (previous != NULL) previous->next = object;
            else vm.objects = object;
            freeObject(unreached);
        }
    }
}

void collectGarbage(void) {
    markRoots();
    traceReferences();
    tableRemoveWhite(&vm.strings);
    sweep();
    vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
    if (vm.nextGC < 1024 * 1024) vm.nextGC = 1024 * 1024;
}

void freeObjects(void) {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;
        freeObject(object);
        object = next;
    }
    vm.objects = NULL;
    free(vm.grayStack);
    vm.grayStack = NULL;
}
