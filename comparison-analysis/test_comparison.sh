#!/bin/bash
"""
Быстрый тест системы сравнения на Interface Kit
"""

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "🧪 Тестирование системы сравнения на Interface Kit..."

# Тест 1: Базовое сравнение
echo "📋 Тест 1: Базовое сравнение"
python3 compare_builds.py interface_kit "*/interface/interface_kit.o" --output test_basic.md

# Тест 2: С поиском HSL символов
echo "📋 Тест 2: Поиск HSL/RGB символов"
python3 compare_builds.py interface_kit "*/interface/interface_kit.o" \
    --search-symbols hsl rgb color --output test_hsl.md --json

# Тест 3: Несуществующий компонент (должен вернуть ошибку)
echo "📋 Тест 3: Несуществующий компонент"
if python3 compare_builds.py nonexistent "*/nonexistent/*.o" 2>/dev/null; then
    echo "❌ Тест провален: должен был вернуть ошибку"
    exit 1
else
    echo "✅ Правильно обработана ошибка несуществующего компонента"
fi

echo
echo "✅ Все тесты пройдены!"
echo "📄 Созданы файлы:"
ls -la test_*.md test_*.json 2>/dev/null || true