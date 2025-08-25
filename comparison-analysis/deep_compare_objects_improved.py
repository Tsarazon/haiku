#!/usr/bin/env python3
"""
Enhanced Deep Object Comparison Tool - CMake vs Jam builds
Comprehensive analysis of symbol differences between object files
"""

import subprocess
import sys
import os
import re
from collections import defaultdict

def run_command(cmd):
    """Run shell command and return output"""
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Error running: {cmd}")
            print(f"Error: {result.stderr}")
            return None
        return result.stdout.strip()
    except Exception as e:
        print(f"Exception running {cmd}: {e}")
        return None

def extract_all_symbols(objfile):
    """Extract all symbols from object file using nm -A"""
    cmd = f"nm -A -C {objfile}"
    output = run_command(cmd)
    if not output:
        return []
    
    symbols = []
    for line in output.split('\n'):
        if line.strip():
            parts = line.split()
            if len(parts) >= 3:
                # Format: filename:address type name
                colon_idx = line.find(':')
                if colon_idx > 0:
                    rest = line[colon_idx+1:].strip()
                    parts = rest.split()
                    if len(parts) >= 3:
                        address = parts[0] if parts[0] != '' else '00000000'
                        sym_type = parts[1]
                        name = ' '.join(parts[2:])
                        
                        symbols.append({
                            'address': address,
                            'type': sym_type,
                            'name': name,
                            'demangled': demangle_symbol(name)
                        })
    return symbols

def extract_symbols(objfile, symbol_type="T"):
    """Extract symbols from object file using nm"""
    cmd = f"nm -C {objfile} | grep ' {symbol_type} '"
    output = run_command(cmd)
    if not output:
        return []
    
    symbols = []
    for line in output.split('\n'):
        if line.strip():
            parts = line.split()
            if len(parts) >= 3:
                address = parts[0]
                sym_type = parts[1]
                name = ' '.join(parts[2:])
                symbols.append({
                    'address': address,
                    'type': sym_type,
                    'name': name,
                    'demangled': demangle_symbol(name)
                })
    return symbols

def extract_section_info(objfile):
    """Extract section information using objdump"""
    cmd = f"objdump -h {objfile}"
    output = run_command(cmd)
    if not output:
        return {}
    
    sections = {}
    for line in output.split('\n'):
        line = line.strip()
        if line and line[0].isdigit():
            parts = line.split()
            if len(parts) >= 3:
                idx = parts[0]
                name = parts[1]
                size = int(parts[2], 16) if len(parts[2]) > 0 else 0
                sections[name] = {
                    'index': idx,
                    'size': size
                }
    return sections

def extract_relocations(objfile):
    """Extract relocation information"""
    cmd = f"objdump -r {objfile}"
    output = run_command(cmd)
    if not output:
        return []
    
    relocations = []
    current_section = None
    
    for line in output.split('\n'):
        line = line.strip()
        if line.startswith('RELOCATION RECORDS FOR'):
            current_section = line.split('[')[1].split(']')[0]
        elif line and not line.startswith('OFFSET') and current_section:
            parts = line.split()
            if len(parts) >= 3:
                relocations.append({
                    'section': current_section,
                    'offset': parts[0],
                    'type': parts[1],
                    'symbol': ' '.join(parts[2:])
                })
    return relocations

def demangle_symbol(symbol):
    """Demangle C++ symbol name"""
    if not symbol.startswith('_Z'):
        return symbol
    
    cmd = f"c++filt {symbol}"
    result = run_command(cmd)
    return result if result else symbol

