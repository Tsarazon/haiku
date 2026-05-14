# План рефакторинга vm.cpp (v5 — финальный)

## Полная карта приватных заголовков

### headers/private/kernel/vm/
| Заголовок | Содержимое | Включает |
|-----------|------------|----------|
| `vm.h` | vm_*(), _user_*(), CREATE_AREA_*, VM_PRIORITY_* | vm_types.h |
| `vm_priv.h` | vm_page_fault(), vm_try_reserve_memory(), vm_unreserve_memory() | vm_types.h |
| `vm_types.h` | vm_page, vm_page_mapping, virtual/physical_address_restrictions | — |
| `vm_page.h` | vm_page_*(), gMappedPagesCount | vm.h, vm_types.h |
| `VMArea.h` | VMArea, VMAreas, VMAreaWiredRange, VMPageWiringInfo | vm_types.h |
| `VMCache.h` | VMCache (Lock, Unlock, AcquireRef, UserData...), VMCacheFactory | **vm.h**, VMArea.h |
| `VMAddressSpace.h` | VMAddressSpace, AreaIterator | **vm_priv.h**, VMArea.h, VMTranslationMap.h |
| `VMTranslationMap.h` | VMTranslationMap, VMPhysicalPageMapper | VMArea.h |

### headers/private/system/
| Заголовок | Содержимое |
|-----------|------------|
| `vm_defs.h` | B_SHARED_AREA, B_KERNEL_AREA, B_ALREADY_WIRED, MEMORY_TYPE_SHIFT |
| `syscalls.h` | _kern_create_area(), _kern_delete_area(), _kern_clone_area() и др. |

### headers/private/kernel/
| Заголовок | Содержимое |
|-----------|------------|
| `AreaKeeper.h` | RAII wrapper для area (Create, Map, автоудаление) |

### headers/private/shared/
| Заголовок | Содержимое |
|-----------|------------|
| `AutoDeleterOS.h` | AreaDeleter typedef |

**Ключевое:** `VMCache.h` включает `vm.h`, который включает `vm_types.h`. Один include даёт почти всё!

## Что уже доступно через приватные заголовки

Для AreaCacheLocker и VMCacheChainLocker нужны:
- ✅ `vm_area_get_locked_cache()` — vm.h
- ✅ `vm_area_put_locked_cache()` — vm.h
- ✅ `VMCache::Lock()`, `Unlock()` — VMCache.h
- ✅ `VMCache::AcquireRefLocked()`, `ReleaseRefAndUnlock()` — VMCache.h
- ✅ `VMCache::UserData()`, `SetUserData()` — VMCache.h
- ✅ `VMCache::source` — VMCache.h (публичный член)
- ✅ `AutoLocker<>` — util/AutoLock.h

**Вывод:** vm_cache_locking.h нужен только один include: `<vm/VMCache.h>`!

## Предлагаемое разделение

### Новые файлы

| Файл | Строк | Includes |
|------|-------|----------|
| `vm_cache_locking.h` | ~160 | `<vm/VMCache.h>`, `<util/AutoLock.h>` |
| `vm_syscalls.cpp` | ~500 | `<vm/vm.h>`, `<vm/VMAddressSpace.h>` |
| `vm_init.cpp` | ~500 | `<vm/vm.h>`, `<vm/vm_priv.h>`, `<vm/vm_page.h>` |
| `vm_memory_ops.cpp` | ~400 | `<vm/vm.h>`, `<vm/VMTranslationMap.h>` |

### Остаётся в vm.cpp (~5100 строк)
- Static переменные (sAreaCacheLock, sAvailableMemory, etc.)
- Static функции (delete_area, vm_soft_fault, map_backing_store, cut_area, etc.)
- Page fault handling
- Area management (vm_clone_area, vm_copy_area, etc.)
- Symbol versioning

## Детальный план

### Фаза 1: vm_cache_locking.h

```cpp
#ifndef VM_CACHE_LOCKING_H
#define VM_CACHE_LOCKING_H

#include <vm/VMCache.h>
#include <util/AutoLock.h>

class AreaCacheLocking {
public:
    inline bool Lock(VMCache* lockable) { return false; }
    inline void Unlock(VMCache* lockable) {
        vm_area_put_locked_cache(lockable);
    }
};

class AreaCacheLocker : public AutoLocker<VMCache, AreaCacheLocking> {
public:
    inline AreaCacheLocker(VMCache* cache = NULL)
        : AutoLocker<VMCache, AreaCacheLocking>(cache, true) {}
    inline AreaCacheLocker(VMArea* area)
        : AutoLocker<VMCache, AreaCacheLocking>() { SetTo(area); }
    inline void SetTo(VMCache* cache, bool alreadyLocked) {
        AutoLocker<VMCache, AreaCacheLocking>::SetTo(cache, alreadyLocked);
    }
    inline void SetTo(VMArea* area) {
        return AutoLocker<VMCache, AreaCacheLocking>::SetTo(
            area != NULL ? vm_area_get_locked_cache(area) : NULL, true, true);
    }
};

class VMCacheChainLocker {
    // ... полный код из vm.cpp строки 119-234
};

#endif // VM_CACHE_LOCKING_H
```

