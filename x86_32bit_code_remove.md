# Удаление 32-битного кода из x86 kernel arch

Система: только x86-64. Сохраняем 32-битный BIOS boot для загрузки 64-битного ядра.

## Файлы на полное удаление

### `src/system/kernel/arch/x86/arch_commpage_compat.cpp`

Compat-слой commpage для 32-битных приложений. Целиком не нужен.

### `src/system/kernel/arch/x86/arch_elf_compat.cpp`

32-битный ELF загрузчик (`ELF32_COMPAT`). Целиком не нужен.

---

## Файлы с частичными правками

---

### `src/system/kernel/arch/x86/arch_cpu.cpp`

#### 1. Глобальные переменные (удалить)

```cpp
#ifndef __x86_64__
void (*gX86SwapFPUFunc)(void* oldState, const void* newState) = x86_noop_swap;
bool gHasSSE = false;
#endif
```

#### 2. `x86_init_fpu()` — удалить 32-битные проверки FPU/SSE

```cpp
#ifndef __x86_64__
	if (!x86_check_feature(IA32_FEATURE_FPU, FEATURE_COMMON)) {
		// No FPU... time to install one in your 386?
		dprintf("%s: Warning: CPU has no reported FPU.\n", __func__);
		gX86SwapFPUFunc = x86_noop_swap;
		return;
	}

	if (!x86_check_feature(IA32_FEATURE_SSE, FEATURE_COMMON)
		|| !x86_check_feature(IA32_FEATURE_FXSR, FEATURE_COMMON)) {
		dprintf("%s: CPU has no SSE... just enabling FPU.\n", __func__);
		// we don't have proper SSE support, just enable FPU
		x86_write_cr0(x86_read_cr0() & ~(CR0_FPU_EMULATION | CR0_MONITOR_FPU));
		gX86SwapFPUFunc = x86_fnsave_swap;
		return;
	}
#endif
```

```cpp
#ifndef __x86_64__
	// enable OS support for SSE
	x86_write_cr4(x86_read_cr4() | CR4_OS_FXSR | CR4_OS_XMM_EXCEPTION);
	x86_write_cr0(x86_read_cr0() & ~(CR0_FPU_EMULATION | CR0_MONITOR_FPU));

	gX86SwapFPUFunc = x86_fxsave_swap;
	gHasSSE = true;
#endif
```

#### 3. `init_tsc()` — удалить 32-битную ветку, оставить только 64-битный код без `#ifdef`

Удалить:
```cpp
#else
	if (conversionFactorNsecs >> 32 != 0) {
		// the TSC frequency is < 1 GHz, which forces us to shift the factor
		__x86_setup_system_time(conversionFactor, conversionFactorNsecs >> 16,
			true);
	} else {
		// the TSC frequency is >= 1 GHz
		__x86_setup_system_time(conversionFactor, conversionFactorNsecs, false);
	}
#endif
```

Снять обёртку `#ifdef __x86_64__` / `#endif` вокруг оставшегося 64-битного кода.

#### 4. `arch_cpu_shutdown()` — удалить APM

Удалить:
```cpp
#ifndef __x86_64__
		return apm_shutdown();
#else
```

и соответствующий `#endif`. Оставить `return B_NOT_SUPPORTED;` без условной компиляции.

---

### `src/system/kernel/arch/x86/arch_debug.cpp`

#### 1. Удалить 32-битные хелперы для аргументов функций

```cpp
#ifndef __x86_64__


static void
set_debug_argument_variable(int32 index, uint64 value)
{
	char name[8];
	snprintf(name, sizeof(name), "_arg%d", index);
	set_debug_variable(name, value);
}


template<typename Type>
static Type
read_function_argument_value(void* argument, bool& _valueKnown)
{
	Type value;
	if (debug_memcpy(B_CURRENT_TEAM, &value, argument, sizeof(Type)) == B_OK) {
		_valueKnown = true;
		return value;
	}

	_valueKnown = false;
	return 0;
}
```

#### 2. Удалить 32-битную `print_demangled_call()`