def categorize_symbols_enhanced(symbols):
    """Enhanced symbol categorization"""
    categories = {
        'constructors': [],
        'destructors': [],
        'methods': [],
        'operators': [],
        'vtables': [],
        'typeinfo': [],
        'templates': [],
        'static_members': [],
        'global_functions': [],
        'other': []
    }
    
    for sym in symbols:
        name = sym['name']
        demangled = sym['demangled']
        
        # Enhanced constructor detection
        if ('::~' in demangled):
            categories['destructors'].append(sym)
        elif (re.search(r'::\w+\(', demangled) and 
              any(x in name for x in ['C1E', 'C2E', 'C1I', 'C2I'])):
            categories['constructors'].append(sym)
        elif '_ZTV' in name:  # vtable
            categories['vtables'].append(sym)
        elif '_ZTI' in name or '_ZTS' in name:  # typeinfo
            categories['typeinfo'].append(sym)
        elif 'operator' in demangled:
            categories['operators'].append(sym)
        elif '<' in demangled and '>' in demangled:
            categories['templates'].append(sym)
        elif '::' in demangled and '(' in demangled:
            # Check if it's a static member
            if 'static' in demangled or re.search(r'\b[A-Z][A-Za-z]*::\w+$', demangled):
                categories['static_members'].append(sym)
            else:
                categories['methods'].append(sym)
        elif '::' not in demangled and '(' in demangled:
            categories['global_functions'].append(sym)
        else:
            categories['other'].append(sym)
    
    return categories

def extract_class_methods_enhanced(symbols, class_name):
    """Enhanced class method extraction with better pattern matching"""
    methods = []
    patterns = [
        f'{class_name}::',
        f'_ZN.*{len(class_name)}{class_name}',  # Mangled names
    ]
    
    for sym in symbols:
        demangled = sym['demangled']
        name = sym['name']
        
        # Check demangled name first
        if f'{class_name}::' in demangled:
            methods.append(sym)
        # Check mangled name patterns
        elif any(pattern in name for pattern in patterns[1:]):
            methods.append(sym)
    
    return methods

def compare_symbols_enhanced(cmake_symbols, jam_symbols):
    """Enhanced symbol comparison with better analysis"""
    cmake_names = {sym['name'] for sym in cmake_symbols}
    jam_names = {sym['name'] for sym in jam_symbols}
    
    only_cmake = cmake_names - jam_names
    only_jam = jam_names - cmake_names
    common = cmake_names & jam_names
    
    # Group by demangled names for better analysis
    cmake_demangled = {sym['demangled']: sym for sym in cmake_symbols}
    jam_demangled = {sym['demangled']: sym for sym in jam_symbols}
    
    only_cmake_demangled = set(cmake_demangled.keys()) - set(jam_demangled.keys())
    only_jam_demangled = set(jam_demangled.keys()) - set(cmake_demangled.keys())  # Fixed bug
    
    # Analyze symbol types
    cmake_by_type = defaultdict(list)
    jam_by_type = defaultdict(list)
    
    for sym in cmake_symbols:
        cmake_by_type[sym['type']].append(sym)
    for sym in jam_symbols:
        jam_by_type[sym['type']].append(sym)
    
    return {
        'cmake_only': only_cmake,
        'jam_only': only_jam,
        'common': common,
        'cmake_only_demangled': only_cmake_demangled,
        'jam_only_demangled': only_jam_demangled,
        'total_cmake': len(cmake_symbols),
        'total_jam': len(jam_symbols),
        'cmake_by_type': dict(cmake_by_type),
        'jam_by_type': dict(jam_by_type)
    }

def analyze_class_coverage_enhanced(symbols, classes):
    """Enhanced class coverage analysis"""
    coverage = {}
    for class_name in classes:
        methods = extract_class_methods_enhanced(symbols, class_name)
        
        # Categorize methods
        constructors = [m for m in methods if any(x in m['name'] for x in ['C1E', 'C2E'])]
        destructors = [m for m in methods if '::~' in m['demangled']]
        regular_methods = [m for m in methods if m not in constructors and m not in destructors]
        
        coverage[class_name] = {
            'total_count': len(methods),
            'constructors': len(constructors),
            'destructors': len(destructors),
            'methods': len(regular_methods),
            'all_methods': [m['demangled'] for m in methods]
        }
    return coverage