### Фаза 2: vm_syscalls.cpp

**Includes:**
```cpp
#include <vm/vm.h>
#include <vm/VMAddressSpace.h>
#include <vm/VMArea.h>
#include <vm/VMCache.h>

#include "VMAddressSpaceLocking.h"
#include "vm_cache_locking.h"
```

**Функции (все уже объявлены в vm.h):**
- _user_reserve_address_range()
- _user_unreserve_address_range()
- _user_area_for()
- _user_find_area()
- _user_get_area_info()
- _user_get_next_area_info()
- _user_set_area_protection()
- _user_resize_area()
- _user_transfer_area()
- _user_clone_area()
- _user_create_area()
- _user_delete_area()
- _user_map_file()
- _user_unmap_memory()
- _user_set_memory_protection()
- _user_sync_memory()
- _user_memory_advice()
- _user_get_memory_properties()
- _user_mlock()
- _user_munlock()
- user_set_memory_swappable() [static → нужно вынести или оставить]

**Public wrappers (объявлены в vm.h):**
- create_area_etc()
- transfer_area()

### Фаза 3: vm_init.cpp

**Includes:**
```cpp
#include <vm/vm.h>
#include <vm/vm_priv.h>
#include <vm/vm_page.h>
#include <vm/VMCache.h>
#include <boot/stage2.h>
```

**Функции из vm.h:**
- vm_init()
- vm_init_post_sem()
- vm_init_post_thread()
- vm_init_post_modules()
- vm_free_kernel_args()
- vm_free_unused_boot_loader_range()
- vm_allocate_early_physical_page()
- vm_allocate_early()

**Зависимости от static в vm.cpp:**
- `sPhysicalPageMapper` — нужно сделать extern или передать параметром
- `create_page_mappings_object_caches()` — нужно вынести или вызвать из vm.cpp

### Фаза 4: vm_memory_ops.cpp

**Includes:**
```cpp
#include <vm/vm.h>
#include <vm/VMTranslationMap.h>
#include <arch/user_memory.h>
```

**Функции из vm.h:**
- vm_memset_physical()
- vm_memcpy_from_physical()
- vm_memcpy_to_physical()
- vm_memcpy_physical_page()

**Функции (нужно проверить объявления):**
- user_memcpy() — возможно в другом заголовке
- user_strlcpy()
- user_memset()

**Зависимости:**
- `sPhysicalPageMapper` — используется для vm_memset_physical и др.

### Фаза 5: Jamfile

```
KernelMergeObject kernel_vm.o :
    PageCacheLocker.cpp
    vm.cpp
    vm_init.cpp          # NEW
    vm_memory_ops.cpp    # NEW
    vm_syscalls.cpp      # NEW
    vm_debug.cpp
    ...
```

## Проблемы и решения

### Проблема 1: sPhysicalPageMapper
Используется в vm_init.cpp и vm_memory_ops.cpp.

**Решение:** Добавить в vm_priv.h:
```cpp
extern VMPhysicalPageMapper* sPhysicalPageMapper;
```
И в vm.cpp убрать `static`.

### Проблема 2: create_page_mappings_object_caches()
Вызывается из vm_init().

**Решение:** Оставить в vm.cpp, вызывать через функцию-обёртку.

### Проблема 3: user_set_memory_swappable()
Static функция, используется в _user_mlock/_user_munlock.

**Решение:** Переместить вместе с syscalls в vm_syscalls.cpp.

## Порядок выполнения

1. **vm_cache_locking.h** — низкий риск
2. **vm_syscalls.cpp** — низкий риск
3. **vm_memory_ops.cpp** — средний риск (зависимость от sPhysicalPageMapper)
4. **vm_init.cpp** — средний риск (зависимости)
5. **Jamfile** — финализация

## Ожидаемый результат

| Файл | Строк |
|------|-------|
| vm.cpp | ~5100 |
| vm_cache_locking.h | ~160 |
| vm_syscalls.cpp | ~500 |
| vm_init.cpp | ~500 |
| vm_memory_ops.cpp | ~400 |
| **Итого** | ~6660 |

## Верификация

```bash
cd /home/ruslan/haiku/generated
/home/ruslan/haiku/buildtools/jam/jam0 -q -j8 @nightly-anyboot
```
