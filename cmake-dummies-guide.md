# CMake для дебилов: Полное руководство по правильной работе (Расширенная версия 2024)

## Основные ошибки дебилов и как их избежать

### 1. НЕ ССЫЛАЙСЯ НА GENERATED ФАЙЛЫ НАПРЯМУЮ!
❌ **НЕПРАВИЛЬНО:**
```cmake
include_directories(${CMAKE_SOURCE_DIR}/../../generated/build_packages/zlib-1.3.1-3-x86_64/develop/headers)
```

✅ **ПРАВИЛЬНО:**
```cmake
find_package(ZLIB REQUIRED)
target_link_libraries(my_target PRIVATE ZLIB::ZLIB)
```

### 2. Кросс-компиляция: настройка toolchain

✅ **ПРАВИЛЬНАЯ ПОСЛЕДОВАТЕЛЬНОСТЬ:**
```cmake
# 1. КОМПИЛЯТОРЫ УСТАНАВЛИВАЮТСЯ ДО project()
set(CMAKE_C_COMPILER /path/to/cross-gcc CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER /path/to/cross-g++ CACHE FILEPATH "" FORCE)

# 2. Настройка sysroot
set(CMAKE_SYSROOT /path/to/sysroot)

# 3. Режимы поиска для кросс-компиляции
set(CMAKE_FIND_ROOT_PATH /path/to/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # Программы берем с хоста
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)    # Библиотеки только из sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)    # Headers только из sysroot

# 4. Пропуск тестов компилятора (если нужно)
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

# 5. ТОЛЬКО ПОТОМ project()
project(MyProject LANGUAGES C CXX)
```

### 3. Object Libraries: правильное использование

✅ **КАК СОЗДАТЬ OBJECT LIBRARY:**
```cmake
# Создание object library
add_library(my_objects OBJECT
    source1.cpp
    source2.cpp
    source3.cpp
)

# Настройка target'а (ПОСЛЕ создания!)
target_compile_definitions(my_objects PRIVATE MY_DEFINE=1)
target_include_directories(my_objects PRIVATE ${SOME_INCLUDES})
target_link_libraries(my_objects PRIVATE some_lib)
```

✅ **КАК ИСПОЛЬЗОВАТЬ OBJECT FILES:**
```cmake
# В другой library/executable
add_library(final_lib STATIC $<TARGET_OBJECTS:my_objects>)
add_executable(my_exe $<TARGET_OBJECTS:my_objects>)
```

### 4. Custom Commands для объединения объектов

✅ **ПРАВИЛЬНЫЙ СПОСОБ ОБЪЕДИНИТЬ OBJECT FILES:**
```cmake
# Создаем merged object file
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/merged.o
    COMMAND ${CMAKE_LINKER} -r $<TARGET_OBJECTS:my_objects> -o ${CMAKE_BINARY_DIR}/merged.o
    DEPENDS my_objects
    COMMENT "Merging object files"
    VERBATIM
)

# Создаем target который зависит от merged object
add_custom_target(merged_objects ALL DEPENDS ${CMAKE_BINARY_DIR}/merged.o)
```

### 5. Generator Expressions: когда и как использовать

✅ **ОСНОВНЫЕ ПРАВИЛА:**
- `$<TARGET_OBJECTS:tgt>` - список всех object files от target'а
- `$<TARGET_FILE:tgt>` - путь к итоговому файлу target'а
- Используй VERBATIM в custom commands
- Заключай в кавычки если есть пробелы

✅ **ПРИМЕРЫ:**
```cmake
# Правильное использование в custom command
add_custom_command(
    OUTPUT result.txt
    COMMAND some_tool "$<TARGET_FILE:my_exe>" --output result.txt
    DEPENDS my_exe
    VERBATIM
)

# Условная компиляция
target_compile_definitions(my_target PRIVATE
    $<$<CONFIG:Debug>:DEBUG_MODE=1>
    $<$<CONFIG:Release>:NDEBUG>
)
```

### 6. Поиск зависимостей: современный подход

✅ **ИСПОЛЬЗУЙ СОВРЕМЕННЫЕ CMAKE МОДУЛИ:**
```cmake
# Поиск пакетов
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ICU REQUIRED icu-uc icu-io)

# Линковка с target'ами (НЕ с переменными!)
target_link_libraries(my_target PRIVATE 
    ZLIB::ZLIB           # Imported target
    ${ICU_LIBRARIES}     # Переменные только если нет imported target
)

# Include directories
target_include_directories(my_target PRIVATE ${ICU_INCLUDE_DIRS})
```

### 7. Что НЕ делать в CMake

❌ **ЗАПРЕЩЕНО:**
- Хардкодить абсолютные пути
- Ссылаться на generated/ директории
- Устанавливать компилятор после project()
- Использовать глобальные include_directories() везде
- Копировать файлы вручную вместо CMake install
- Использовать устаревшие переменные вместо target-based подхода

### 8. Правильная структура CMakeLists.txt

