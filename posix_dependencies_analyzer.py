#!/usr/bin/env python3
"""
Haiku POSIX Dependencies Analyzer
Глубокий анализ внешних зависимостей всех POSIX модулей в libroot
Создает детальный отчет в формате Markdown
"""

import os
import sys
import re
from pathlib import Path
from typing import Dict, List, Set, Tuple
from datetime import datetime
import subprocess

class PosixDependencyAnalyzer:
    """Анализатор зависимостей POSIX модулей"""
    
    def __init__(self, haiku_root: str = "/home/ruslan/haiku"):
        self.haiku_root = Path(haiku_root)
        self.posix_path = self.haiku_root / "src/system/libroot/posix"
        
        # Паттерны для поиска различных зависимостей
        self.dependency_patterns = {
            'icu': {
                'includes': [r'#include.*unicode/', r'#include.*icu/', r'#include.*<u(cal|coll|dat|loc|norm|brk|trans).*\.h>'],
                'functions': [r'\bu[a-z]+_[a-zA-Z_]+\(', r'U_ICU_[A-Z_]+', r'icu::', r'UChar\s*\*', r'UnicodeString', r'ucol_[a-zA-Z_]+\(', r'ubrk_[a-zA-Z_]+\(', r'uloc_[a-zA-Z_]+\('],
                'defines': [r'U_PLATFORM_[A-Z_]+', r'UCONFIG_[A-Z_]+', r'U_ICU_VERSION']
            },
            'openssl': {
                'includes': [r'#include\s*<openssl/', r'#include\s*"openssl/', r'#include\s*<crypto\.h>', r'#include\s*"crypto\.h"'],
                'functions': [r'\bEVP_[a-zA-Z_]+\s*\(', r'\bHMAC_[a-zA-Z_]+\s*\(', r'\bSHA[0-9]+_[a-zA-Z_]+\s*\(', 
                             r'\bMD5_[a-zA-Z_]+\s*\(', r'\bRSA_[a-zA-Z_]+\s*\(', r'\bAES_[a-zA-Z_]+\s*\('],
                'defines': [r'#define\s+OPENSSL_[A-Z_]+', r'#define\s+EVP_[A-Z_]+']
            },
            'zlib': {
                'includes': [r'#include.*zlib\.h', r'#include.*zconf\.h'],
                'functions': [r'deflate\(', r'inflate\(', r'gzip[a-zA-Z_]*\(', r'compress\(', r'uncompress\('],
                'defines': [r'ZLIB_[A-Z_]+']
            },
            'zstd': {
                'includes': [r'#include.*zstd\.h', r'#include.*zdict\.h'],
                'functions': [r'ZSTD_[a-zA-Z_]+\(', r'ZDICT_[a-zA-Z_]+\('],
                'defines': [r'ZSTD_[A-Z_]+']
            },
            'libpng': {
                'includes': [r'#include.*png\.h', r'#include.*pngconf\.h'],
                'functions': [r'png_[a-zA-Z_]+\('],
                'defines': [r'PNG_[A-Z_]+']
            },
            'jpeg': {
                'includes': [r'#include.*jpeglib\.h', r'#include.*jerror\.h'],
                'functions': [r'jpeg_[a-zA-Z_]+\('],
                'defines': [r'JPEG_[A-Z_]+']
            },
            'freetype': {
                'includes': [r'#include.*freetype/', r'#include.*ft2build\.h'],
                'functions': [r'FT_[a-zA-Z_]+\(', r'FTC_[a-zA-Z_]+\('],
                'defines': [r'FREETYPE_[A-Z_]+', r'FT_[A-Z_]+']
            },
            'curl': {
                'includes': [r'#include.*curl/'],
                'functions': [r'curl_[a-zA-Z_]+\(', r'CURL[A-Z]*_[a-zA-Z_]+\('],
                'defines': [r'CURL_[A-Z_]+', r'LIBCURL_[A-Z_]+']
            },
            'math': {
                'includes': [r'#include.*math\.h', r'#include.*complex\.h', r'#include.*fenv\.h'],
                'functions': [r'sin\(', r'cos\(', r'tan\(', r'pow\(', r'sqrt\(', r'log\(', r'exp\('],
                'defines': [r'M_PI', r'HUGE_VAL']
            }
        }
        
        # Исключения из поиска (ложные срабатывания)
        self.false_positives = [
            r'.*DISCLAIMED.*',  # BSD лицензии
            r'.*PARTICULAR PURPOSE.*',  # Лицензии
            r'.*FITNESS FOR.*',  # Лицензии
            r'.*COPYRIGHT.*',  # Copyright
            r'/\*.*\*/',  # Комментарии
            r'^\s*//',  # Однострочные комментарии в начале
            r'.*unlock_variables\(.*',  # unlock_variables - не ICU функция
            r'.*update_variable\(.*',  # update_variable - не ICU функция  
            r'#include.*<u(nistd|mask|time|types|io)\.h>',  # системные заголовки, не ICU
            r'.*EMIT_.*',  # glibc internal defines
            r'^\s*\*.*',  # Комментарии в стиле /* * function_name() */
            r'^\s*//.*',  # Комментарии //
        ]

    def discover_posix_modules(self) -> Dict[str, Dict]:
        """Обнаруживает все POSIX модули"""
        modules = {}
        
        if not self.posix_path.exists():
            return modules
            
        for item in self.posix_path.iterdir():
            if item.is_dir() and not item.name.startswith('.') and item.name not in ['arch', 'CVS']:
                modules[item.name] = {
                    'name': item.name,
                    'path': item,
                    'sources': self._find_source_files(item),
                    'headers': self._find_header_files(item),
                    'dependencies': {}
                }
                
        return modules

    def _find_source_files(self, directory: Path) -> List[Path]:
        """Находит исходные файлы в директории"""
        patterns = ['*.c', '*.cpp', '*.cc', '*.cxx', '*.S', '*.asm']
        files = []
        for pattern in patterns:
            files.extend(directory.rglob(pattern))
        return files

    def _find_header_files(self, directory: Path) -> List[Path]:
        """Находит заголовочные файлы в директории"""
        patterns = ['*.h', '*.hpp', '*.hh', '*.hxx']
        files = []
        for pattern in patterns:
            files.extend(directory.rglob(pattern))
        return files

    def _is_false_positive(self, line: str) -> bool:
        """Проверяет, является ли строка ложным срабатыванием"""
        for pattern in self.false_positives:
            if re.search(pattern, line, re.IGNORECASE):
                return True
        return False

    def analyze_file_dependencies(self, file_path: Path) -> Dict[str, List[Dict]]:
        """Анализирует зависимости в одном файле"""
        dependencies = {}
        
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
                lines = content.split('\n')
                
            for dep_name, patterns in self.dependency_patterns.items():
                matches = []
                
                # Поиск includes
                for include_pattern in patterns['includes']:
                    for i, line in enumerate(lines, 1):
                        if self._is_false_positive(line):
                            continue
                        match = re.search(include_pattern, line)
                        if match:
                            matches.append({
                                'type': 'include',
                                'line': i,
                                'text': line.strip(),
                                'match': match.group()
                            })
                
                # Поиск функций
                for func_pattern in patterns['functions']:
                    for i, line in enumerate(lines, 1):
                        if self._is_false_positive(line):
                            continue
                        match = re.search(func_pattern, line)
                        if match:
                            matches.append({
                                'type': 'function',
                                'line': i,
                                'text': line.strip(),
                                'match': match.group()
                            })
                
                # Поиск определений
                for define_pattern in patterns['defines']:
                    for i, line in enumerate(lines, 1):
                        if self._is_false_positive(line):
                            continue
                        match = re.search(define_pattern, line)
                        if match:
                            matches.append({
                                'type': 'define',
                                'line': i,
                                'text': line.strip(),
                                'match': match.group()
                            })
                
                if matches:
                    dependencies[dep_name] = matches
                    
        except Exception as e:
            print(f"Error analyzing {file_path}: {e}")
            
        return dependencies

    def analyze_module_dependencies(self, module_info: Dict) -> Dict:
        """Анализирует зависимости модуля"""
        all_dependencies = {}
        all_files = module_info['sources'] + module_info['headers']
        
        for file_path in all_files:
            file_deps = self.analyze_file_dependencies(file_path)
            for dep_name, matches in file_deps.items():
                if dep_name not in all_dependencies:
                    all_dependencies[dep_name] = []
                
                for match in matches:
                    match['file'] = file_path
                    all_dependencies[dep_name].append(match)
        
        module_info['dependencies'] = all_dependencies
        return module_info

    def generate_markdown_report(self, modules: Dict[str, Dict]) -> str:
        """Генерирует Markdown отчет"""
        report = []
        
        # Заголовок
        report.append("# Haiku POSIX Modules Dependencies Analysis")
        report.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        report.append(f"Total modules analyzed: {len(modules)}")
        report.append("")
        
        # Сводка
        report.append("## Summary")
        report.append("")
        
        summary_table = []
        summary_table.append("| Module | Files | Dependencies | Status |")
        summary_table.append("|--------|-------|-------------|--------|")
        
        for module_name, module_info in sorted(modules.items()):
            files_count = len(module_info['sources']) + len(module_info['headers'])
            deps_count = len(module_info['dependencies'])
            status = "✅ Clean" if deps_count == 0 else f"⚠️  {deps_count} deps"
            
            summary_table.append(f"| {module_name} | {files_count} | {deps_count} | {status} |")
        
        report.extend(summary_table)
        report.append("")
        
        # Детальный анализ по модулям
        report.append("## Detailed Analysis")
        report.append("")
        
        for module_name, module_info in sorted(modules.items()):
            report.append(f"### {module_name}")
            report.append(f"**Path:** `{module_info['path']}`")
            report.append(f"**Files:** {len(module_info['sources'])} sources, {len(module_info['headers'])} headers")
            report.append("")
            
            if not module_info['dependencies']:
                report.append("✅ **No external dependencies found**")
                report.append("")
            else:
                report.append("⚠️  **External dependencies detected:**")
                report.append("")
                
                for dep_name, matches in sorted(module_info['dependencies'].items()):
                    report.append(f"#### {dep_name.upper()} ({len(matches)} occurrences)")
                    report.append("")
                    
                    # Группировка по файлам
                    files_dict = {}
                    for match in matches:
                        file_path = match['file']
                        if file_path not in files_dict:
                            files_dict[file_path] = []
                        files_dict[file_path].append(match)
                    
                    for file_path, file_matches in sorted(files_dict.items()):
                        rel_path = file_path.relative_to(module_info['path'])
                        report.append(f"**{rel_path}:**")
                        
                        for match in sorted(file_matches, key=lambda x: x['line']):
                            match_type = match['type'].capitalize()
                            line_num = match['line']
                            text = match['text'][:100] + "..." if len(match['text']) > 100 else match['text']
                            report.append(f"- Line {line_num} ({match_type}): `{text}`")
                        report.append("")
                
                report.append("")
        
        # Рекомендации
        report.append("## Recommendations")
        report.append("")
        
        modules_with_deps = {name: info for name, info in modules.items() if info['dependencies']}
        
        if not modules_with_deps:
            report.append("✅ All POSIX modules are clean with no external dependencies.")
        else:
            report.append("Based on the analysis, the following BuildFeatures integration is recommended:")
            report.append("")
            
            for module_name, module_info in sorted(modules_with_deps.items()):
                deps = list(module_info['dependencies'].keys())
                report.append(f"- **{module_name}**: {', '.join(deps)}")
            
            report.append("")
            report.append("Update `libroot_posix_modules_generator.py` module feature map:")
            report.append("")
            report.append("```python")
            report.append("module_feature_map = {")
            
            for module_name, module_info in sorted(modules_with_deps.items()):
                deps = list(module_info['dependencies'].keys())
                clean_deps = [dep for dep in deps if dep in ['icu', 'openssl', 'zlib', 'zstd', 'libpng', 'jpeg', 'freetype', 'curl']]
                if clean_deps:
                    report.append(f"    '{module_name}': {clean_deps},")
            
            report.append("}")
            report.append("```")
        
        report.append("")
        report.append("---")
        report.append(f"*Generated by posix_dependencies_analyzer.py on {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}*")
        
        return '\n'.join(report)

    def run_analysis(self) -> Tuple[Dict[str, Dict], str]:
        """Запускает полный анализ"""
        print("🔍 Discovering POSIX modules...")
        modules = self.discover_posix_modules()
        print(f"Found {len(modules)} modules")
        
        print("🔬 Analyzing dependencies...")
        for module_name, module_info in modules.items():
            print(f"  Analyzing {module_name}...")
            self.analyze_module_dependencies(module_info)
        
        print("📄 Generating report...")
        report = self.generate_markdown_report(modules)
        
        return modules, report

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='Analyze POSIX modules dependencies')
    parser.add_argument('--haiku-root', default='/home/ruslan/haiku',
                       help='Path to Haiku source root')
    parser.add_argument('--output', default='POSIX_DEPENDENCIES_ANALYSIS.md',
                       help='Output markdown file')
    parser.add_argument('--verbose', action='store_true',
                       help='Verbose output')
    
    args = parser.parse_args()
    
    print("🚀 Haiku POSIX Dependencies Analyzer")
    print(f"📁 Haiku root: {args.haiku_root}")
    print(f"📄 Output file: {args.output}")
    print("=" * 60)
    
    analyzer = PosixDependencyAnalyzer(args.haiku_root)
    modules, report = analyzer.run_analysis()
    
    # Записываем отчет
    output_path = Path(args.haiku_root) / args.output
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(report)
    
    print(f"\n✅ Analysis complete!")
    print(f"📄 Report saved to: {output_path}")
    print(f"📊 Analyzed {len(modules)} modules")
    
    # Краткая статистика
    modules_with_deps = sum(1 for m in modules.values() if m['dependencies'])
    total_deps = sum(len(m['dependencies']) for m in modules.values())
    
    print(f"⚠️  Modules with dependencies: {modules_with_deps}")
    print(f"🔗 Total dependencies found: {total_deps}")

if __name__ == "__main__":
    main()