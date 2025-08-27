# Haiku Meson Migration Strategy - Гибридный подход

## Общая стратегия

**Цель**: Миграция JAM системы сборки Haiku OS на Meson с сохранением всей функциональности и минимизацией циклических зависимостей.

**Подход**: Гибридная архитектура - базовые компоненты через HaikuCommon.py + прямые импорты для специализированных модулей + lazy loading для циклов.

## Архитектура модулей

### 1. ЦЕНТРАЛЬНАЯ ОРКЕСТРАЦИЯ (HaikuCommon.py)
**Содержит только стабильные, фундаментальные компоненты без циклических зависимостей**

```python
# /home/ruslan/haiku/build/meson/modules/HaikuCommon.py
"""
Центральная оркестрация базовых компонентов Haiku build system.
Импортирует только стабильные модули без циклических зависимостей.
"""

# Phase 1: True Foundation  
from .HelperRules import *        # Утилиты JAM (FDirName, FGristFiles, etc.)
from .MathRules import *          # Математические операции

# Phase 2: Extended Foundation
from .MiscRules import *          # Echo, Exit, GLOB и др.

# Phase 3: Configuration
from .ConfigRules import *        # SetConfigVar, AppendToConfigVar
from .CommandLineArguments import *  # ProcessCommandLineArguments

# Phase 4: Architecture (КРИТИЧНО - используется везде)
from .ArchitectureRules import *  # ArchitectureSetup, KernelArchitectureSetup
from .BuildSetup import *         # Глобальные переменные сборки

# Phase 5: Core stable rules
from .FileRules import *          # HaikuInstall, файловые операции
from .BeOSRules import *          # BeOS совместимость
from .SystemLibraryRules import * # Системные библиотеки

# НЕ импортируем циклические: BuildFeatures, BuildFeatureRules, DefaultBuildProfiles
# НЕ импортируем специализированные: PackageRules, ImageRules, KernelRules, etc.
```

### 2. ОБЪЕДИНЕННЫЕ МОДУЛИ (Рефакторинг)

#### 2.1. BuildFeatures.py (объединение 2 файлов)
```python
# /home/ruslan/haiku/build/meson/modules/BuildFeatures.py
"""
Объединение JAM BuildFeatures + BuildFeatureRules в один модуль.
Детекция возможностей системы + правила их использования.
"""

# Содержимое BuildFeatures (детекция)
class HaikuBuildFeatures:
    def detect_features(self): ...
    def get_available_features(self): ...

# Содержимое BuildFeatureRules (использование)  
class HaikuBuildFeatureRules:
    def use_build_feature_headers(self): ...
    def link_against_build_feature(self): ...

# Единый API
def get_build_features(): ...
def get_build_feature_rules(): ...
```

#### 2.2. ImageRules.py (включает CDRules)
```python
# /home/ruslan/haiku/build/meson/modules/ImageRules.py
"""
Создание системных образов Haiku + CD/DVD функциональность.
Объединение JAM ImageRules + CDRules.
"""

class HaikuImageRules:
    # Основные функции ImageRules
    def build_haiku_image(self): ...
    def haiku_image_get_system_commands(self): ...
    
    # Функции CDRules (перенесены сюда)
    def build_cd_boot_image(self): ...
    def build_anyboot_image(self): ...
    def build_mmc_image(self): ...

# Единый API для создания образов
```

### 3. СПЕЦИАЛИЗИРОВАННЫЕ МОДУЛИ (Прямые импорты)

#### 3.1. Высокоуровневые модули сборки
```python
# PackageRules.py
from .HaikuCommon import ArchitectureRules, FileRules, HelperRules
from .BuildFeatures import get_build_features  # Прямой импорт

# MainBuildRules.py  
from .HaikuCommon import ArchitectureRules, SystemLibraryRules
from .BuildFeatures import get_build_feature_rules  # Прямой импорт

# HeadersRules.py
from .HaikuCommon import ArchitectureRules, FileRules
from .BuildFeatures import use_build_feature_headers  # Прямой импорт
```

#### 3.2. Специализированные системы
```python
# BootRules.py
from .HaikuCommon import ArchitectureRules, HelperRules  # Базовое

# KernelRules.py
from .HaikuCommon import ArchitectureRules
from .BootRules import boot_ld, boot_objects  # Прямой импорт

# TestsRules.py
from .HaikuCommon import ArchitectureRules, SystemLibraryRules
```

### 4. ЦИКЛИЧЕСКИЕ ЗАВИСИМОСТИ (Lazy Loading)

#### 4.1. DefaultBuildProfiles.py (разрыв цикла)
```python
# /home/ruslan/haiku/build/meson/modules/DefaultBuildProfiles.py
from .HaikuCommon import ConfigRules

class DefaultBuildProfiles:
    def __init__(self):
        self._build_features_loaded = False
        self._build_features = None
    
    def _lazy_load_build_features(self):
        """Lazy loading для избежания циклической зависимости"""
        if not self._build_features_loaded:
            from .BuildFeatures import get_build_features
            self._build_features = get_build_features()
            self._build_features_loaded = True
        return self._build_features
    
    def get_profile_for_feature(self, feature_name):
        features = self._lazy_load_build_features()
        return features.get(feature_name)
```

## Файловая структура

