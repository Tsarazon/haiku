#!/usr/bin/env python3
"""
Haiku Build Comparison Tool
Автоматически сравнивает JAM и Meson сборки для любых компонентов Haiku
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path
from typing import List, Dict, Set, Tuple, Optional
import json
from datetime import datetime

class BuildComparator:
    """Класс для сравнения JAM и Meson сборок"""
    
    def __init__(self, haiku_root: str = "/home/ruslan/haiku"):
        self.haiku_root = Path(haiku_root)
        self.jam_objects = self.haiku_root / "generated/objects/haiku/x86_64/release"
        self.meson_objects = self.haiku_root / "builddir"
        self.temp_dir = Path("/tmp")
        
    def run_command(self, cmd: List[str], capture_output: bool = True) -> subprocess.CompletedProcess:
        """Выполнить команду и вернуть результат"""
        try:
            return subprocess.run(cmd, capture_output=capture_output, text=True, check=True)
        except subprocess.CalledProcessError as e:
            print(f"❌ Ошибка выполнения команды {' '.join(cmd)}: {e}")
            return e
    
    def find_object_files(self, pattern: str) -> Dict[str, List[Path]]:
        """Найти объектные файлы по паттерну в JAM и Meson сборках"""
        results = {"jam": [], "meson": []}
        
        # Поиск в JAM сборке
        jam_pattern = f"**/{pattern}"
        for file in self.jam_objects.glob(jam_pattern):
            if file.is_file():
                results["jam"].append(file)
                
        # Поиск в Meson сборке
        meson_pattern = f"**/{pattern}"
        for file in self.meson_objects.glob(meson_pattern):
            if file.is_file():
                results["meson"].append(file)
                
        return results
    
    def get_file_info(self, file_path: Path) -> Dict[str, any]:
        """Получить информацию о файле"""
        if not file_path.exists():
            return {"exists": False}
            
        stat = file_path.stat()
        file_result = self.run_command(["file", str(file_path)])
        
        return {
            "exists": True,
            "size": stat.st_size,
            "mtime": datetime.fromtimestamp(stat.st_mtime).isoformat(),
            "type": file_result.stdout.strip() if file_result.returncode == 0 else "unknown"
        }
    
    def extract_symbols(self, object_file: Path, symbol_type: str = "T") -> Set[str]:
        """Извлечь символы из объектного файла"""
        if not object_file.exists():
            return set()
            
        cmd = ["nm", str(object_file)]
        result = self.run_command(cmd)
        
        if result.returncode != 0:
            return set()
            
        symbols = set()
        for line in result.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[1] == symbol_type:
                symbols.add(parts[2])  # Symbol name
                
        return symbols
    
    def count_sections(self, object_file: Path) -> int:
        """Подсчитать количество секций в объектном файле"""
        if not object_file.exists():
            return 0
            
        cmd = ["objdump", "-h", str(object_file)]
        result = self.run_command(cmd)
        
        if result.returncode != 0:
            return 0
            
        sections = 0
        for line in result.stdout.splitlines():
            line = line.strip()
            if line and line[0].isdigit():
                sections += 1
                
        return sections
    
    def demangle_symbol(self, symbol: str) -> str:
        """Демангл C++ символа"""
        cmd = ["c++filt", "-n", symbol]
        result = self.run_command(cmd)
        
        if result.returncode == 0:
            return result.stdout.strip()
        return symbol
    
    def compare_object_files(self, jam_file: Path, meson_file: Path) -> Dict[str, any]:
        """Сравнить два объектных файла"""
        jam_info = self.get_file_info(jam_file)
        meson_info = self.get_file_info(meson_file)
        
        if not jam_info["exists"] or not meson_info["exists"]:
            return {
                "error": "One or both files don't exist",
                "jam_exists": jam_info["exists"],
                "meson_exists": meson_info["exists"]
            }
        
        # Извлечь символы
        jam_symbols = self.extract_symbols(jam_file)
        meson_symbols = self.extract_symbols(meson_file)
        
        # Подсчитать секции
        jam_sections = self.count_sections(jam_file)
        meson_sections = self.count_sections(meson_file)
        
        # Анализ различий
        unique_to_jam = jam_symbols - meson_symbols
        unique_to_meson = meson_symbols - jam_symbols
        common_symbols = jam_symbols & meson_symbols
        
        return {
            "files": {
                "jam": {
                    "path": str(jam_file),
                    "size": jam_info["size"],
                    "mtime": jam_info["mtime"],
                    "type": jam_info["type"]
                },
                "meson": {
                    "path": str(meson_file),
                    "size": meson_info["size"], 
                    "mtime": meson_info["mtime"],
                    "type": meson_info["type"]
                }
            },
            "size_comparison": {
                "jam_size": jam_info["size"],
                "meson_size": meson_info["size"],
                "difference": meson_info["size"] - jam_info["size"],
                "percent_diff": ((meson_info["size"] - jam_info["size"]) / jam_info["size"] * 100) if jam_info["size"] > 0 else 0
            },
            "symbols": {
                "jam_count": len(jam_symbols),
                "meson_count": len(meson_symbols),
                "common_count": len(common_symbols),
                "unique_to_jam": len(unique_to_jam),
                "unique_to_meson": len(unique_to_meson),
                "identical": len(unique_to_jam) == 0 and len(unique_to_meson) == 0
            },
            "sections": {
                "jam_count": jam_sections,
                "meson_count": meson_sections,
                "difference": meson_sections - jam_sections
            },
            "unique_symbols": {
                "only_in_jam": list(unique_to_jam)[:10],  # First 10 for brevity
                "only_in_meson": list(unique_to_meson)[:10]
            }
        }
    
    def search_specific_symbols(self, object_file: Path, patterns: List[str]) -> Dict[str, List[str]]:
        """Поиск специфичных символов по паттернам"""
        if not object_file.exists():
            return {}
            
        symbols = self.extract_symbols(object_file)
        results = {}
        
        for pattern in patterns:
            matching = [s for s in symbols if pattern.lower() in s.lower()]
            if matching:
                results[pattern] = matching
                
        return results
    
    def generate_report(self, comparison: Dict[str, any], output_file: Optional[Path] = None) -> str:
        """Генерировать отчет о сравнении"""
        report = []
        report.append("# Haiku Build Comparison Report")
        report.append(f"Generated: {datetime.now().isoformat()}")
        report.append("")
        
        # File info
        jam_file = comparison["files"]["jam"]
        meson_file = comparison["files"]["meson"]
        
        report.append("## File Information")
        report.append("### JAM Build:")
        report.append(f"- Path: `{jam_file['path']}`")
        report.append(f"- Size: {jam_file['size']:,} bytes")
        report.append(f"- Modified: {jam_file['mtime']}")
        report.append(f"- Type: {jam_file['type']}")
        report.append("")
        
        report.append("### Meson Build:")
        report.append(f"- Path: `{meson_file['path']}`")
        report.append(f"- Size: {meson_file['size']:,} bytes") 
        report.append(f"- Modified: {meson_file['mtime']}")
        report.append(f"- Type: {meson_file['type']}")
        report.append("")
        
        # Size comparison
        size_comp = comparison["size_comparison"]
        report.append("## Size Comparison")
        report.append(f"- JAM: {size_comp['jam_size']:,} bytes")
        report.append(f"- Meson: {size_comp['meson_size']:,} bytes")
        report.append(f"- Difference: {size_comp['difference']:+,} bytes ({size_comp['percent_diff']:+.2f}%)")
        
        if abs(size_comp['percent_diff']) < 1.0:
            report.append("- ✅ **Sizes are virtually identical**")
        elif abs(size_comp['percent_diff']) < 5.0:
            report.append("- ⚠️ **Small size difference**")
        else:
            report.append("- ❌ **Significant size difference**")
        report.append("")
        
        # Symbol comparison
        symbols = comparison["symbols"]
        report.append("## Symbol Comparison")
        report.append(f"- JAM symbols: {symbols['jam_count']:,}")
        report.append(f"- Meson symbols: {symbols['meson_count']:,}")
        report.append(f"- Common symbols: {symbols['common_count']:,}")
        report.append(f"- Unique to JAM: {symbols['unique_to_jam']:,}")
        report.append(f"- Unique to Meson: {symbols['unique_to_meson']:,}")
        
        if symbols["identical"]:
            report.append("- ✅ **Symbol sets are identical**")
        else:
            report.append("- ⚠️ **Symbol sets differ**")
        report.append("")
        
        # Section comparison
        sections = comparison["sections"]
        report.append("## Section Comparison") 
        report.append(f"- JAM sections: {sections['jam_count']}")
        report.append(f"- Meson sections: {sections['meson_count']}")
        report.append(f"- Difference: {sections['difference']:+}")
        
        if sections['difference'] == 0:
            report.append("- ✅ **Section counts identical**")
        elif abs(sections['difference']) <= 5:
            report.append("- ⚠️ **Minor section count difference**")
        else:
            report.append("- ❌ **Significant section count difference**")
        report.append("")
        
        # Unique symbols (if any)
        unique_syms = comparison["unique_symbols"]
        if unique_syms["only_in_jam"]:
            report.append("### Symbols only in JAM (first 10):")
            for sym in unique_syms["only_in_jam"]:
                demangled = self.demangle_symbol(sym)
                report.append(f"- `{sym}`")
                if demangled != sym:
                    report.append(f"  - Demangled: `{demangled}`")
            report.append("")
            
        if unique_syms["only_in_meson"]:
            report.append("### Symbols only in Meson (first 10):")
            for sym in unique_syms["only_in_meson"]:
                demangled = self.demangle_symbol(sym)
                report.append(f"- `{sym}`")
                if demangled != sym:
                    report.append(f"  - Demangled: `{demangled}`")
            report.append("")
        
        # Overall assessment
        report.append("## Overall Assessment")
        
        assessment_score = 0
        total_checks = 3
        
        # Size check
        if abs(size_comp['percent_diff']) < 1.0:
            assessment_score += 1
            
        # Symbol check
        if symbols["identical"]:
            assessment_score += 1
            
        # Section check
        if abs(sections['difference']) <= 3:
            assessment_score += 1
            
        if assessment_score == total_checks:
            report.append("✅ **EXCELLENT**: Builds are essentially identical")
        elif assessment_score >= 2:
            report.append("⚠️ **GOOD**: Builds are very similar with minor differences")
        else:
            report.append("❌ **CONCERNING**: Builds have significant differences")
            
        report_text = "\n".join(report)
        
        if output_file:
            with open(output_file, 'w', encoding='utf-8') as f:
                f.write(report_text)
                
        return report_text
    
    def compare_component(self, component_name: str, object_pattern: str, 
                         search_patterns: Optional[List[str]] = None) -> Dict[str, any]:
        """Сравнить компонент по имени"""
        print(f"🔍 Searching for {component_name} objects with pattern '{object_pattern}'...")
        
        # Найти файлы
        files = self.find_object_files(object_pattern)
        
        if not files["jam"]:
            print(f"❌ No JAM objects found for pattern '{object_pattern}'")
            return {"error": "No JAM objects found"}
            
        if not files["meson"]:
            print(f"❌ No Meson objects found for pattern '{object_pattern}'")
            return {"error": "No Meson objects found"}
        
        print(f"📁 JAM objects found: {len(files['jam'])}")
        for f in files["jam"]:
            print(f"   - {f}")
            
        print(f"📁 Meson objects found: {len(files['meson'])}")
        for f in files["meson"]:
            print(f"   - {f}")
        
        # Сравнить первые найденные файлы (можно расширить для всех)
        jam_file = files["jam"][0]
        meson_file = files["meson"][0]
        
        print(f"🔄 Comparing:")
        print(f"  JAM:   {jam_file}")
        print(f"  Meson: {meson_file}")
        
        comparison = self.compare_object_files(jam_file, meson_file)
        
        # Поиск специфичных символов если задано
        if search_patterns:
            print(f"🔍 Searching for specific symbols: {search_patterns}")
            jam_specific = self.search_specific_symbols(jam_file, search_patterns)
            meson_specific = self.search_specific_symbols(meson_file, search_patterns)
            
            comparison["specific_symbols"] = {
                "patterns": search_patterns,
                "jam_matches": jam_specific,
                "meson_matches": meson_specific
            }
        
        return comparison

def main():
    parser = argparse.ArgumentParser(
        description="Compare JAM and Meson builds of Haiku components",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Compare interface kit
  python compare_builds.py interface_kit "*/interface/interface_kit.o"
  
  # Compare with specific symbol search
  python compare_builds.py interface_kit "*/interface/interface_kit.o" --search-symbols hsl rgb color
  
  # Save report to file
  python compare_builds.py app_kit "*/app/app_kit.o" --output app_kit_comparison.md
        """
    )
    
    parser.add_argument("component", help="Component name for reporting")
    parser.add_argument("pattern", help="Glob pattern to find object files")
    parser.add_argument("--search-symbols", "-s", nargs="*", 
                       help="Symbol patterns to search for specifically")
    parser.add_argument("--output", "-o", type=Path,
                       help="Output file for markdown report")
    parser.add_argument("--json", action="store_true",
                       help="Also output raw JSON data")
    parser.add_argument("--haiku-root", default="/home/ruslan/haiku",
                       help="Path to Haiku source root")
    
    args = parser.parse_args()
    
    # Создать компаратор
    comparator = BuildComparator(args.haiku_root)
    
    # Выполнить сравнение
    comparison = comparator.compare_component(
        args.component, 
        args.pattern,
        args.search_symbols
    )
    
    if "error" in comparison:
        print(f"❌ Comparison failed: {comparison['error']}")
        sys.exit(1)
    
    # Генерировать отчет
    print("\n📝 Generating report...")
    report = comparator.generate_report(comparison, args.output)
    
    # Вывод результатов
    if args.output:
        print(f"📄 Report saved to: {args.output}")
    else:
        print("\n" + "="*60)
        print(report)
    
    # JSON output если нужен
    if args.json:
        json_file = args.output.with_suffix('.json') if args.output else Path(f"{args.component}_comparison.json")
        with open(json_file, 'w') as f:
            json.dump(comparison, f, indent=2, default=str)
        print(f"📊 JSON data saved to: {json_file}")
    
    # Summary
    symbols = comparison["symbols"]
    size_diff = comparison["size_comparison"]["percent_diff"]
    
    print(f"\n📊 Quick Summary:")
    print(f"   Size difference: {size_diff:+.2f}%")
    print(f"   Symbol count: JAM={symbols['jam_count']}, Meson={symbols['meson_count']}")
    print(f"   Identical symbols: {'✅ YES' if symbols['identical'] else '❌ NO'}")
    
    if symbols["identical"] and abs(size_diff) < 1.0:
        print("🎉 PERFECT MATCH! Builds are essentially identical.")
        sys.exit(0)
    elif symbols["identical"]:
        print("✅ EXCELLENT! Symbol sets identical, minor size difference.")
        sys.exit(0)
    else:
        print("⚠️  Builds differ - check the report for details.")
        sys.exit(1)

if __name__ == "__main__":
    main()