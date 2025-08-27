# Haiku JAM Build System Dependency Map

## Overview
Анализ 26 JAM файлов системы сборки Haiku OS для понимания зависимостей и правильного порядка импортов в Meson.

## Dependency Layers (ИСПРАВЛЕНО - от базового к сложному)

### Layer 1: Foundation (Основание)
**Истинно независимые модули**

1. **HelperRules** - базовые утилиты
   - DEFINES: FGristFiles, FDirName, FReverse, FURL, etc.
   - DEPENDENCIES: None
   - EXPORTS: ~30 utility functions

2. **MathRules** - математические операции
   - DEFINES: Math operations (Add, Subtract, etc.)
   - DEPENDENCIES: None  
   - EXPORTS: ~10 math functions

### Layer 2: Extended Foundation (Расширенное основание)
**Зависят только от HelperRules**

3. **MiscRules** - разные утилиты
   - DEFINES: Echo, Exit, GLOB, etc.
   - DEPENDENCIES: HelperRules ← **ИСПРАВЛЕНО!**
   - EXPORTS: ~20 utility functions

### Layer 3: Configuration (Конфигурация)  
**Используют Foundation, настраивают основные параметры**

4. **CommandLineArguments** - парсинг аргументов
   - DEFINES: ProcessCommandLineArguments
   - DEPENDENCIES: HelperRules, MiscRules ← **ИСПРАВЛЕНО!**
   - EXPORTS: Command line processing

5. **ConfigRules** - конфигурация сборки
   - DEFINES: SetConfigVar, AppendToConfigVar, etc.
   - DEPENDENCIES: HelperRules ← **ИСПРАВЛЕНО! (не MiscRules)**
   - EXPORTS: Configuration management

### Layer 4: Architecture Setup (Настройка архитектур)
**Критический слой для настройки компиляторов**

6. **ArchitectureRules** - настройка архитектур
   - DEFINES: ArchitectureSetup, KernelArchitectureSetup, MultiArch*
   - DEPENDENCIES: HelperRules, ConfigRules
   - EXPORTS: Architecture configuration, compiler flags
   - KEY VARIABLES: HAIKU_*FLAGS, TARGET_*, HAIKU_ARCH*

7. **BuildSetup** - базовая настройка сборки
   - DEFINES: Global build configuration variables
   - DEPENDENCIES: ArchitectureRules, ConfigRules, CommandLineArguments, MiscRules ← **ИСПРАВЛЕНО!**
   - EXPORTS: Build environment setup

### Layer 5: Feature Detection (Детекция возможностей) 
**ВНИМАНИЕ: ЦИКЛИЧЕСКИЕ ЗАВИСИМОСТИ!**

8. **BuildFeatures** - детекция возможностей системы
   - DEFINES: Feature detection variables  
   - DEPENDENCIES: ArchitectureRules, ConfigRules, MiscRules ← **ИСПРАВЛЕНО!**
   - EXPORTS: HAIKU_BUILD_FEATURES list

9. **BuildFeatureRules** - правила для использования возможностей
   - DEFINES: UseBuildFeatureHeaders, LinkAgainstBuildFeature, etc.
   - DEPENDENCIES: BuildFeatures, ArchitectureRules ← **ИСПРАВЛЕНО!**
   - EXPORTS: Feature utilization functions

10. **DefaultBuildProfiles** - профили сборки
    - DEFINES: Build profile definitions
    - DEPENDENCIES: BuildFeatureRules ← **ИСПРАВЛЕНО! (не ConfigRules)**
    - EXPORTS: Build profiles (release, debug, etc.)
    - ⚠️ **CIRCULAR**: DefaultBuildProfiles ↔ BuildFeatures ↔ BuildFeatureRules

### Layer 6: Core Build Rules (Основные правила сборки)
**Используют архитектуру и возможности для компиляции**

11. **FileRules** - операции с файлами
    - DEFINES: HaikuInstall, CopySetHaikuRevision, etc.
    - DEPENDENCIES: ArchitectureRules, HelperRules
    - EXPORTS: File operation rules