```cpp
static status_t
print_demangled_call(const char* image, const char* symbol, addr_t args,
	bool noObjectMethod, bool addDebugVariables)
{
	static const size_t kBufferSize = 256;
	char* buffer = (char*)debug_malloc(kBufferSize);
	if (buffer == NULL)
		return B_NO_MEMORY;

	bool isObjectMethod;
	const char* name = debug_demangle_symbol(symbol, buffer, kBufferSize,
		&isObjectMethod);
	if (name == NULL) {
		debug_free(buffer);
		return B_ERROR;
	}

	uint32* arg = (uint32*)args;

	if (noObjectMethod)
		isObjectMethod = false;
	if (isObjectMethod) {
		const char* lastName = strrchr(name, ':') - 1;
		int namespaceLength = lastName - name;

		uint32 argValue = 0;
		if (debug_memcpy(B_CURRENT_TEAM, &argValue, arg, 4) == B_OK) {
			kprintf("<%s> %.*s<\33[32m%#" B_PRIx32 "\33[0m>%s", image,
				namespaceLength, name, argValue, lastName);
		} else
			kprintf("<%s> %.*s<\?\?\?>%s", image, namespaceLength, name, lastName);

		if (addDebugVariables)
			set_debug_variable("_this", argValue);
		arg++;
	} else
		kprintf("<%s> %s", image, name);

	kprintf("(");

	size_t length;
	int32 type, i = 0;
	uint32 cookie = 0;
	while (debug_get_next_demangled_argument(&cookie, symbol, buffer,
			kBufferSize, &type, &length) == B_OK) {
		if (i++ > 0)
			kprintf(", ");

		// retrieve value and type identifier

		uint64 value;
		bool valueKnown = false;

		switch (type) {
			case B_INT64_TYPE:
				value = read_function_argument_value<int64>(arg, valueKnown);
				if (valueKnown)
					kprintf("int64: \33[34m%lld\33[0m", value);
				break;
			case B_INT32_TYPE:
				value = read_function_argument_value<int32>(arg, valueKnown);
				if (valueKnown)
					kprintf("int32: \33[34m%d\33[0m", (int32)value);
				break;
			case B_INT16_TYPE:
				value = read_function_argument_value<int16>(arg, valueKnown);
				if (valueKnown)
					kprintf("int16: \33[34m%d\33[0m", (int16)value);
				break;
			case B_INT8_TYPE:
				value = read_function_argument_value<int8>(arg, valueKnown);
				if (valueKnown)
					kprintf("int8: \33[34m%d\33[0m", (int8)value);
				break;
			case B_UINT64_TYPE:
				value = read_function_argument_value<uint64>(arg, valueKnown);
				if (valueKnown) {
					kprintf("uint64: \33[34m%#Lx\33[0m", value);
					if (value < 0x100000)
						kprintf(" (\33[34m%Lu\33[0m)", value);
				}
				break;
			case B_UINT32_TYPE:
				value = read_function_argument_value<uint32>(arg, valueKnown);
				if (valueKnown) {
					kprintf("uint32: \33[34m%#x\33[0m", (uint32)value);
					if (value < 0x100000)
						kprintf(" (\33[34m%u\33[0m)", (uint32)value);
				}
				break;
			case B_UINT16_TYPE:
				value = read_function_argument_value<uint16>(arg, valueKnown);
				if (valueKnown) {
					kprintf("uint16: \33[34m%#x\33[0m (\33[34m%u\33[0m)",
						(uint16)value, (uint16)value);
				}
				break;
			case B_UINT8_TYPE:
				value = read_function_argument_value<uint8>(arg, valueKnown);
				if (valueKnown) {
					kprintf("uint8: \33[34m%#x\33[0m (\33[34m%u\33[0m)",
						(uint8)value, (uint8)value);
				}
				break;
			case B_BOOL_TYPE:
				value = read_function_argument_value<uint8>(arg, valueKnown);
				if (valueKnown)
					kprintf("\33[34m%s\33[0m", value ? "true" : "false");
				break;
			default:
				if (buffer[0])
					kprintf("%s: ", buffer);

				if (length == 4) {
					value = read_function_argument_value<uint32>(arg,
						valueKnown);
					if (valueKnown) {
						if (value == 0
							&& (type == B_POINTER_TYPE || type == B_REF_TYPE))
							kprintf("NULL");
						else
							kprintf("\33[34m%#x\33[0m", (uint32)value);
					}
					break;
				}


				if (length == 8) {
					value = read_function_argument_value<uint64>(arg,
						valueKnown);
				} else
					value = (uint64)arg;

				if (valueKnown)
					kprintf("\33[34m%#Lx\33[0m", value);
				break;
		}

		if (!valueKnown)
			kprintf("???");

		if (valueKnown && type == B_STRING_TYPE) {
			if (value == 0)
				kprintf(" \33[31m\"<NULL>\"\33[0m");
			else if (debug_strlcpy(B_CURRENT_TEAM, buffer, (char*)(addr_t)value,
					kBufferSize) < B_OK) {
				kprintf(" \33[31m\"<\?\?\?>\"\33[0m");
			} else
				kprintf(" \33[36m\"%s\"\33[0m", buffer);
		}

		if (addDebugVariables)
			set_debug_argument_variable(i, value);
		arg = (uint32*)((uint8*)arg + length);
	}

	debug_free(buffer);

	kprintf(")");
	return B_OK;
}


#else	// __x86_64__
```

