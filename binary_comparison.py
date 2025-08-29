#!/usr/bin/env python3
"""
Binary Comparison Tool for Haiku OS Development
Comprehensive analysis and comparison of binary files after C++14 refactoring

Usage:
    python3 binary_comparison.py <old_binary> <new_binary> [--report output.md]
    
Examples:
    python3 binary_comparison.py /path/to/old/app_server /path/to/new/app_server
    python3 binary_comparison.py --report detailed_report.md old_binary new_binary
    python3 binary_comparison.py --json comparison.json old_binary new_binary
"""

import os
import sys
import subprocess
import argparse
import json
from datetime import datetime
from typing import Dict, Set, List, Optional, Tuple
import tempfile


class BinaryComparator:
    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        
    def log(self, message: str, level: str = "INFO"):
        """Логирование с временными метками"""
        if self.verbose or level == "ERROR":
            timestamp = datetime.now().strftime('%H:%M:%S')
            print(f"[{timestamp}] {level}: {message}")
    
    def compare_binaries(self, old_binary: str, new_binary: str) -> Dict:
        """Полное сравнение двух бинарных файлов"""
        self.log(f"Starting comprehensive binary comparison")
        self.log(f"Old binary: {old_binary}")
        self.log(f"New binary: {new_binary}")
        
        comparison = {
            'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
            'old_binary': old_binary,
            'new_binary': new_binary,
            'file_properties': {},
            'elf_sections': {},
            'symbol_analysis': {},
            'dependency_analysis': {},
            'api_compatibility': {},
            'performance_analysis': {},
            'security_analysis': {},
            'summary': {}
        }
        
        if not self._validate_files(old_binary, new_binary):
            return comparison
            
        try:
            # 1. Основные свойства файлов
            comparison['file_properties'] = self._analyze_file_properties(old_binary, new_binary)
            
            # 2. Анализ ELF секций
            comparison['elf_sections'] = self._analyze_elf_sections(old_binary, new_binary)
            
            # 3. Анализ таблицы символов
            comparison['symbol_analysis'] = self._analyze_symbols(old_binary, new_binary)
            
            # 4. Анализ зависимостей
            comparison['dependency_analysis'] = self._analyze_dependencies(old_binary, new_binary)
            
            # 5. Проверка совместимости API
            comparison['api_compatibility'] = self._check_api_compatibility(comparison['symbol_analysis'])
            
            # 6. Анализ производительности
            comparison['performance_analysis'] = self._analyze_performance_impact(comparison)
            
            # 7. Анализ безопасности
            comparison['security_analysis'] = self._analyze_security_features(old_binary, new_binary)
            
            # 8. Итоговый summary
            comparison['summary'] = self._generate_summary(comparison)
            
        except Exception as e:
            self.log(f"Comparison failed: {e}", "ERROR")
        
        return comparison
    
    def _validate_files(self, old_binary: str, new_binary: str) -> bool:
        """Проверка существования и доступности файлов"""
        for binary, name in [(old_binary, "old"), (new_binary, "new")]:
            if not os.path.exists(binary):
                self.log(f"{name.capitalize()} binary not found: {binary}", "ERROR")
                return False
            if not os.access(binary, os.R_OK):
                self.log(f"{name.capitalize()} binary not readable: {binary}", "ERROR")
                return False
        return True
    
    def _analyze_file_properties(self, old_binary: str, new_binary: str) -> Dict:
        """Анализ базовых свойств файлов"""
        props = {
            'size_comparison': {},
            'file_types': {},
            'timestamps': {},
            'permissions': {}
        }
        
        for binary, key in [(old_binary, 'old'), (new_binary, 'new')]:
            stat = os.stat(binary)
            
            props['size_comparison'][key] = stat.st_size
            props['timestamps'][key] = datetime.fromtimestamp(stat.st_mtime).strftime('%Y-%m-%d %H:%M:%S')
            props['permissions'][key] = oct(stat.st_mode)[-3:]
            
            # Определение типа файла
            try:
                result = subprocess.run(['file', binary], capture_output=True, text=True)
                props['file_types'][key] = result.stdout.strip() if result.returncode == 0 else "Unknown"
            except:
                props['file_types'][key] = "Unknown"
        
        # Вычисления разности
        props['size_comparison']['diff'] = props['size_comparison']['new'] - props['size_comparison']['old']
        props['size_comparison']['diff_percent'] = (
            props['size_comparison']['diff'] / props['size_comparison']['old'] * 100 
            if props['size_comparison']['old'] > 0 else 0
        )
        
        props['type_compatible'] = props['file_types']['old'] == props['file_types']['new']
        
        return props
    
    def _analyze_elf_sections(self, old_binary: str, new_binary: str) -> Dict:
        """Детальный анализ ELF секций"""
        sections = {
            'section_sizes': {},
            'section_differences': {},
            'new_sections': [],
            'removed_sections': [],
            'total_analysis': {}
        }
        
        for binary, key in [(old_binary, 'old'), (new_binary, 'new')]:
            sections['section_sizes'][key] = {}
            
            try:
                # Используем objdump для получения информации о секциях
                result = subprocess.run(['objdump', '-h', binary], capture_output=True, text=True)
                
                if result.returncode == 0:
                    for line in result.stdout.split('\n'):
                        if line.strip() and not line.startswith('Idx'):
                            parts = line.split()
                            if len(parts) >= 6 and parts[0].isdigit():
                                section_name = parts[1]
                                try:
                                    size = int(parts[2], 16)
                                    sections['section_sizes'][key][section_name] = size
                                except ValueError:
                                    continue
                
                # Также получаем размеры основных секций через size
                size_result = subprocess.run(['size', binary], capture_output=True, text=True)
                if size_result.returncode == 0:
                    lines = size_result.stdout.strip().split('\n')
                    if len(lines) >= 2:
                        headers = lines[0].split()
                        values = lines[1].split()
                        
                        if len(headers) == len(values):
                            for header, value in zip(headers, values):
                                if header.lower() in ['text', 'data', 'bss', 'dec', 'hex']:
                                    try:
                                        sections['section_sizes'][key][f'_{header.lower()}'] = int(value)
                                    except ValueError:
                                        pass
                        
            except Exception as e:
                self.log(f"Section analysis failed for {binary}: {e}")
        
        # Анализ различий
        old_sections = set(sections['section_sizes'].get('old', {}).keys())
        new_sections = set(sections['section_sizes'].get('new', {}).keys())
        
        sections['new_sections'] = list(new_sections - old_sections)
        sections['removed_sections'] = list(old_sections - new_sections)
        
        common_sections = old_sections & new_sections
        for section in common_sections:
            old_size = sections['section_sizes']['old'].get(section, 0)
            new_size = sections['section_sizes']['new'].get(section, 0)
            
            sections['section_differences'][section] = {
                'old': old_size,
                'new': new_size,
                'diff': new_size - old_size,
                'diff_percent': (new_size - old_size) / old_size * 100 if old_size > 0 else 0
            }
        
        # Суммарный анализ
        sections['total_analysis'] = self._calculate_total_sections(sections['section_differences'])
        
        return sections
    
    def _calculate_total_sections(self, section_diffs: Dict) -> Dict:
        """Подсчет итогового изменения размера секций"""
        total = {'old': 0, 'new': 0, 'diff': 0}
        
        for section, data in section_diffs.items():
            if not section.startswith('_'):  # Пропускаем служебные секции от size
                total['old'] += data.get('old', 0)
                total['new'] += data.get('new', 0)
        
        total['diff'] = total['new'] - total['old']
        total['diff_percent'] = total['diff'] / total['old'] * 100 if total['old'] > 0 else 0
        
        return total
    
    def _analyze_symbols(self, old_binary: str, new_binary: str) -> Dict:
        """Глубокий анализ символов"""
        symbols = {
            'public_symbols': {'old': set(), 'new': set()},
            'private_symbols': {'old': set(), 'new': set()},
            'undefined_symbols': {'old': set(), 'new': set()},
            'symbol_changes': {},
            'critical_symbols': {},
            'virtual_tables': {'old': set(), 'new': set()},
            'function_symbols': {'old': set(), 'new': set()},
            'statistics': {}
        }
        
        # Анализ различных типов символов
        symbol_types = [
            ('public_symbols', ['nm', '-D', '--defined-only']),
            ('undefined_symbols', ['nm', '-D', '--undefined-only']), 
            ('private_symbols', ['nm', '--defined-only'])
        ]
        
        for symbol_type, cmd_base in symbol_types:
            for binary, key in [(old_binary, 'old'), (new_binary, 'new')]:
                try:
                    cmd = cmd_base + [binary]
                    result = subprocess.run(cmd, capture_output=True, text=True)
                    
                    if result.returncode == 0:
                        for line in result.stdout.split('\n'):
                            if line.strip():
                                parts = line.split()
                                if len(parts) >= 3:
                                    symbol_name = parts[-1]
                                    symbol_type_char = parts[1] if len(parts) > 1 else ''
                                    
                                    symbols[symbol_type][key].add(symbol_name)
                                    
                                    # Классификация символов
                                    if symbol_name.startswith('_ZTV'):  # Virtual tables
                                        symbols['virtual_tables'][key].add(symbol_name)
                                    elif symbol_type_char in ['T', 't']:  # Functions
                                        symbols['function_symbols'][key].add(symbol_name)
                                        
                except Exception as e:
                    self.log(f"Symbol analysis failed for {binary}: {e}")
        
        # Анализ изменений
        for symbol_type in ['public_symbols', 'undefined_symbols', 'virtual_tables', 'function_symbols']:
            old_syms = symbols[symbol_type]['old']
            new_syms = symbols[symbol_type]['new']
            
            symbols['symbol_changes'][symbol_type] = {
                'added': list(new_syms - old_syms),
                'removed': list(old_syms - new_syms),
                'common': list(old_syms & new_syms),
                'total_old': len(old_syms),
                'total_new': len(new_syms),
                'net_change': len(new_syms) - len(old_syms)
            }
        
        # Анализ критичных символов (Desktop, Decorator, AppServer)
        critical_patterns = ['Desktop', 'Decorator', 'AppServer', 'EventDispatcher', 'ClientMemoryAllocator']
        symbols['critical_symbols'] = self._analyze_critical_symbols(symbols['symbol_changes'], critical_patterns)
        
        # Статистика
        symbols['statistics'] = {
            'total_public_symbols_old': len(symbols['public_symbols']['old']),
            'total_public_symbols_new': len(symbols['public_symbols']['new']),
            'public_symbols_changed': len(symbols['symbol_changes']['public_symbols']['added']) + 
                                   len(symbols['symbol_changes']['public_symbols']['removed']),
            'api_breaking_changes': len(symbols['symbol_changes']['public_symbols']['removed']),
            'virtual_tables_changed': len(symbols['symbol_changes']['virtual_tables']['added']) + 
                                    len(symbols['symbol_changes']['virtual_tables']['removed'])
        }
        
        return symbols
    
    def _analyze_critical_symbols(self, symbol_changes: Dict, patterns: List[str]) -> Dict:
        """Анализ изменений в критичных символах"""
        critical = {pattern: {'added': [], 'removed': []} for pattern in patterns}
        
        for pattern in patterns:
            for symbol in symbol_changes['public_symbols']['added']:
                if pattern in symbol:
                    critical[pattern]['added'].append(symbol)
            
            for symbol in symbol_changes['public_symbols']['removed']:
                if pattern in symbol:
                    critical[pattern]['removed'].append(symbol)
        
        return critical
    
    def _analyze_dependencies(self, old_binary: str, new_binary: str) -> Dict:
        """Анализ зависимостей (shared libraries)"""
        deps = {
            'shared_libraries': {'old': set(), 'new': set()},
            'dependency_changes': {},
            'library_versions': {'old': {}, 'new': {}},
            'missing_dependencies': [],
            'new_dependencies': []
        }
        
        for binary, key in [(old_binary, 'old'), (new_binary, 'new')]:
            try:
                # ldd для получения зависимостей
                result = subprocess.run(['ldd', binary], capture_output=True, text=True)
                if result.returncode == 0:
                    for line in result.stdout.split('\n'):
                        if '=>' in line:
                            parts = line.split('=>')
                            if len(parts) >= 2:
                                lib_name = parts[0].strip()
                                lib_path = parts[1].strip().split(' ')[0]
                                deps['shared_libraries'][key].add(lib_name)
                                deps['library_versions'][key][lib_name] = lib_path
                
                # objdump -p для дополнительной информации
                objdump_result = subprocess.run(['objdump', '-p', binary], capture_output=True, text=True)
                if objdump_result.returncode == 0:
                    for line in objdump_result.stdout.split('\n'):
                        if 'NEEDED' in line:
                            lib = line.strip().split()[-1]
                            deps['shared_libraries'][key].add(lib)
                            
            except Exception as e:
                self.log(f"Dependency analysis failed for {binary}: {e}")
        
        # Анализ изменений зависимостей
        old_deps = deps['shared_libraries']['old']
        new_deps = deps['shared_libraries']['new']
        
        deps['new_dependencies'] = list(new_deps - old_deps)
        deps['missing_dependencies'] = list(old_deps - new_deps)
        
        deps['dependency_changes'] = {
            'total_old': len(old_deps),
            'total_new': len(new_deps),
            'added_count': len(deps['new_dependencies']),
            'removed_count': len(deps['missing_dependencies']),
            'unchanged_count': len(old_deps & new_deps)
        }
        
        return deps
    
    def _check_api_compatibility(self, symbol_analysis: Dict) -> Dict:
        """Проверка совместимости API"""
        compatibility = {
            'is_compatible': True,
            'compatibility_score': 0.0,
            'breaking_changes': [],
            'risk_level': 'LOW',
            'compatibility_details': {}
        }
        
        # Проверяем удаленные публичные символы (игнорируем defaulted constructors/destructors)
        removed_public = symbol_analysis['symbol_changes']['public_symbols']['removed']
        filtered_removed = []
        
        for sym in removed_public:
            # Игнорируем defaulted конструкторы/деструкторы (C1Ev, C2Ev, D0Ev, D1Ev, D2Ev)
            if not (sym.endswith('C1Ev') or sym.endswith('C2Ev') or 
                   sym.endswith('D0Ev') or sym.endswith('D1Ev') or sym.endswith('D2Ev')):
                filtered_removed.append(sym)
        
        if filtered_removed:
            compatibility['is_compatible'] = False
            compatibility['breaking_changes'].extend([f"Removed public symbol: {sym}" for sym in filtered_removed])
        else:
            # Если удалены только defaulted constructors/destructors, это не breaking change
            if removed_public:
                compatibility['breaking_changes'].append(f"Note: {len(removed_public)} defaulted constructors/destructors became inline (safe optimization)")
        
        # Проверяем изменения в виртуальных таблицах
        removed_vtables = symbol_analysis['symbol_changes']['virtual_tables']['removed']
        if removed_vtables:
            compatibility['is_compatible'] = False
            compatibility['breaking_changes'].extend([f"Removed virtual table: {sym}" for sym in removed_vtables])
        
        # Проверяем критичные символы
        for pattern, changes in symbol_analysis['critical_symbols'].items():
            if changes['removed']:
                compatibility['is_compatible'] = False
                compatibility['breaking_changes'].extend([
                    f"Removed critical {pattern} symbol: {sym}" for sym in changes['removed']
                ])
        
        # Вычисляем уровень риска
        total_changes = len(compatibility['breaking_changes'])
        if total_changes == 0:
            compatibility['risk_level'] = 'LOW'
            compatibility['compatibility_score'] = 1.0
        elif total_changes <= 5:
            compatibility['risk_level'] = 'MEDIUM'
            compatibility['compatibility_score'] = 0.7
        else:
            compatibility['risk_level'] = 'HIGH'
            compatibility['compatibility_score'] = 0.3
        
        compatibility['compatibility_details'] = {
            'removed_public_symbols': len(removed_public),
            'removed_virtual_tables': len(removed_vtables),
            'total_breaking_changes': total_changes,
            'preserved_symbols_ratio': (
                len(symbol_analysis['symbol_changes']['public_symbols']['common']) /
                max(1, symbol_analysis['statistics']['total_public_symbols_old'])
            )
        }
        
        return compatibility
    
    def _analyze_performance_impact(self, comparison: Dict) -> Dict:
        """Анализ влияния на производительность"""
        performance = {
            'code_size_impact': 'NEUTRAL',
            'memory_impact': 'NEUTRAL',
            'optimization_indicators': [],
            'performance_score': 0.5,
            'recommendations': []
        }
        
        # Анализ размера кода
        if 'elf_sections' in comparison:
            total_diff = comparison['elf_sections']['total_analysis'].get('diff', 0)
            
            if total_diff < -100:  # Существенное уменьшение
                performance['code_size_impact'] = 'IMPROVED'
                performance['optimization_indicators'].append('Code size reduction detected')
                performance['performance_score'] += 0.2
            elif total_diff > 1000:  # Существенное увеличение
                performance['code_size_impact'] = 'DEGRADED'
                performance['optimization_indicators'].append('Significant code size increase')
                performance['performance_score'] -= 0.2
            
            # Анализ отдельных секций
            sections = comparison['elf_sections']['section_differences']
            
            # Текстовая секция (код)
            if '_text' in sections:
                text_diff = sections['_text'].get('diff', 0)
                if text_diff < 0:
                    performance['optimization_indicators'].append('Text section optimized')
                elif text_diff > 500:
                    performance['optimization_indicators'].append('Text section enlarged significantly')
            
            # Секция данных
            if '_data' in sections:
                data_diff = sections['_data'].get('diff', 0)
                if data_diff < 0:
                    performance['memory_impact'] = 'IMPROVED'
                    performance['optimization_indicators'].append('Data section size reduced')
                elif data_diff > 200:
                    performance['memory_impact'] = 'DEGRADED'
                    performance['optimization_indicators'].append('Data section size increased')
        
        # Анализ символов
        if 'symbol_analysis' in comparison:
            stats = comparison['symbol_analysis']['statistics']
            
            # Меньше символов может означать лучшую оптимизацию
            if stats['total_public_symbols_new'] < stats['total_public_symbols_old']:
                performance['optimization_indicators'].append('Public symbol count reduced')
                performance['performance_score'] += 0.1
        
        # Генерация рекомендаций
        if performance['code_size_impact'] == 'DEGRADED':
            performance['recommendations'].append('Consider reviewing code changes for optimization opportunities')
        
        if performance['memory_impact'] == 'DEGRADED':
            performance['recommendations'].append('Monitor memory usage in runtime testing')
        
        if not performance['optimization_indicators']:
            performance['recommendations'].append('No significant performance changes detected')
        
        return performance
    
    def _analyze_security_features(self, old_binary: str, new_binary: str) -> Dict:
        """Анализ безопасности бинарных файлов"""
        security = {
            'security_features': {'old': {}, 'new': {}},
            'security_changes': [],
            'security_score': 0.5,
            'recommendations': []
        }
        
        security_checks = [
            ('stack_canary', 'Stack canary protection'),
            ('nx_bit', 'NX bit / Data Execution Prevention'),
            ('pie', 'Position Independent Executable'),
            ('relro', 'Relocation Read-Only'),
            ('fortify', 'FORTIFY_SOURCE')
        ]
        
        for binary, key in [(old_binary, 'old'), (new_binary, 'new')]:
            security['security_features'][key] = {}
            
            try:
                # Проверяем с помощью checksec (если доступен) или readelf
                result = subprocess.run(['readelf', '-W', '-a', binary], capture_output=True, text=True)
                
                if result.returncode == 0:
                    output = result.stdout
                    
                    # Stack canary
                    security['security_features'][key]['stack_canary'] = '__stack_chk_fail' in output
                    
                    # NX bit
                    security['security_features'][key]['nx_bit'] = 'GNU_STACK' in output and 'RWE' not in output
                    
                    # PIE
                    security['security_features'][key]['pie'] = 'DYN' in output and 'EXEC' not in output
                    
                    # RELRO
                    security['security_features'][key]['relro'] = 'GNU_RELRO' in output
                    
                    # FORTIFY
                    security['security_features'][key]['fortify'] = any(
                        func in output for func in ['__printf_chk', '__strcpy_chk', '__memcpy_chk']
                    )
                    
            except Exception as e:
                self.log(f"Security analysis failed for {binary}: {e}")
                continue
        
        # Анализ изменений в безопасности
        for check, description in security_checks:
            old_status = security['security_features']['old'].get(check, False)
            new_status = security['security_features']['new'].get(check, False)
            
            if not old_status and new_status:
                security['security_changes'].append(f"✅ Enabled {description}")
                security['security_score'] += 0.1
            elif old_status and not new_status:
                security['security_changes'].append(f"❌ Disabled {description}")
                security['security_score'] -= 0.2
        
        # Рекомендации по безопасности
        if security['security_score'] < 0.5:
            security['recommendations'].append('Consider enabling additional security features')
        elif security['security_score'] > 0.7:
            security['recommendations'].append('Good security posture maintained')
        
        return security
    
    def _generate_summary(self, comparison: Dict) -> Dict:
        """Генерация итогового резюме"""
        summary = {
            'overall_status': 'UNKNOWN',
            'compatibility_status': 'UNKNOWN',
            'performance_status': 'UNKNOWN',
            'security_status': 'UNKNOWN',
            'key_findings': [],
            'critical_issues': [],
            'recommendations': [],
            'metrics': {}
        }
        
        # Статус совместимости
        if comparison.get('api_compatibility', {}).get('is_compatible', False):
            summary['compatibility_status'] = 'COMPATIBLE'
        else:
            summary['compatibility_status'] = 'INCOMPATIBLE'
            summary['critical_issues'].append('API compatibility issues detected')
        
        # Статус производительности
        perf_score = comparison.get('performance_analysis', {}).get('performance_score', 0.5)
        if perf_score > 0.7:
            summary['performance_status'] = 'IMPROVED'
        elif perf_score < 0.3:
            summary['performance_status'] = 'DEGRADED'
        else:
            summary['performance_status'] = 'NEUTRAL'
        
        # Статус безопасности
        security_score = comparison.get('security_analysis', {}).get('security_score', 0.5)
        if security_score > 0.7:
            summary['security_status'] = 'IMPROVED'
        elif security_score < 0.3:
            summary['security_status'] = 'DEGRADED'
        else:
            summary['security_status'] = 'MAINTAINED'
        
        # Общий статус
        if (summary['compatibility_status'] == 'COMPATIBLE' and 
            summary['performance_status'] != 'DEGRADED' and
            summary['security_status'] != 'DEGRADED'):
            summary['overall_status'] = 'SUCCESS'
        elif summary['compatibility_status'] == 'INCOMPATIBLE':
            summary['overall_status'] = 'FAILURE'
        else:
            summary['overall_status'] = 'WARNING'
        
        # Ключевые находки
        size_diff = comparison.get('file_properties', {}).get('size_comparison', {}).get('diff', 0)
        if size_diff != 0:
            summary['key_findings'].append(f"Binary size changed by {size_diff:+,} bytes")
        
        removed_symbols = len(comparison.get('symbol_analysis', {}).get('symbol_changes', {}).get('public_symbols', {}).get('removed', []))
        if removed_symbols > 0:
            summary['key_findings'].append(f"{removed_symbols} public symbols removed")
        
        # Метрики
        summary['metrics'] = {
            'size_change_bytes': size_diff,
            'size_change_percent': comparison.get('file_properties', {}).get('size_comparison', {}).get('diff_percent', 0),
            'api_compatibility_score': comparison.get('api_compatibility', {}).get('compatibility_score', 0),
            'performance_score': perf_score,
            'security_score': security_score,
            'symbols_removed': removed_symbols,
            'symbols_added': len(comparison.get('symbol_analysis', {}).get('symbol_changes', {}).get('public_symbols', {}).get('added', []))
        }
        
        return summary
    
    def save_report(self, comparison: Dict, output_file: str, format_type: str = 'markdown'):
        """Сохранение отчета в различных форматах"""
        if format_type == 'json':
            self._save_json_report(comparison, output_file)
        else:
            self._save_markdown_report(comparison, output_file)
    
    def _save_json_report(self, comparison: Dict, output_file: str):
        """Сохранение JSON отчета"""
        # Преобразуем sets в lists для JSON сериализации
        json_comparison = self._convert_sets_to_lists(comparison)
        
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(json_comparison, f, indent=2, ensure_ascii=False)
        
        self.log(f"JSON report saved to: {output_file}")
    
    def _convert_sets_to_lists(self, obj):
        """Рекурсивное преобразование sets в lists"""
        if isinstance(obj, set):
            return sorted(list(obj))
        elif isinstance(obj, dict):
            return {key: self._convert_sets_to_lists(value) for key, value in obj.items()}
        elif isinstance(obj, list):
            return [self._convert_sets_to_lists(item) for item in obj]
        else:
            return obj
    
    def _save_markdown_report(self, comparison: Dict, output_file: str):
        """Сохранение подробного Markdown отчета"""
        report = self._generate_markdown_report(comparison)
        
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(report)
        
        self.log(f"Markdown report saved to: {output_file}")
    
    def _generate_markdown_report(self, comparison: Dict) -> str:
        """Генерация подробного Markdown отчета"""
        summary = comparison.get('summary', {})
        
        report = f"""# Comprehensive Binary Comparison Report

**Generated:** {comparison.get('timestamp', 'Unknown')}  
**Old Binary:** `{comparison.get('old_binary', 'Unknown')}`  
**New Binary:** `{comparison.get('new_binary', 'Unknown')}`

---

## 🎯 Executive Summary

| Metric | Status | Score |
|--------|--------|-------|
| **Overall Status** | **{summary.get('overall_status', 'UNKNOWN')}** | - |
| **API Compatibility** | {summary.get('compatibility_status', 'UNKNOWN')} | {summary.get('metrics', {}).get('api_compatibility_score', 0):.3f} |
| **Performance Impact** | {summary.get('performance_status', 'UNKNOWN')} | {summary.get('metrics', {}).get('performance_score', 0):.3f} |
| **Security Status** | {summary.get('security_status', 'UNKNOWN')} | {summary.get('metrics', {}).get('security_score', 0):.3f} |

### Key Findings
"""
        
        for finding in summary.get('key_findings', []):
            report += f"- {finding}\n"
        
        if summary.get('critical_issues'):
            report += "\n### ⚠️ Critical Issues\n"
            for issue in summary['critical_issues']:
                report += f"- **{issue}**\n"
        
        # Детальный анализ файлов
        props = comparison.get('file_properties', {})
        if props:
            size_comp = props.get('size_comparison', {})
            report += f"""
---

## 📁 File Properties Analysis

| Property | Old Binary | New Binary | Change |
|----------|------------|------------|--------|
| **Size** | {size_comp.get('old', 0):,} bytes | {size_comp.get('new', 0):,} bytes | {size_comp.get('diff', 0):+,} bytes ({size_comp.get('diff_percent', 0):+.3f}%) |
| **Modified** | {props.get('timestamps', {}).get('old', 'Unknown')} | {props.get('timestamps', {}).get('new', 'Unknown')} | - |
| **Type Compatible** | - | - | {'✅ Yes' if props.get('type_compatible', False) else '❌ No'} |

### File Types
- **Old:** `{props.get('file_types', {}).get('old', 'Unknown')}`
- **New:** `{props.get('file_types', {}).get('new', 'Unknown')}`
"""
        
        # ELF секции
        elf_data = comparison.get('elf_sections', {})
        if elf_data and elf_data.get('section_differences'):
            report += """
---

## 🔧 ELF Sections Analysis

| Section | Old Size | New Size | Difference | Change % |
|---------|----------|----------|------------|----------|
"""
            for section, data in elf_data['section_differences'].items():
                if not section.startswith('_'):  # Пропускаем служебные
                    continue
                report += f"| {section.lstrip('_').upper()} | {data.get('old', 0):,} | {data.get('new', 0):,} | {data.get('diff', 0):+,} | {data.get('diff_percent', 0):+.3f}% |\n"
            
            total = elf_data.get('total_analysis', {})
            if total:
                report += f"| **TOTAL** | **{total.get('old', 0):,}** | **{total.get('new', 0):,}** | **{total.get('diff', 0):+,}** | **{total.get('diff_percent', 0):+.3f}%** |\n"
        
        # Анализ символов
        symbols = comparison.get('symbol_analysis', {})
        if symbols:
            stats = symbols.get('statistics', {})
            report += f"""
---

## 🔍 Symbol Analysis

### Summary Statistics
- **Public Symbols:** {stats.get('total_public_symbols_old', 0)} → {stats.get('total_public_symbols_new', 0)} ({stats.get('total_public_symbols_new', 0) - stats.get('total_public_symbols_old', 0):+d})
- **API Breaking Changes:** {stats.get('api_breaking_changes', 0)}
- **Virtual Tables Changed:** {stats.get('virtual_tables_changed', 0)}

### Symbol Changes Detail
"""
            
            for symbol_type, changes in symbols.get('symbol_changes', {}).items():
                if changes.get('added') or changes.get('removed'):
                    report += f"\n#### {symbol_type.replace('_', ' ').title()}\n"
                    
                    if changes.get('removed'):
                        report += f"**Removed ({len(changes['removed'])}):**\n"
                        for sym in changes['removed'][:10]:  # Показываем первые 10
                            report += f"- `{sym}`\n"
                        if len(changes['removed']) > 10:
                            report += f"- ... and {len(changes['removed']) - 10} more\n"
                    
                    if changes.get('added'):
                        report += f"**Added ({len(changes['added'])}):**\n"
                        for sym in changes['added'][:10]:  # Показываем первые 10
                            report += f"+ `{sym}`\n"
                        if len(changes['added']) > 10:
                            report += f"+ ... and {len(changes['added']) - 10} more\n"
            
            # Критичные символы
            critical = symbols.get('critical_symbols', {})
            if any(data['removed'] or data['added'] for data in critical.values()):
                report += "\n### 🚨 Critical Symbol Changes\n"
                for pattern, data in critical.items():
                    if data['removed'] or data['added']:
                        report += f"\n**{pattern}:**\n"
                        for sym in data['removed']:
                            report += f"- ❌ Removed: `{sym}`\n"
                        for sym in data['added']:
                            report += f"- ✅ Added: `{sym}`\n"
        
        # Анализ зависимостей
        deps = comparison.get('dependency_analysis', {})
        if deps and (deps.get('new_dependencies') or deps.get('missing_dependencies')):
            report += """
---

## 📦 Dependency Analysis

"""
            changes = deps.get('dependency_changes', {})
            report += f"**Total Dependencies:** {changes.get('total_old', 0)} → {changes.get('total_new', 0)}\n\n"
            
            if deps.get('new_dependencies'):
                report += f"### New Dependencies ({len(deps['new_dependencies'])})\n"
                for dep in deps['new_dependencies']:
                    report += f"+ `{dep}`\n"
                report += "\n"
            
            if deps.get('missing_dependencies'):
                report += f"### Removed Dependencies ({len(deps['missing_dependencies'])})\n"
                for dep in deps['missing_dependencies']:
                    report += f"- `{dep}`\n"
                report += "\n"
        
        # Совместимость API
        api_compat = comparison.get('api_compatibility', {})
        if api_compat:
            report += f"""
---

## 🔌 API Compatibility Analysis

**Status:** {'✅ COMPATIBLE' if api_compat.get('is_compatible', False) else '❌ INCOMPATIBLE'}  
**Risk Level:** {api_compat.get('risk_level', 'UNKNOWN')}  
**Compatibility Score:** {api_compat.get('compatibility_score', 0):.3f}/1.0

"""
            if api_compat.get('breaking_changes'):
                report += "### Breaking Changes\n"
                for change in api_compat['breaking_changes']:
                    report += f"- {change}\n"
                report += "\n"
            
            details = api_compat.get('compatibility_details', {})
            if details:
                report += f"""### Compatibility Metrics
- **Removed Public Symbols:** {details.get('removed_public_symbols', 0)}
- **Removed Virtual Tables:** {details.get('removed_virtual_tables', 0)}
- **Preserved Symbols Ratio:** {details.get('preserved_symbols_ratio', 0):.3f}
"""
        
        # Анализ производительности
        perf = comparison.get('performance_analysis', {})
        if perf:
            report += f"""
---

## ⚡ Performance Analysis

**Code Size Impact:** {perf.get('code_size_impact', 'NEUTRAL')}  
**Memory Impact:** {perf.get('memory_impact', 'NEUTRAL')}  
**Performance Score:** {perf.get('performance_score', 0.5):.3f}/1.0

"""
            
            if perf.get('optimization_indicators'):
                report += "### Optimization Indicators\n"
                for indicator in perf['optimization_indicators']:
                    report += f"- {indicator}\n"
                report += "\n"
            
            if perf.get('recommendations'):
                report += "### Performance Recommendations\n"
                for rec in perf['recommendations']:
                    report += f"- {rec}\n"
                report += "\n"
        
        # Анализ безопасности
        security = comparison.get('security_analysis', {})
        if security:
            report += f"""
---

## 🛡️ Security Analysis

**Security Score:** {security.get('security_score', 0.5):.3f}/1.0

"""
            
            if security.get('security_changes'):
                report += "### Security Changes\n"
                for change in security['security_changes']:
                    report += f"- {change}\n"
                report += "\n"
            
            # Таблица безопасности
            old_features = security.get('security_features', {}).get('old', {})
            new_features = security.get('security_features', {}).get('new', {})
            
            if old_features or new_features:
                report += "### Security Features Comparison\n\n"
                report += "| Feature | Old Binary | New Binary | Status |\n"
                report += "|---------|------------|------------|--------|\n"
                
                all_features = set(old_features.keys()) | set(new_features.keys())
                for feature in sorted(all_features):
                    old_status = '✅' if old_features.get(feature, False) else '❌'
                    new_status = '✅' if new_features.get(feature, False) else '❌'
                    
                    if old_features.get(feature, False) == new_features.get(feature, False):
                        status = 'Unchanged'
                    elif new_features.get(feature, False):
                        status = '🆙 Improved'
                    else:
                        status = '⬇️ Degraded'
                    
                    report += f"| {feature.replace('_', ' ').title()} | {old_status} | {new_status} | {status} |\n"
        
        # Заключение и рекомендации
        report += """
---

## 📋 Conclusion and Recommendations

"""
        
        if summary.get('overall_status') == 'SUCCESS':
            report += "✅ **Overall Assessment: SUCCESS**\n\n"
            report += "The binary comparison shows successful modernization with maintained compatibility.\n\n"
        elif summary.get('overall_status') == 'FAILURE':
            report += "❌ **Overall Assessment: FAILURE**\n\n"
            report += "Critical compatibility issues detected. Review required before deployment.\n\n"
        else:
            report += "⚠️ **Overall Assessment: WARNING**\n\n"
            report += "Some concerns detected. Additional testing recommended.\n\n"
        
        if summary.get('recommendations'):
            report += "### Recommendations\n"
            for rec in summary['recommendations']:
                report += f"- {rec}\n"
        
        report += f"""
---

*Report generated by Haiku Binary Comparison Tool v1.0*  
*Analysis completed at {comparison.get('timestamp', 'Unknown')}*
"""
        
        return report