12. **HeadersRules** - работа с заголовками
    - DEFINES: HaikuPublicHeaders, PrivateHeaders, etc.
    - DEPENDENCIES: ArchitectureRules, FileRules, BuildFeatureRules ← **ИСПРАВЛЕНО!**
    - EXPORTS: Header management

13. **BeOSRules** - совместимость с BeOS
    - DEFINES: BeOS compatibility rules
    - DEPENDENCIES: ArchitectureRules
    - EXPORTS: BeOS-specific build rules

14. **SystemLibraryRules** - системные библиотеки
    - DEFINES: System library linking rules
    - DEPENDENCIES: ArchitectureRules, BuildFeatureRules
    - EXPORTS: System library management

### Layer 6: Specialized Build Rules (Специализированные правила)
**Для конкретных типов сборки**

15. **BootRules** - правила загрузчика
    - DEFINES: SetupBoot, BootObjects, BootLd, etc.
    - DEPENDENCIES: ArchitectureRules, HelperRules
    - EXPORTS: Boot loader build rules

16. **KernelRules** - правила ядра
    - DEFINES: KernelObjects, KernelLd, KernelAddon, etc.
    - DEPENDENCIES: ArchitectureRules, BootRules
    - EXPORTS: Kernel build rules

17. **TestsRules** - правила тестов
    - DEFINES: UnitTestLib, SimpleTest, BuildPlatformTest, etc.
    - DEPENDENCIES: ArchitectureRules, SystemLibraryRules
    - EXPORTS: Test building rules

### Layer 7: Advanced Build Features (Продвинутые возможности)
**Сложные операции сборки**

18. **LocaleRules** - локализация
    - DEFINES: PreprocessCatalog, CompileCatalog, etc.
    - DEPENDENCIES: ArchitectureRules, FileRules
    - EXPORTS: Localization rules

19. **RepositoryRules** - репозитории пакетов
    - DEFINES: BuildHaikuRepository, etc.
    - DEPENDENCIES: FileRules, HelperRules
    - EXPORTS: Package repository management

20. **CDRules** - создание CD/DVD образов
    - DEFINES: BuildCDBootImage, etc.
    - DEPENDENCIES: FileRules, ArchitectureRules
    - EXPORTS: CD/DVD creation rules

### Layer 8: Package System (Система пакетов)
**Создание и управление пакетами**

21. **PackageRules** - создание пакетов
    - DEFINES: HaikuPackage, BuildHaikuPackage, AddFilesToPackage, etc.
    - DEPENDENCIES: ArchitectureRules, FileRules, BuildFeatureRules
    - EXPORTS: Package creation and management

22. **HaikuPackages** - определение пакетов Haiku
    - DEFINES: Package definitions for Haiku components
    - DEPENDENCIES: PackageRules, BuildFeatureRules
    - EXPORTS: Standard Haiku packages

23. **OptionalPackages** - опциональные пакеты
    - DEFINES: Optional package definitions
    - DEPENDENCIES: PackageRules, HaikuPackages
    - EXPORTS: Optional package builds

### Layer 9: High-Level Build (Высокоуровневая сборка)
**Объединение всего в финальные цели**

24. **MainBuildRules** - основные правила сборки
    - DEFINES: Application, SharedLibrary, StaticLibrary, etc.
    - DEPENDENCIES: ArchitectureRules, SystemLibraryRules, BuildFeatureRules
    - EXPORTS: Primary build targets

25. **ImageRules** - создание системных образов
    - DEFINES: BuildHaikuImage, HaikuImageGetSystemCommands, etc.
    - DEPENDENCIES: PackageRules, MainBuildRules, BootRules, KernelRules
    - EXPORTS: System image assembly

26. **OverriddenJamRules** - переопределение стандартных JAM правил
    - DEFINES: Objects, Library, Main, etc. (overrides)
    - DEPENDENCIES: ArchitectureRules, MainBuildRules, BuildFeatureRules
    - EXPORTS: Enhanced JAM rule implementations

## Critical Dependencies

### Most Depended Upon (Самые критичные)
1. **ArchitectureRules** ← используется в 18+ файлах
2. **HelperRules** ← используется в 15+ файлах  
3. **ConfigRules** ← используется в 12+ файлах
4. **BuildFeatureRules** ← используется в 8+ файлах