✅ **ШАБЛОН:**
```cmake
# 1. Минимальная версия
cmake_minimum_required(VERSION 3.16)

# 2. Кросс-компиляция (если нужно)
if(CMAKE_CROSSCOMPILING)
    set(CMAKE_C_COMPILER /path/to/gcc CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER /path/to/g++ CACHE FILEPATH "" FORCE)
    set(CMAKE_SYSROOT /path/to/sysroot)
    # ... остальные настройки кросс-компиляции
endif()

# 3. Project declaration
project(MyProject LANGUAGES C CXX)

# 4. Поиск зависимостей
find_package(ZLIB REQUIRED)

# 5. Создание target'ов
add_library(my_lib STATIC src1.cpp src2.cpp)

# 6. Настройка target'ов
target_include_directories(my_lib PUBLIC include/)
target_link_libraries(my_lib PRIVATE ZLIB::ZLIB)
target_compile_definitions(my_lib PRIVATE MY_DEFINE=1)

# 7. Install rules
install(TARGETS my_lib DESTINATION lib)
```

### 9. Debug и диагностика

✅ **ПОЛЕЗНЫЕ КОМАНДЫ ДЛЯ ОТЛАДКИ:**
```cmake
# Показать все свойства target'а
get_target_property(MY_INCLUDES my_target INCLUDE_DIRECTORIES)
message(STATUS "Includes: ${MY_INCLUDES}")

# Показать найденные пакеты
message(STATUS "ZLIB found: ${ZLIB_FOUND}")
message(STATUS "ZLIB libraries: ${ZLIB_LIBRARIES}")

# Verbose сборка
make VERBOSE=1
```

### 10. Продвинутые возможности CMake 3.24+

#### 10.1 FetchContent vs ExternalProject: управление зависимостями

✅ **FETCHCONTENT - ДЛЯ ИСХОДНИКОВ:**
```cmake
include(FetchContent)

# Загрузка и интеграция в build time
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE    # Быстрее клонирование
)

# Делаем доступным в том же build
FetchContent_MakeAvailable(googletest)

# Теперь можем использовать как обычные target'ы
target_link_libraries(my_test PRIVATE gtest_main)
```

✅ **EXTERNALPROJECT - ДЛЯ СЛОЖНЫХ ЗАВИСИМОСТЕЙ:**
```cmake
include(ExternalProject)

# Полный контроль над сборкой, отдельный build
ExternalProject_Add(
    custom_library
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/custom_lib
    CMAKE_ARGS 
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/deps
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCUSTOM_OPTION=ON
    BUILD_ALWAYS FALSE
    STEP_TARGETS build
)

# Создаем imported target для использования
add_library(CustomLib::CustomLib UNKNOWN IMPORTED)
set_target_properties(CustomLib::CustomLib PROPERTIES
    IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/deps/lib/libcustom.a
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_BINARY_DIR}/deps/include
)
add_dependencies(CustomLib::CustomLib custom_library)
```

**КОГДА ЧТО ИСПОЛЬЗОВАТЬ:**
- FetchContent: исходники, header-only библиотеки, простые CMake проекты
- ExternalProject: сложные системы сборки, autotools, проекты требующие особой настройки

#### 10.2 Современные Toolchain Files для кросс-компиляции

✅ **ПРАВИЛЬНЫЙ TOOLCHAIN FILE:**
```cmake
# cross_haiku.cmake - современный toolchain file
set(CMAKE_SYSTEM_NAME Haiku)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Компиляторы
set(CROSS_PREFIX /home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-)
set(CMAKE_C_COMPILER ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${CROSS_PREFIX}gcc)
set(CMAKE_LINKER ${CROSS_PREFIX}ld)
set(CMAKE_AR ${CROSS_PREFIX}ar)
set(CMAKE_RANLIB ${CROSS_PREFIX}ranlib)
set(CMAKE_STRIP ${CROSS_PREFIX}strip)

# Современный sysroot подход
set(CMAKE_SYSROOT /home/ruslan/haiku/generated/cross-tools-x86_64/x86_64-unknown-haiku)

# Поиск путей - только в sysroot
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY) 
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Флаги для Haiku
set(CMAKE_C_FLAGS_INIT "-fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fPIC -std=c++17")

# Заставить CMake доверять компилятору
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
```

#### 10.3 Generator Expressions: BUILD_INTERFACE vs INSTALL_INTERFACE

✅ **ПРАВИЛЬНОЕ ИСПОЛЬЗОВАНИЕ ИНТЕРФЕЙСОВ:**
```cmake
# Создаем library с правильными интерфейсами
add_library(MyLib SHARED src/lib.cpp)

# Разные include paths для build и install
target_include_directories(MyLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>  # Относительно CMAKE_INSTALL_PREFIX
)

# Разные определения для build и install
target_compile_definitions(MyLib PRIVATE
    $<BUILD_INTERFACE:BUILDING_MYLIB>
    $<INSTALL_INTERFACE:USING_MYLIB>
)

# Export target для других проектов
install(TARGETS MyLib 
    EXPORT MyLibTargets
    LIBRARY DESTINATION lib
    INCLUDES DESTINATION include
)

install(EXPORT MyLibTargets
    FILE MyLibTargets.cmake
    NAMESPACE MyLib::
    DESTINATION lib/cmake/MyLib
)
```