def generate_enhanced_report(cmake_obj, jam_obj, output_file):
    """Generate enhanced comparison report"""
    
    print(f"Analyzing {cmake_obj} vs {jam_obj}...")
    
    # Extract symbols (all types)
    cmake_text_symbols = extract_symbols(cmake_obj, "T")
    jam_text_symbols = extract_symbols(jam_obj, "T")
    
    cmake_all_symbols = extract_all_symbols(cmake_obj)
    jam_all_symbols = extract_all_symbols(jam_obj)
    
    # Extract additional info
    cmake_sections = extract_section_info(cmake_obj)
    jam_sections = extract_section_info(jam_obj)
    
    cmake_relocations = extract_relocations(cmake_obj)
    jam_relocations = extract_relocations(jam_obj)
    
    if not cmake_text_symbols or not jam_text_symbols:
        print("Error: Could not extract symbols from one or both files")
        return
    
    # Compare symbols
    text_comparison = compare_symbols_enhanced(cmake_text_symbols, jam_text_symbols)
    all_comparison = compare_symbols_enhanced(cmake_all_symbols, jam_all_symbols)
    
    # Categorize symbols
    cmake_categories = categorize_symbols_enhanced(cmake_text_symbols)
    jam_categories = categorize_symbols_enhanced(jam_text_symbols)
    
    # Analyze important classes
    important_classes = ['BApplication', 'BMessage', 'BHandler', 'BLooper', 'BMessenger', 
                        'BString', 'BList', 'BArchivable', 'BFlattenable']
    cmake_coverage = analyze_class_coverage_enhanced(cmake_text_symbols, important_classes)
    jam_coverage = analyze_class_coverage_enhanced(jam_text_symbols, important_classes)
    
    # Get file sizes
    cmake_size = os.path.getsize(cmake_obj) if os.path.exists(cmake_obj) else 0
    jam_size = os.path.getsize(jam_obj) if os.path.exists(jam_obj) else 0
    size_diff = cmake_size - jam_size
    size_pct = (size_diff / jam_size * 100) if jam_size > 0 else 0
    
    # Generate enhanced markdown report
    report = f"""# Enhanced Deep Object Comparison: {os.path.basename(cmake_obj)}

## Summary
- **CMake Object**: `{cmake_obj}`
- **Jam Object**: `{jam_obj}`
- **Analysis Date**: {run_command('date')}

## File Information
| Metric | CMake | Jam | Difference |
|--------|-------|-----|------------|
| File Size | {cmake_size:,} bytes | {jam_size:,} bytes | {size_diff:+,} bytes ({size_pct:+.2f}%) |
| Text Symbols | {text_comparison['total_cmake']} | {text_comparison['total_jam']} | {text_comparison['total_cmake'] - text_comparison['total_jam']:+d} |
| All Symbols | {all_comparison['total_cmake']} | {all_comparison['total_jam']} | {all_comparison['total_cmake'] - all_comparison['total_jam']:+d} |
| Common Text Symbols | {len(text_comparison['common'])} | {len(text_comparison['common'])} | - |
| Unique to CMake | {len(text_comparison['cmake_only'])} | - | - |
| Unique to Jam | - | {len(text_comparison['jam_only'])} | - |
| Relocations | {len(cmake_relocations)} | {len(jam_relocations)} | {len(cmake_relocations) - len(jam_relocations):+d} |

## Symbol Type Analysis
| Type | CMake Count | Jam Count | Difference | Description |
|------|-------------|-----------|------------|-------------|
"""
    
    # Analyze by symbol type
    all_types = set(text_comparison['cmake_by_type'].keys()) | set(text_comparison['jam_by_type'].keys())
    type_descriptions = {
        'T': 'Text (code) symbols',
        'D': 'Initialized data symbols', 
        'B': 'Uninitialized data symbols',
        'R': 'Read-only data symbols',
        'W': 'Weak symbols',
        'U': 'Undefined symbols'
    }
    
    for sym_type in sorted(all_types):
        cmake_count = len(text_comparison['cmake_by_type'].get(sym_type, []))
        jam_count = len(text_comparison['jam_by_type'].get(sym_type, []))
        diff = cmake_count - jam_count
        desc = type_descriptions.get(sym_type, f'Type {sym_type} symbols')
        report += f"| {sym_type} | {cmake_count} | {jam_count} | {diff:+d} | {desc} |\n"

    report += f"""
## Section Analysis
| Section | CMake Size | Jam Size | Difference |
|---------|------------|----------|------------|
"""
    
    all_sections = set(cmake_sections.keys()) | set(jam_sections.keys())
    for section in sorted(all_sections):
        cmake_size_sec = cmake_sections.get(section, {}).get('size', 0)
        jam_size_sec = jam_sections.get(section, {}).get('size', 0)
        diff = cmake_size_sec - jam_size_sec
        report += f"| {section} | {cmake_size_sec} | {jam_size_sec} | {diff:+d} |\n"

    report += f"""
## Enhanced Symbol Category Breakdown
| Category | CMake Count | Jam Count | Difference |
|----------|-------------|-----------|------------|
"""
    
    for category in cmake_categories:
        cmake_count = len(cmake_categories[category])
        jam_count = len(jam_categories[category])
        diff = cmake_count - jam_count
        report += f"| {category.replace('_', ' ').title()} | {cmake_count} | {jam_count} | {diff:+d} |\n"
    
    report += f"""
## Enhanced Core Class Analysis
"""
    
    for class_name in important_classes:
        cmake_cov = cmake_coverage[class_name]
        jam_cov = jam_coverage[class_name]
        total_diff = cmake_cov['total_count'] - jam_cov['total_count']
        
        report += f"""### {class_name}
| Metric | CMake | Jam | Difference |
|--------|-------|-----|------------|
| Total Methods | {cmake_cov['total_count']} | {jam_cov['total_count']} | {total_diff:+d} |
| Constructors | {cmake_cov['constructors']} | {jam_cov['constructors']} | {cmake_cov['constructors'] - jam_cov['constructors']:+d} |
| Destructors | {cmake_cov['destructors']} | {jam_cov['destructors']} | {cmake_cov['destructors'] - jam_cov['destructors']:+d} |
| Regular Methods | {cmake_cov['methods']} | {jam_cov['methods']} | {cmake_cov['methods'] - jam_cov['methods']:+d} |

"""
        
        if abs(total_diff) > 0:
            report += "**Method Differences:**\n"
            cmake_methods = set(cmake_cov['all_methods'])
            jam_methods = set(jam_cov['all_methods'])
            
            only_cmake = cmake_methods - jam_methods
            only_jam = jam_methods - cmake_methods
            
            if only_cmake:
                report += "- Only in CMake:\n"
                for method in sorted(only_cmake)[:10]:
                    report += f"  - `{method}`\n"
                if len(only_cmake) > 10:
                    report += f"  - ... and {len(only_cmake) - 10} more\n"
            
            if only_jam:
                report += "- Only in Jam:\n"
                for method in sorted(only_jam)[:10]:
                    report += f"  - `{method}`\n"
                if len(only_jam) > 10:
                    report += f"  - ... and {len(only_jam) - 10} more\n"
        
        report += "\n"
    
    # Missing symbols analysis
    if text_comparison['jam_only']:
        report += f"""## Missing Symbols Analysis ({len(text_comparison['jam_only'])})

"""
        jam_only_symbols = [sym for sym in jam_text_symbols if sym['name'] in text_comparison['jam_only']]
        
        # Group by likely cause
        missing_by_category = defaultdict(list)
        for sym in jam_only_symbols:
            if 'template' in sym['demangled'] or '<' in sym['demangled']:
                missing_by_category['Template instantiations'].append(sym)
            elif '::~' in sym['demangled']:
                missing_by_category['Destructors'].append(sym)
            elif any(x in sym['name'] for x in ['C1E', 'C2E']):
                missing_by_category['Constructors'].append(sym)
            elif 'operator' in sym['demangled']:
                missing_by_category['Operators'].append(sym)
            else:
                missing_by_category['Other'].append(sym)
        
        for category, syms in missing_by_category.items():
            report += f"### {category} ({len(syms)} missing)\n"
            for sym in sorted(syms, key=lambda x: x['demangled'])[:5]:
                report += f"- `{sym['demangled']}`\n"
            if len(syms) > 5:
                report += f"- ... and {len(syms) - 5} more\n"
            report += "\n"
    
    # Assessment with enhanced criteria
    missing_critical = len([s for s in jam_text_symbols 
                           if s['name'] in text_comparison['jam_only'] and 
                           any(cls in s['demangled'] for cls in important_classes)])
    
    if len(text_comparison['cmake_only']) == 0 and len(text_comparison['jam_only']) == 0:
        assessment = "PERFECT"
    elif missing_critical == 0 and len(text_comparison['jam_only']) <= 5:
        assessment = "EXCELLENT" 
    elif missing_critical == 0 and abs(size_pct) <= 5:
        assessment = "GOOD"
    else:
        assessment = "NEEDS_ATTENTION"
    
    report += f"""
## Assessment: {assessment}

### Compatibility Analysis
"""
    
    if len(text_comparison['jam_only']) == 0:
        report += "✅ **No missing symbols** - All Jam symbols present in CMake build\n"
    else:
        report += f"⚠️ **{len(text_comparison['jam_only'])} missing symbols** - CMake missing some Jam symbols\n"
    
    if missing_critical == 0:
        report += "✅ **No missing critical class methods** - All important class symbols present\n"
    else:
        report += f"⚠️ **{missing_critical} missing critical symbols** - Important class methods missing\n"
    
    if abs(size_pct) <= 1:
        report += "✅ **Excellent size match** - Less than 1% size difference\n"
    elif abs(size_pct) <= 5:
        report += "✅ **Good size match** - Less than 5% size difference\n"
    else:
        report += f"⚠️ **Size difference** - {size_pct:+.2f}% size change needs investigation\n"
    
    report += f"""
### Functional Equivalence
Based on enhanced analysis, the CMake build appears functionally {"equivalent" if assessment in ["PERFECT", "EXCELLENT"] else "mostly compatible"} to the Jam build.

**Key Metrics:**
- Symbol match rate: {(len(text_comparison['common']) / len(jam_text_symbols) * 100):.1f}%
- Critical class coverage: {((len(important_classes) - sum(1 for cls in important_classes if cmake_coverage[cls]['total_count'] != jam_coverage[cls]['total_count'])) / len(important_classes) * 100):.1f}%
- Size efficiency: {100 - abs(size_pct):.1f}%

---
*Generated by enhanced deep_compare_objects.py*
"""
    
    # Write report
    with open(output_file, 'w') as f:
        f.write(report)
    
    print(f"Enhanced report generated: {output_file}")
    print(f"Assessment: {assessment}")
    print(f"Size difference: {size_pct:+.2f}%")
    print(f"Symbol difference: {text_comparison['total_cmake'] - text_comparison['total_jam']:+d}")
    print(f"Missing critical symbols: {missing_critical}")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python3 deep_compare_objects_improved.py <cmake_obj> <jam_obj> <output_md>")
        sys.exit(1)
    
    cmake_obj = sys.argv[1]
    jam_obj = sys.argv[2] 
    output_file = sys.argv[3]
    
    generate_enhanced_report(cmake_obj, jam_obj, output_file)