```
/home/ruslan/haiku/build/meson/modules/
├── HaikuCommon.py              # Центральная оркестрация (базовые модули)
├── 
├── # Foundation modules
├── HelperRules.py              # Утилиты JAM 
├── MathRules.py                # Математические операции
├── MiscRules.py                # Разные утилиты
├── ConfigRules.py              # Конфигурация сборки
├── CommandLineArguments.py     # Парсинг аргументов
├── 
├── # Architecture modules  
├── ArchitectureConfig.py       # Настройка архитектур (уже готов)
├── ArchitectureRules.py        # JAM ArchitectureRules → Python
├── BuildSetup.py              # Глобальные переменные → meson.build
├── 
├── # Core modules
├── FileRules.py                # Файловые операции
├── BeOSRules.py               # BeOS совместимость  
├── SystemLibraryRules.py      # Системные библиотеки
├── 
├── # Feature system (ОБЪЕДИНЕННЫЙ)
├── BuildFeatures.py           # BuildFeatures + BuildFeatureRules
├── DefaultBuildProfiles.py    # С lazy loading
├── 
├── # Specialized build systems
├── BootRules.py               # Загрузчик (уже готов)
├── KernelRules.py             # Ядро
├── TestsRules.py              # Тесты
├── LocaleRules.py             # Локализация
├── RepositoryRules.py         # Репозитории пакетов
├── 
├── # Package system
├── PackageRules.py            # Пакеты (уже готов)
├── HaikuPackages.py           # Определения пакетов Haiku
├── OptionalPackages.py        # Опциональные пакеты
├── 
├── # High-level build
├── MainBuildRules.py          # Основные правила сборки
├── OverriddenJamRules.py      # Переопределение JAM правил
├── ImageRules.py              # Образы системы + CDRules
├── 
├── # Utility modules
├── ResourceHandler.py         # Ресурсы (уже готов)
└── JAM_DEPENDENCY_MAP.md      # Карта зависимостей (готова)
```

## План миграции по приоритетам

### Приоритет 1: КРИТИЧНЫЕ БАЗОВЫЕ МОДУЛИ (уже готовы)
- ✅ ArchitectureConfig.py (готов)
- ✅ BootRules.py (готов) 
- ✅ PackageRules.py (готов)
- ✅ ResourceHandler.py (готов)

### Приоритет 2: ЦЕНТРАЛЬНАЯ ОРКЕСТРАЦИЯ  
1. **HaikuCommon.py** - создать центральную оркестрацию
2. **HelperRules.py** - базовые JAM утилиты
3. **ConfigRules.py** - конфигурация сборки
4. **ArchitectureRules.py** - использовать готовый ArchitectureConfig.py

### Приоритет 3: ОБЪЕДИНЕННЫЕ МОДУЛИ
1. **BuildFeatures.py** - объединить BuildFeatures + BuildFeatureRules
2. **ImageRules.py** - объединить ImageRules + CDRules  
3. **DefaultBuildProfiles.py** - с lazy loading

### Приоритет 4: СПЕЦИАЛИЗИРОВАННЫЕ МОДУЛИ
1. **MainBuildRules.py** - основные правила сборки
2. **KernelRules.py** - правила ядра
3. **HeadersRules.py** - заголовки
4. **FileRules.py** - файловые операции

### Приоритет 5: ВЫСОКОУРОВНЕВЫЕ МОДУЛИ  
1. **HaikuPackages.py** - пакеты Haiku
2. **TestsRules.py** - система тестов
3. **OverriddenJamRules.py** - переопределения

## Принципы импорта

### ✅ DO (Делать)
```python
# 1. Базовые компоненты из HaikuCommon
from HaikuCommon import ArchitectureRules, HelperRules

# 2. Прямые импорты специализированных модулей
from .PackageRules import HaikuPackageRules

# 3. Lazy loading для циклов
def _lazy_import():
    from .SomeModule import some_function
    return some_function
```

### ❌ DON'T (Не делать)
```python
# 1. НЕ импортировать ВСЁ из HaikuCommon
from HaikuCommon import *  # Плохо - циклы неизбежны

# 2. НЕ создавать прямые циклические импорты
from .ModuleA import something
# в ModuleA: from .ModuleB import something  # Цикл!

# 3. НЕ дублировать базовые импорты везде
from .HelperRules import FDirName  # Используй HaikuCommon
```

## Разрешение циклических зависимостей

### Проблемный треугольник:
```
DefaultBuildProfiles → BuildFeatureRules → BuildFeatures → DefaultBuildProfiles
```

### Решение:
1. **Объединить** BuildFeatures + BuildFeatureRules в один модуль
2. **Lazy loading** в DefaultBuildProfiles для BuildFeatures
3. **Прямые импорты** где это безопасно

## Преимущества гибридного подхода

1. **Производительность** - загружается только необходимое
2. **Ясность** - явно видно, откуда что импортируется  
3. **Отладка** - легче найти источник проблем
4. **Масштабируемость** - легко добавлять новые модули
5. **Совместимость** - сохраняется вся функциональность JAM

## Заключение

Гибридный подход обеспечивает:
- **Стабильную основу** через HaikuCommon.py
- **Гибкость** через прямые импорты
- **Решение циклов** через lazy loading и рефакторинг
- **Полную совместимость** с существующей JAM системой

Этот план обеспечивает плавную миграцию с минимальными рисками и максимальным сохранением функциональности.