Снять обёртку `#else // __x86_64__` и `#endif // __x86_64__` вокруг оставшейся 64-битной версии `print_demangled_call()`.

#### 3. Удалить 32-битную `print_call()` и команду `show_call`

```cpp
#ifndef __x86_64__
static void
print_call(Thread *thread, addr_t eip, addr_t ebp, addr_t nextEbp,
	int32 argCount)
{
	const char *symbol, *image;
	addr_t baseAddress;
	bool exactMatch;
	status_t status;
	bool demangled = false;
	int32 *arg = (int32 *)(nextEbp + 8);

	status = lookup_symbol(thread, eip, &baseAddress, &symbol, &image,
		&exactMatch);

	kprintf("%08lx %08lx   ", ebp, eip);

	if (status == B_OK) {
		if (symbol != NULL) {
			if (exactMatch && (argCount == 0 || argCount == -1)) {
				status = print_demangled_call(image, symbol, (addr_t)arg,
					argCount == -1, true);
				if (status == B_OK)
					demangled = true;
			}
			if (!demangled) {
				kprintf("<%s>:%s%s", image, symbol,
					exactMatch ? "" : " (nearest)");
			}
		} else {
			kprintf("<%s@%p>:unknown + 0x%04lx", image,
				(void *)baseAddress, eip - baseAddress);
		}
	} else {
		VMArea *area = NULL;
		if (thread->team->address_space != NULL)
			area = thread->team->address_space->LookupArea(eip);
		if (area != NULL) {
			kprintf("%d:%s@%p + %#lx", area->id, area->name,
				(void *)area->Base(), eip - area->Base());
		}
	}

	if (!demangled) {
		kprintf("(");

		for (int32 i = 0; i < argCount; i++) {
			if (i > 0)
				kprintf(", ");
			kprintf("%#x", *arg);
			if (*arg > -0x10000 && *arg < 0x10000)
				kprintf(" (%d)", *arg);

			set_debug_argument_variable(i + 1, *(uint32 *)arg);
			arg++;
		}

		kprintf(")\n");
	} else
		kprintf("\n");

	set_debug_variable("_frame", nextEbp);
}


static int
show_call(int argc, char **argv)
{
	static const char* usage
		= "usage: %s [ <thread id> ] <call index> [ -<arg count> ]\n"
		"Prints a function call with parameters of the current, respectively\n"
		"the specified thread.\n"
		"  <thread id>   -  The ID of the thread for which to print the call.\n"
		"  <call index>  -  The index of the call in the stack trace.\n"
		"  <arg count>   -  The number of call arguments to print (use 'c' to\n"
		"                   force the C++ demangler to use class methods,\n"
		"                   use 'd' to disable demangling).\n";
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		kprintf(usage, argv[0]);
		return 0;
	}

	Thread *thread = NULL;
	phys_addr_t oldPageDirectory = 0;
	addr_t ebp = x86_get_stack_frame();
	int32 argCount = 0;

	if (argc >= 2 && argv[argc - 1][0] == '-') {
		if (argv[argc - 1][1] == 'c')
			argCount = -1;
		else if (argv[argc - 1][1] == 'd')
			argCount = -2;
		else
			argCount = strtoul(argv[argc - 1] + 1, NULL, 0);

		if (argCount < -2 || argCount > 16) {
			kprintf("Invalid argument count \"%d\".\n", argCount);
			return 0;
		}
		argc--;
	}

	if (argc < 2 || argc > 3) {
		kprintf(usage, argv[0]);
		return 0;
	}

	if (!setup_for_thread(argc == 3 ? argv[1] : NULL, &thread, &ebp,
			&oldPageDirectory))
		return 0;

	DebuggedThreadSetter threadSetter(thread);

	int32 callIndex = strtoul(argv[argc == 3 ? 2 : 1], NULL, 0);

	if (thread != NULL)
		kprintf("thread %d, %s\n", thread->id, thread->name);

	bool onKernelStack = true;

	for (int32 index = 0; index <= callIndex; index++) {
		onKernelStack = onKernelStack
			&& is_kernel_stack_address(thread, ebp);

		if (onKernelStack && is_iframe(thread, ebp)) {
			struct iframe *frame = (struct iframe *)ebp;

			if (index == callIndex)
				print_call(thread, frame->ip, ebp, frame->bp, argCount);

 			ebp = frame->bp;
		} else {
			addr_t eip, nextEbp;

			if (get_next_frame_debugger(ebp, &nextEbp, &eip) != B_OK) {
				kprintf("%08lx -- read fault\n", ebp);
				break;
			}

			if (eip == 0 || ebp == 0)
				break;

			if (index == callIndex)
				print_call(thread, eip, ebp, nextEbp, argCount);

			ebp = nextEbp;
		}

		if (ebp == 0)
			break;
	}

	if (oldPageDirectory != 0) {
		// switch back to the previous page directory to not cause any troubles
		x86_write_cr3(oldPageDirectory);
	}

	return 0;
}
#endif
```

