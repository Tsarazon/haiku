#!/bin/bash
"""
Скрипт для массового сравнения всех основных компонентов Haiku
"""

set -e

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'  
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Директория скрипта
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPARE_SCRIPT="$SCRIPT_DIR/compare_builds.py"

# Проверка существования основного скрипта
if [[ ! -f "$COMPARE_SCRIPT" ]]; then
    echo -e "${RED}❌ Не найден compare_builds.py в $SCRIPT_DIR${NC}"
    exit 1
fi

# Список компонентов для сравнения
declare -a COMPONENTS=(
    "interface_kit */interface/interface_kit.o hsl,rgb,color,gradient"
    "storage_kit */storage/storage_kit.o mime,file,directory,volume"
    "support_kit */support/support_kit.o string,list,locker,archivable"
    "app_kit */app/app_kit.o application,window,message,handler"
    "locale_kit */locale/locale_kit.o locale,catalog,collator,formatter"
)

# Опции
VERBOSE=false
OUTPUT_DIR=""
JSON_OUTPUT=false

# Парсинг аргументов
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose)
            VERBOSE=true
            shift
            ;;
        -o|--output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --json)
            JSON_OUTPUT=true
            shift
            ;;
        -h|--help)
            cat << EOF
Использование: $0 [опции]

Опции:
    -v, --verbose       Подробный вывод
    -o, --output-dir    Директория для сохранения отчетов
    --json              Создать также JSON файлы
    -h, --help          Показать эту справку

Примеры:
    $0                                    # Базовое сравнение
    $0 -v -o reports --json              # Подробно с сохранением отчетов
EOF
            exit 0
            ;;
        *)
            echo -e "${RED}❌ Неизвестная опция: $1${NC}"
            exit 1
            ;;
    esac
done

# Создать директорию для отчетов если нужно
if [[ -n "$OUTPUT_DIR" ]]; then
    mkdir -p "$OUTPUT_DIR"
    echo -e "${BLUE}📁 Отчеты будут сохранены в: $OUTPUT_DIR${NC}"
fi

echo -e "${BLUE}🚀 Начинаем массовое сравнение Haiku компонентов...${NC}"
echo

# Статистика
TOTAL_COMPONENTS=${#COMPONENTS[@]}
SUCCESS_COUNT=0
WARNING_COUNT=0
ERROR_COUNT=0

# Функция для логирования
log_result() {
    local component="$1"
    local status="$2"  # success, warning, error
    local message="$3"
    
    case $status in
        success)
            echo -e "${GREEN}✅ $component: $message${NC}"
            ((SUCCESS_COUNT++))
            ;;
        warning)
            echo -e "${YELLOW}⚠️  $component: $message${NC}"
            ((WARNING_COUNT++))
            ;;
        error)
            echo -e "${RED}❌ $component: $message${NC}"
            ((ERROR_COUNT++))
            ;;
    esac
}

# Основной цикл сравнения
for component_info in "${COMPONENTS[@]}"; do
    # Парсинг информации о компоненте
    IFS=' ' read -r component_name pattern search_symbols <<< "$component_info"
    
    echo -e "${BLUE}🔄 Сравниваем $component_name...${NC}"
    
    # Подготовка аргументов
    args=("$component_name" "$pattern")
    
    # Добавить поиск символов если указан
    if [[ -n "$search_symbols" ]]; then
        IFS=',' read -ra SYMBOLS <<< "$search_symbols"
        args+=(--search-symbols "${SYMBOLS[@]}")
    fi
    
    # Добавить вывод в файл если нужно
    if [[ -n "$OUTPUT_DIR" ]]; then
        output_file="$OUTPUT_DIR/${component_name}_comparison.md"
        args+=(--output "$output_file")
    fi
    
    # Добавить JSON если нужно
    if [[ "$JSON_OUTPUT" == "true" ]]; then
        args+=(--json)
    fi
    
    # Выполнить сравнение
    if [[ "$VERBOSE" == "true" ]]; then
        echo -e "${BLUE}   Команда: python3 $COMPARE_SCRIPT ${args[*]}${NC}"
    fi
    
    if output=$(python3 "$COMPARE_SCRIPT" "${args[@]}" 2>&1); then
        exit_code=$?
        
        if [[ $exit_code -eq 0 ]]; then
            if echo "$output" | grep -q "PERFECT MATCH"; then
                log_result "$component_name" "success" "Идеально совпадают"
            else
                log_result "$component_name" "success" "Очень похожи"
            fi
        else
            log_result "$component_name" "warning" "Есть различия"
        fi
        
        if [[ "$VERBOSE" == "true" ]]; then
            echo "$output" | tail -5 | sed 's/^/   /'
        fi
    else
        log_result "$component_name" "error" "Ошибка сравнения"
        if [[ "$VERBOSE" == "true" ]]; then
            echo "$output" | tail -3 | sed 's/^/   /' 
        fi
    fi
    
    echo
done

# Итоговая статистика
echo -e "${BLUE}📊 Итоговая статистика:${NC}"
echo -e "   Всего компонентов: $TOTAL_COMPONENTS"
echo -e "   ${GREEN}Успешно: $SUCCESS_COUNT${NC}"
echo -e "   ${YELLOW}С предупреждениями: $WARNING_COUNT${NC}" 
echo -e "   ${RED}С ошибками: $ERROR_COUNT${NC}"
echo

# Финальная оценка
if [[ $ERROR_COUNT -eq 0 ]] && [[ $WARNING_COUNT -eq 0 ]]; then
    echo -e "${GREEN}🎉 ВСЕ КОМПОНЕНТЫ ИДЕАЛЬНО СОВПАДАЮТ!${NC}"
    exit 0
elif [[ $ERROR_COUNT -eq 0 ]]; then
    echo -e "${YELLOW}✅ Все компоненты собираются, есть незначительные различия.${NC}"
    exit 0
else
    echo -e "${RED}⚠️  Есть компоненты с ошибками. Требуется проверка.${NC}"
    exit 1
fi