### Most Independent (Самые независимые)
1. **HelperRules** - не зависит ни от кого
2. **MiscRules** - не зависит ни от кого
3. **MathRules** - не зависит ни от кого

### Complex Interdependencies (Сложные взаимосвязи)
- **PackageRules ↔ BuildFeatureRules** - двусторонняя зависимость
- **ImageRules → PackageRules → ArchitectureRules** - цепочка зависимостей
- **MainBuildRules ↔ OverriddenJamRules** - взаимное переопределение

## ✅ ИСПРАВЛЕННЫЙ Meson Import Order  

### Phase 1 - True Foundation
```python
from HelperRules import *     # Truly independent
from MathRules import *       # Truly independent
```

### Phase 2 - Extended Foundation
```python
from MiscRules import *       # Depends on HelperRules only
```

### Phase 3 - Configuration
```python
from ConfigRules import *     # HelperRules only
from CommandLineArguments import *  # HelperRules + MiscRules
```

### Phase 4 - Architecture
```python  
from ArchitectureRules import *     # HelperRules + ConfigRules
from BuildSetup import *           # ArchitectureRules + ConfigRules + CommandLineArguments + MiscRules
```

### Phase 5 - Features (⚠️ ЦИКЛИЧЕСКИЕ!)
```python
# ВНИМАНИЕ: Циклические зависимости! Нужен особый подход
from BuildFeatures import *        # Depends on MiscRules + ArchitectureRules + ConfigRules
from BuildFeatureRules import *    # Depends on BuildFeatures + ArchitectureRules
# DefaultBuildProfiles создает цикл - импортировать ПОСЛЕДНИМ или рефакторить
```

### Phase 5 - Core Rules
```python
from FileRules import *
from HeadersRules import *
from BeOSRules import *
from SystemLibraryRules import *
```

### Phase 6 - Specialized
```python
from BootRules import *
from KernelRules import *
from TestsRules import *
from LocaleRules import *
from RepositoryRules import *
from CDRules import *
```

### Phase 7 - Packages
```python
from PackageRules import *
from HaikuPackages import *
from OptionalPackages import *
```

### Phase 8 - High-Level
```python
from MainBuildRules import *
from OverriddenJamRules import *
from ImageRules import *
```

## Key Integration Points

### Variable Flow
```
CommandLineArguments → ConfigRules → ArchitectureRules → BuildFeatureRules → MainBuildRules
```

### Build Target Flow
```
ArchitectureRules → MainBuildRules → PackageRules → ImageRules
```

### Feature Detection Flow  
```
BuildFeatures → BuildFeatureRules → SystemLibraryRules → MainBuildRules
```

## Notes for Meson Migration

1. **ArchitectureRules is critical** - must be ported completely first
2. **BuildFeatureRules provides external dependency detection** - map to Meson dependency()
3. **MainBuildRules provides core targets** - map to Meson executable(), library()
4. **PackageRules is Haiku-specific** - needs custom Meson implementation
5. **ImageRules is the final assembly** - can use Meson custom_target()

## ⚠️ КРИТИЧЕСКИЕ ИСПРАВЛЕНИЯ ЗАВИСИМОСТЕЙ

### Обнаруженные ошибки в исходной карте:
1. **MiscRules НЕ независимый** - зависит от HelperRules
2. **ConfigRules НЕ зависит от MiscRules** - только от HelperRules
3. **CommandLineArguments зависит от MiscRules** - пропущено в карте
4. **BuildSetup зависит от MiscRules** - пропущено в карте  
5. **BuildFeatures зависит от MiscRules** - пропущено в карте
6. **HeadersRules зависит от BuildFeatureRules** - пропущено в карте
7. **DefaultBuildProfiles зависит от BuildFeatureRules** - не от ConfigRules

### Циклические зависимости обнаружены:
⚠️ **CIRCULAR DEPENDENCY TRIANGLE**:
```
DefaultBuildProfiles → BuildFeatureRules → BuildFeatures → DefaultBuildProfiles
```

**Решение для Meson**: Разорвать цикл через lazy loading или рефакторинг

This dependency map provides the foundation for systematic migration to Meson while preserving all functionality and relationships.