#### 4. `print_iframe()` — удалить 32-битную ветку

```cpp
#else
	kprintf("%s iframe at %p (end = %p)\n", isUser ? "user" : "kernel", frame,
		isUser ? (void*)(frame + 1) : (void*)&frame->user_sp);

	kprintf(" eax %#-10x    ebx %#-10x     ecx %#-10x  edx %#x\n",
		frame->ax, frame->bx, frame->cx, frame->dx);
	kprintf(" esi %#-10x    edi %#-10x     ebp %#-10x  esp %#x\n",
		frame->si, frame->di, frame->bp, frame->sp);
	kprintf(" eip %#-10x eflags %#-10x", frame->ip, frame->flags);
	if (isUser) {
		// from user space
		kprintf("user esp %#x", frame->user_sp);
	}
	kprintf("\n");
#endif
```

Снять `#ifdef __x86_64__` / `#endif` вокруг 64-битного кода.

#### 5. `find_debug_variable()` — удалить 32-битные регистры

```cpp
#else
	CHECK_DEBUG_VARIABLE("gs", frame->gs, false);
	CHECK_DEBUG_VARIABLE("fs", frame->fs, false);
	CHECK_DEBUG_VARIABLE("es", frame->es, false);
	CHECK_DEBUG_VARIABLE("ds", frame->ds, false);
	CHECK_DEBUG_VARIABLE("cs", frame->cs, false);
	CHECK_DEBUG_VARIABLE("edi", frame->di, true);
	CHECK_DEBUG_VARIABLE("esi", frame->si, true);
	CHECK_DEBUG_VARIABLE("ebp", frame->bp, true);
	CHECK_DEBUG_VARIABLE("esp", frame->sp, true);
	CHECK_DEBUG_VARIABLE("ebx", frame->bx, true);
	CHECK_DEBUG_VARIABLE("edx", frame->dx, true);
	CHECK_DEBUG_VARIABLE("ecx", frame->cx, true);
	CHECK_DEBUG_VARIABLE("eax", frame->ax, true);
	CHECK_DEBUG_VARIABLE("orig_eax", frame->orig_eax, true);
	CHECK_DEBUG_VARIABLE("orig_edx", frame->orig_edx, true);
	CHECK_DEBUG_VARIABLE("eip", frame->ip, true);
	CHECK_DEBUG_VARIABLE("eflags", frame->flags, true);

	if (IFRAME_IS_USER(frame)) {
		CHECK_DEBUG_VARIABLE("user_esp", frame->user_sp, true);
		CHECK_DEBUG_VARIABLE("user_ss", frame->user_ss, false);
	}
#endif
```

Снять `#ifdef __x86_64__` / `#endif` вокруг 64-битного кода.

#### 6. `arch_debug_unset_current_thread()` — удалить 32-битную ветку

```cpp
#else
	asm volatile("mov %0, %%gs:0" : : "r" (unsetThread) : "memory");
#endif
```

Снять `#ifdef __x86_64__` / `#endif` вокруг `x86_write_msr(...)`.

#### 7. `arch_debug_gdb_get_registers()` — удалить 32-битный формат GDB