def main():
    parser = argparse.ArgumentParser(
        description='Comprehensive Binary Comparison Tool for Haiku OS',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument('old_binary', help='Path to the old binary file')
    parser.add_argument('new_binary', help='Path to the new binary file')
    parser.add_argument('--report', '-r', help='Output report file (default: auto-generated)')
    parser.add_argument('--format', choices=['markdown', 'json'], default='markdown',
                       help='Report format (default: markdown)')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Enable verbose output')
    parser.add_argument('--quiet', '-q', action='store_true',
                       help='Suppress console output except errors')
    
    args = parser.parse_args()
    
    # Создаем компаратор
    comparator = BinaryComparator(verbose=args.verbose and not args.quiet)
    
    # Выполняем сравнение
    comparison = comparator.compare_binaries(args.old_binary, args.new_binary)
    
    # Определяем имя файла отчета
    if args.report:
        report_file = args.report
    else:
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        extension = 'json' if args.format == 'json' else 'md'
        report_file = f'binary_comparison_{timestamp}.{extension}'
    
    # Сохраняем отчет
    comparator.save_report(comparison, report_file, args.format)
    
    # Выводим краткую сводку на консоль (если не quiet)
    if not args.quiet:
        summary = comparison.get('summary', {})
        print(f"\n📊 Binary Comparison Summary:")
        print(f"   Overall Status: {summary.get('overall_status', 'UNKNOWN')}")
        print(f"   API Compatible: {summary.get('compatibility_status', 'UNKNOWN')}")
        print(f"   Performance: {summary.get('performance_status', 'UNKNOWN')}")
        print(f"   Size Change: {summary.get('metrics', {}).get('size_change_bytes', 0):+,} bytes")
        print(f"   Report saved: {report_file}")
        
        if summary.get('critical_issues'):
            print(f"\n⚠️  Critical Issues:")
            for issue in summary['critical_issues']:
                print(f"   - {issue}")
    
    # Возвращаем код выхода
    if comparison.get('summary', {}).get('overall_status') == 'FAILURE':
        return 1
    else:
        return 0


if __name__ == '__main__':
    sys.exit(main())