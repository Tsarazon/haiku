# Haiku Build Comparison Tools

Набор инструментов для автоматического сравнения JAM и Meson сборок компонентов Haiku OS.

## compare_builds.py

Основной скрипт для сравнения объектных файлов между JAM и Meson сборками.

### Возможности

- 📊 **Сравнение размеров файлов** с процентным отклонением
- 🔍 **Анализ символов** - публичные функции, переменные
- 📚 **Подсчет секций** ELF объектных файлов  
- 🔎 **Поиск специфичных символов** по паттернам
- 📝 **Генерация отчетов** в Markdown формате
- 💾 **Экспорт данных** в JSON формате
- 🔧 **Демангл C++ символов** для читаемости

### Использование

#### Базовое сравнение

```bash
# Сравнить Interface Kit
./compare_builds.py interface_kit "*/interface/interface_kit.o"

# Сравнить Storage Kit  
./compare_builds.py storage_kit "*/storage/storage_kit.o"

# Сравнить App Kit
./compare_builds.py app_kit "*/app/app_kit.o"
```

#### С поиском специфичных символов

```bash
# Поиск HSL/RGB функций в Interface Kit
./compare_builds.py interface_kit "*/interface/interface_kit.o" --search-symbols hsl rgb color

# Поиск функций локализации
./compare_builds.py locale_kit "*/locale/locale_kit.o" --search-symbols locale catalog translation
```

#### Сохранение отчетов

```bash
# Сохранить отчет в файл
./compare_builds.py interface_kit "*/interface/interface_kit.o" \
    --output interface_kit_report.md

# Создать и JSON и Markdown отчеты
./compare_builds.py storage_kit "*/storage/storage_kit.o" \
    --output storage_kit_report.md --json
```

#### Изменение корневой директории

```bash
# Если Haiku находится в другой директории
./compare_builds.py interface_kit "*/interface/interface_kit.o" \
    --haiku-root /path/to/haiku
```

### Примеры паттернов

| Компонент | Паттерн |
|-----------|---------|
| Interface Kit | `*/interface/interface_kit.o` |
| Storage Kit | `*/storage/storage_kit.o` |
| App Kit | `*/app/app_kit.o` |
| Support Kit | `*/support/support_kit.o` |
| Locale Kit | `*/locale/locale_kit.o` |
| libbe.so | `*/kits/be/libbe.so*` |
| libroot.so | `*/libroot/libroot.so*` |
| Отдельные файлы | `*/interface/View.o` |
| Все .o файлы кита | `*/interface/*.o` |

### Интерпретация результатов

#### Размер файлов
- **< 1% разница**: ✅ Практически идентично
- **< 5% разница**: ⚠️ Малая разница  
- **> 5% разница**: ❌ Значительная разница

#### Символы
- **Identical symbols**: ✅ Наборы функций идентичны
- **Minor differences**: ⚠️ Несколько уникальных символов
- **Major differences**: ❌ Много различающихся символов

#### Секции ELF
- **0-3 разница**: ✅ Нормальная разница
- **4-10 разница**: ⚠️ Заметная разница
- **>10 разница**: ❌ Существенная разница

### Коды выхода

- **0**: Сборки идентичны или очень похожи
- **1**: Есть заметные различия, требует проверки

## Примеры отчетов

### Успешное сравнение
```
🎉 PERFECT MATCH! Builds are essentially identical.
   Size difference: +0.14%
   Symbol count: JAM=5383, Meson=5383  
   Identical symbols: ✅ YES
```

### Есть различия
```
⚠️ Builds differ - check the report for details.
   Size difference: +2.30%
   Symbol count: JAM=5383, Meson=5390
   Identical symbols: ❌ NO
```

## Дополнительные инструменты

### Быстрое сравнение всех китов

Создайте скрипт для массового сравнения:

```bash
#!/bin/bash
# compare_all_kits.sh

KITS=(
    "interface_kit */interface/interface_kit.o"
    "storage_kit */storage/storage_kit.o" 
    "support_kit */support/support_kit.o"
    "app_kit */app/app_kit.o"
    "locale_kit */locale/locale_kit.o"
)

for kit in "${KITS[@]}"; do
    name=$(echo $kit | cut -d' ' -f1)
    pattern=$(echo $kit | cut -d' ' -f2-)
    
    echo "Comparing $name..."
    ./compare_builds.py "$name" "$pattern" --output "${name}_report.md"
    echo "---"
done
```

### Поиск проблем в компиляции

```bash
# Найти все .o файлы которые есть в JAM но нет в Meson
./compare_builds.py missing_objects "*.o" 2>&1 | grep "No Meson objects"
```

## Устранение неполадок

### "No JAM objects found"
- Убедитесь что JAM сборка выполнена: `jam -q <target>`
- Проверьте паттерн поиска файлов
- Убедитесь в правильности `--haiku-root`

### "No Meson objects found"  
- Убедитесь что Meson сборка выполнена: `meson compile -C builddir <target>`
- Проверьте паттерн поиска файлов
- Убедитесь что builddir существует

### Ошибки nm/objdump
- Убедитесь что файлы являются объектными/исполняемыми
- Проверьте что binutils установлены

## Расширение функциональности

Скрипт легко расширяется для:
- Сравнения исполняемых файлов (.so, исполняемых)
- Анализа зависимостей библиотек
- Проверки ABI совместимости
- Интеграции в CI/CD pipeline