```cpp
#else
	// For x86 the register order is:
	//
	//    eax, ecx, edx, ebx,
	//    esp, ebp, esi, edi,
	//    eip, eflags,
	//    cs, ss, ds, es, fs, gs
	//
	// Note that even though the segment descriptors are actually 16 bits wide,
	// gdb requires them as 32 bit integers.
	static const int32 kRegisterCount = 16;
	gdb_register registers[kRegisterCount] = {
		{ B_UINT32_TYPE, frame->ax }, { B_UINT32_TYPE, frame->cx },
		{ B_UINT32_TYPE, frame->dx }, { B_UINT32_TYPE, frame->bx },
		{ B_UINT32_TYPE, frame->sp }, { B_UINT32_TYPE, frame->bp },
		{ B_UINT32_TYPE, frame->si }, { B_UINT32_TYPE, frame->di },
		{ B_UINT32_TYPE, frame->ip }, { B_UINT32_TYPE, frame->flags },
		{ B_UINT32_TYPE, frame->cs }, { B_UINT32_TYPE, frame->ds },
			// assume ss == ds
		{ B_UINT32_TYPE, frame->ds }, { B_UINT32_TYPE, frame->es },
		{ B_UINT32_TYPE, frame->fs }, { B_UINT32_TYPE, frame->gs },
	};
#endif
```

Снять `#ifdef __x86_64__` / `#endif` вокруг 64-битного кода.

#### 8. `arch_debug_init()` — удалить регистрацию команды `call`

```cpp
#ifndef __x86_64__
	add_debugger_command("call", &show_call, "Show call with arguments");
#endif
```

---

### `src/system/kernel/arch/x86/arch_elf.cpp`

#### 1. Удалить 32-битный блок REL-релокаций и RELA-заглушку

Весь блок за условием:

```cpp
#if !defined(__x86_64__) || defined(ELF32_COMPAT)	\
|| (defined(_BOOT_MODE) && defined(BOOT_SUPPORT_ELF32))
```

до соответствующего:

```cpp
#endif	// !defined(__x86_64__) || defined(ELF32_COMPAT)
```

Это включает обе функции: `arch_elf_relocate_rel` (для Elf32_Rel) и `arch_elf_relocate_rela` (заглушка "not supported on x86_32").

> **Примечание:** этот блок уже не компилируется на чистом x86_64 без `ELF32_COMPAT`, но после удаления `arch_elf_compat.cpp` он становится полностью мёртвым кодом. Удаление повышает читаемость.

---

### `src/system/kernel/arch/x86/arch_thread.cpp`

#### 1. Удалить extern на `gX86SwapFPUFunc`

```cpp
// from arch_cpu.cpp
#ifndef __x86_64__
extern void (*gX86SwapFPUFunc)(void *oldState, const void *newState);
#endif
```

#### 2. `arch_thread_context_switch()` — удалить FPU swap

```cpp
#ifndef __x86_64__
	gX86SwapFPUFunc(from->arch_info.fpu_state, to->arch_info.fpu_state);
#endif
```

---

### `src/system/kernel/arch/x86/arch_vm.cpp`

#### 1. `arch_vm_init_post_area()` — удалить `bios_init()`

```cpp
#ifndef __x86_64__
	return bios_init();
#else
	return B_OK;
#endif
```

Заменить на:
```cpp
	return B_OK;
```

---

### `src/system/kernel/arch/x86/asm_offsets.cpp`

#### 1. Удалить 32-битный `orig_eax`

```cpp
	#else
	// x86 (32-bit) specific saved registers
	DEFINE_OFFSET_MACRO(IFRAME, iframe, orig_eax);
	#endif
```

Снять `#ifdef __x86_64__` / `#endif` вокруг 64-битных макросов.

---

### `src/system/kernel/arch/x86/arch_system_info.cpp`

#### 1. `arch_fill_topology_node()` — упростить определение платформы

Удалить:
```cpp
		#if __i386__
		node->data.root.platform = B_CPU_x86;
		#elif __x86_64__
		node->data.root.platform = B_CPU_x86_64;
		#else
		node->data.root.platform = B_CPU_UNKNOWN;
		#endif
```

Заменить на:
```cpp
		node->data.root.platform = B_CPU_x86_64;
```

---

## Общие замечания

1. После удаления `#ifndef __x86_64__` блоков, все оставшиеся `#ifdef __x86_64__` обёртки в тех же функциях становятся безусловными — их тоже нужно снять, оставив только содержимое.

2. Включение `<arch/x86/bios.h>` в `arch_vm.cpp` можно убрать после удаления `bios_init()`.

3. После удаления `arch_elf_compat.cpp` из сборки, нужно убрать его из соответствующего `Jamfile` / `CMakeLists` / `xmake.lua`.

4. То же самое для `arch_commpage_compat.cpp`.

5. Общий объём удаляемого кода: ~500 строк + 2 файла целиком.