#### 10.4 Dependency Providers: кастомные поставщики зависимостей

✅ **КАСТОМНЫЕ DEPENDENCY PROVIDERS (CMake 3.24+):**
```cmake
# В CMakeLists.txt верхнего уровня
cmake_policy(SET CMP0141 NEW)  # Включить dependency providers

# Устанавливаем кастомный provider
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES custom_deps.cmake)
```

```cmake
# custom_deps.cmake
cmake_language(
    SET_DEPENDENCY_PROVIDER haiku_dependency_provider
    SUPPORTED_METHODS FIND_PACKAGE
)

macro(haiku_dependency_provider method)
    if(method STREQUAL "FIND_PACKAGE")
        set(pkg_name ${ARGN})
        list(GET pkg_name 0 name)
        
        # Кастомная логика для Haiku зависимостей
        if(name STREQUAL "HaikuHeaders")
            # Используем системные headers вместо find_package
            add_library(HaikuHeaders::HaikuHeaders INTERFACE IMPORTED GLOBAL)
            target_include_directories(HaikuHeaders::HaikuHeaders INTERFACE
                ${CMAKE_SYSROOT}/boot/system/develop/headers
            )
            set(HaikuHeaders_FOUND TRUE PARENT_SCOPE)
            return()
        endif()
    endif()
    
    # Fallback на стандартный поиск
    cmake_language(DEFER CALL find_package ${ARGN})
endmacro()
```

#### 10.5 Presets и конфигурация проекта (CMake 3.19+)

✅ **CMAKELISTS.JSON PRESETS:**
```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "haiku-cross",
            "displayName": "Haiku Cross Compilation", 
            "description": "Cross compile for Haiku x86_64",
            "toolchainFile": "${sourceDir}/cmake/cross_haiku.cmake",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_VERBOSE_MAKEFILE": "ON",
                "BUILD_TESTING": "OFF"
            },
            "environment": {
                "CC": "/home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-gcc",
                "CXX": "/home/ruslan/haiku/generated/cross-tools-x86_64/bin/x86_64-unknown-haiku-g++"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "haiku-build",
            "configurePreset": "haiku-cross",
            "jobs": 8
        }
    ]
}
```

Команды:
```bash
cmake --preset haiku-cross
cmake --build --preset haiku-build
```

#### 10.6 Workflow Dependencies и DEPFILE

✅ **УМНАЯ РЕГЕНЕРАЦИЯ С DEPFILE:**
```cmake
# Для custom commands с множественными зависимостями
add_custom_command(
    OUTPUT generated_header.h
    COMMAND python ${CMAKE_SOURCE_DIR}/generate.py 
            --input ${CMAKE_SOURCE_DIR}/config.txt
            --output generated_header.h  
            --depfile generated_header.d
    DEPENDS ${CMAKE_SOURCE_DIR}/generate.py
    DEPFILE generated_header.d    # CMake отслеживает доп. зависимости
    COMMENT "Generating header with smart dependencies"
    VERBATIM
)
```

#### 10.7 CPack Integration: правильная упаковка

✅ **СОВРЕМЕННАЯ УПАКОВКА:**
```cmake
# После всех target'ов
include(CPack)

set(CPACK_PACKAGE_NAME "HaikuApp")
set(CPACK_PACKAGE_VENDOR "Haiku Inc")
set(CPACK_PACKAGE_VERSION "1.0.0")

# Haiku-специфичная упаковка
if(CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    set(CPACK_GENERATOR "HPKG")
    set(CPACK_HPKG_FILE_NAME "haikuapp-${CPACK_PACKAGE_VERSION}-x86_64.hpkg")
endif()

# Компоненты для выборочной установки
install(TARGETS my_app DESTINATION bin COMPONENT Runtime)
install(FILES README.md DESTINATION . COMPONENT Documentation)
```

### 11. Интеграция с Build Systems

#### 11.1 Ninja Generator: максимальная производительность

```bash
# Использование Ninja вместо Make
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=cross_haiku.cmake ..
ninja -j$(nproc)

# С presets
cmake --preset haiku-cross -G Ninja
```

#### 11.2 Ccache интеграция

```cmake
# Автоматическое использование ccache
find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set(CMAKE_C_COMPILER_LAUNCHER ccache)
    set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
    message(STATUS "Using ccache for compilation")
endif()
```

### 12. Заключение

**ГЛАВНОЕ ПРАВИЛО:** CMake - это не Makefile! Думай в терминах target'ов, а не файлов и директорий.

**СОВРЕМЕННЫЙ ПОДХОД:**
- Используй `target_*` команды вместо глобальных
- Используй imported targets вместо переменных  
- Используй generator expressions для условной логики
- НЕ ссылайся на generated файлы напрямую
- Всегда используй VERBATIM в custom commands
- FetchContent для простых зависимостей, ExternalProject для сложных
- Toolchain files для кросс-компиляции
- Presets для стандартизации конфигурации
- BUILD_INTERFACE/INSTALL_INTERFACE для правильных публичных интерфейсов