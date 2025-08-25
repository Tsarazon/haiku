#!/usr/bin/env python3
"""
Deep 1-to-1 comparison of Meson vs Jam compiled object files
Analyzes symbols, sections, and actual functionality
"""

import os
import subprocess
import sys
from pathlib import Path
from datetime import datetime
import argparse

def run_command(cmd, cwd=None):
    """Run command and return output"""
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, cwd=cwd)
        return result.stdout.strip(), result.returncode
    except Exception as e:
        return str(e), 1

def get_archive_contents(archive_path):
    """Extract list of object files from archive"""
    output, rc = run_command(f"ar -t {archive_path}")
    if rc != 0:
        return []
    return [obj.strip() for obj in output.split('\n') if obj.strip()]

def extract_archive(archive_path, extract_dir):
    """Extract archive to temporary directory or get object file paths for thin archives"""
    os.makedirs(extract_dir, exist_ok=True)
    
    # Check if it's a thin archive
    file_output, _ = run_command(f"file {archive_path}")
    if "thin archive" in file_output:
        # For thin archives, get the object file paths directly
        archive_contents, rc = run_command(f"ar -tv {archive_path}")
        if rc == 0:
            object_paths = []
            for line in archive_contents.split('\n'):
                if line.strip() and '.o' in line:
                    # Extract the full path from ar -tv output (last field)
                    parts = line.split()
                    if len(parts) > 5 and parts[-1].endswith('.o'):
                        object_paths.append(parts[-1])
            return object_paths
    else:
        # Regular archive extraction
        run_command(f"cd {extract_dir} && ar -x {os.path.abspath(archive_path)}")
        run_command(f"cd {extract_dir} && for f in *.o; do [ -f \"$f\" ] && mv \"$f\" \"$(basename \"$f\")\"; done")
    
    return extract_dir

def get_symbols(obj_path):
    """Get symbols from object file using nm"""
    output, rc = run_command(f"nm -C {obj_path} 2>/dev/null")
    if rc != 0:
        return {"defined": [], "undefined": [], "weak": []}
    
    symbols = {"defined": [], "undefined": [], "weak": []}
    for line in output.split('\n'):
        if not line.strip():
            continue
        parts = line.split()
        if len(parts) >= 2:
            if parts[0] == 'U':
                symbols["undefined"].append(' '.join(parts[1:]))
            elif parts[0] == 'W' or (len(parts) >= 3 and parts[1] == 'W'):
                symbols["weak"].append(parts[-1])
            elif len(parts) >= 3 and parts[1] in ['T', 'D', 'B', 'R', 'C']:
                symbols["defined"].append(parts[-1])
    return symbols

def get_sections(obj_path):
    """Get sections from object file using objdump"""
    output, rc = run_command(f"objdump -h {obj_path} 2>/dev/null")
    if rc != 0:
        return []
    
    sections = []
    for line in output.split('\n'):
        if line.strip() and not line.startswith(' ') and '.' in line:
            parts = line.split()
            if len(parts) >= 7 and parts[1].startswith('.'):
                sections.append({
                    "name": parts[1],
                    "size": parts[2],
                    "vma": parts[3]
                })
    return sections

def get_function_signatures(obj_path):
    """Get function signatures and types using objdump disassembly"""
    output, rc = run_command(f"objdump -t {obj_path} 2>/dev/null")
    if rc != 0:
        return {"functions": [], "data": [], "total_functions": 0}
    
    functions = []
    data_symbols = []
    for line in output.split('\n'):
        if line.strip() and 'F' in line and '.text' in line:
            # Function symbol
            parts = line.split()
            if len(parts) >= 6:
                functions.append(parts[-1])  # function name
        elif line.strip() and 'O' in line and ('.data' in line or '.bss' in line):
            # Data symbol
            parts = line.split()
            if len(parts) >= 6:
                data_symbols.append(parts[-1])
    
    return {
        "functions": functions,
        "data": data_symbols, 
        "total_functions": len(functions),
        "total_data": len(data_symbols)
    }

def get_dependencies(obj_path):
    """Get external dependencies using readelf"""
    output, rc = run_command(f"readelf -s {obj_path} 2>/dev/null")
    if rc != 0:
        return {"external_deps": [], "internal_calls": []}
    
    external_deps = []
    internal_calls = []
    
    for line in output.split('\n'):
        if 'UND' in line and 'FUNC' in line:
            # Undefined function - external dependency
            parts = line.split()
            if len(parts) >= 8:
                external_deps.append(parts[-1])
        elif 'FUNC' in line and 'GLOBAL' in line:
            # Defined global function - internal API
            parts = line.split()
            if len(parts) >= 8:
                internal_calls.append(parts[-1])
    
    return {
        "external_deps": external_deps,
        "internal_calls": internal_calls
    }

def compare_objects(meson_obj, jam_obj, verbose=False):
    """Deep comparison of two object files"""
    comparison = {
        "meson_size": os.path.getsize(meson_obj) if os.path.exists(meson_obj) else 0,
        "jam_size": os.path.getsize(jam_obj) if os.path.exists(jam_obj) else 0,
        "size_diff": 0,
        "symbols_match": False,
        "sections_match": False,
        "functions_match": False,
        "dependencies_match": False,
        "details": {}
    }
    
    if not os.path.exists(meson_obj) or not os.path.exists(jam_obj):
        comparison["error"] = "One or both files missing"
        return comparison
    
    comparison["size_diff"] = comparison["jam_size"] - comparison["meson_size"]
    
    # Compare symbols
    meson_symbols = get_symbols(meson_obj)
    jam_symbols = get_symbols(jam_obj)
    
    comparison["details"]["symbols"] = {
        "meson_defined": len(meson_symbols["defined"]),
        "jam_defined": len(jam_symbols["defined"]),
        "meson_undefined": len(meson_symbols["undefined"]),
        "jam_undefined": len(jam_symbols["undefined"]),
        "meson_weak": len(meson_symbols["weak"]),
        "jam_weak": len(jam_symbols["weak"]),
        "missing_in_meson": list(set(jam_symbols["defined"]) - set(meson_symbols["defined"])),
        "extra_in_meson": list(set(meson_symbols["defined"]) - set(jam_symbols["defined"])),
        "undefined_diff": list(set(jam_symbols["undefined"]) - set(meson_symbols["undefined"]))
    }
    
    comparison["symbols_match"] = (
        set(meson_symbols["defined"]) == set(jam_symbols["defined"])
    )
    
    # Compare sections
    meson_sections = get_sections(meson_obj)
    jam_sections = get_sections(jam_obj)
    
    comparison["details"]["sections"] = {
        "meson_sections": [s["name"] for s in meson_sections],
        "jam_sections": [s["name"] for s in jam_sections],
        "meson_count": len(meson_sections),
        "jam_count": len(jam_sections)
    }
    
    comparison["sections_match"] = (
        set(s["name"] for s in meson_sections) == set(s["name"] for s in jam_sections)
    )
    
    # Compare function signatures and types
    meson_functions = get_function_signatures(meson_obj)
    jam_functions = get_function_signatures(jam_obj)
    
    comparison["details"]["functions"] = {
        "meson_total": meson_functions["total_functions"],
        "jam_total": jam_functions["total_functions"],
        "meson_data": meson_functions["total_data"],
        "jam_data": jam_functions["total_data"],
        "missing_functions": list(set(jam_functions["functions"]) - set(meson_functions["functions"])),
        "extra_functions": list(set(meson_functions["functions"]) - set(jam_functions["functions"])),
        "missing_data": list(set(jam_functions["data"]) - set(meson_functions["data"])),
        "extra_data": list(set(meson_functions["data"]) - set(jam_functions["data"]))
    }
    
    comparison["functions_match"] = (
        set(meson_functions["functions"]) == set(jam_functions["functions"]) and
        set(meson_functions["data"]) == set(jam_functions["data"])
    )
    
    # Compare dependencies
    meson_deps = get_dependencies(meson_obj)
    jam_deps = get_dependencies(jam_obj)
    
    comparison["details"]["dependencies"] = {
        "meson_external": len(meson_deps["external_deps"]),
        "jam_external": len(jam_deps["external_deps"]),
        "meson_internal": len(meson_deps["internal_calls"]),
        "jam_internal": len(jam_deps["internal_calls"]),
        "missing_external": list(set(jam_deps["external_deps"]) - set(meson_deps["external_deps"])),
        "extra_external": list(set(meson_deps["external_deps"]) - set(jam_deps["external_deps"])),
        "missing_internal": list(set(jam_deps["internal_calls"]) - set(meson_deps["internal_calls"])),
        "extra_internal": list(set(meson_deps["internal_calls"]) - set(jam_deps["internal_calls"]))
    }
    
    comparison["dependencies_match"] = (
        set(meson_deps["external_deps"]) == set(jam_deps["external_deps"]) and
        set(meson_deps["internal_calls"]) == set(jam_deps["internal_calls"])
    )
    
    return comparison

def compare_module(module_name, meson_base="/home/ruslan/haiku/builddir", 
                  jam_base="/home/ruslan/haiku/generated/objects/haiku/x86_64/release",
                  verbose=False):
    """Compare a complete module between Meson and Jam"""
    
    print(f"\n{'='*80}")
    print(f"Analyzing module: {module_name}")
    print(f"{'='*80}")
    
    # Find Meson archive
    meson_archive = Path(meson_base) / "src/system/libroot/posix" / module_name / f"libposix_{module_name}.a"
    if not meson_archive.exists():
        print(f"❌ Meson archive not found: {meson_archive}")
        return None
    
    # Find Jam objects directory  
    jam_dir = Path(jam_base) / "system/libroot/posix" / module_name
    if not jam_dir.exists():
        print(f"❌ Jam directory not found: {jam_dir}")
        return None
    
    print(f"✅ Meson archive: {meson_archive}")
    print(f"✅ Jam directory: {jam_dir}")
    
    # Extract Meson archive or get thin archive paths
    temp_dir = Path(f"/tmp/meson_{module_name}_{os.getpid()}")
    extracted = extract_archive(meson_archive, temp_dir)
    
    # Get object files
    if isinstance(extracted, list):
        # Thin archive - we have direct paths to object files
        meson_objects = [Path(path) for path in extracted if Path(path).exists()]
    else:
        # Regular extraction - get files from temp directory
        meson_objects = list(temp_dir.glob("*.o")) + list(temp_dir.glob("**/*.o"))
    
    jam_objects = list(jam_dir.rglob("*.o"))
    
    print(f"\n📊 Object files count:")
    print(f"  Meson: {len(meson_objects)} objects")
    print(f"  Jam:   {len(jam_objects)} objects")
    
    # Match objects by name
    results = {
        "module": module_name,
        "meson_count": len(meson_objects),
        "jam_count": len(jam_objects),
        "matches": [],
        "missing_in_meson": [],
        "extra_in_meson": [],
        "comparison_details": []
    }
    
    # Normalize object names for comparison (remove source extensions)
    def normalize_name(name):
        # Convert "reallocarray.cpp.o" to "reallocarray.o"  
        if '.c.o' in name:
            return name.replace('.c.o', '.o')
        elif '.cpp.o' in name:
            return name.replace('.cpp.o', '.o')
        elif '.cc.o' in name:
            return name.replace('.cc.o', '.o')
        return name
    
    meson_names = {normalize_name(obj.name): obj for obj in meson_objects}
    jam_names = {obj.name: obj for obj in jam_objects}
    
    # Find common objects
    common_names = set(meson_names.keys()) & set(jam_names.keys())
    results["missing_in_meson"] = list(set(jam_names.keys()) - set(meson_names.keys()))
    results["extra_in_meson"] = list(set(meson_names.keys()) - set(jam_names.keys()))
    
    print(f"\n🔍 Detailed comparison of {len(common_names)} common objects:")
    
    for obj_name in sorted(common_names):
        meson_obj = meson_names[obj_name]
        jam_obj = jam_names[obj_name]
        
        comp = compare_objects(meson_obj, jam_obj, verbose)
        results["comparison_details"].append({
            "name": obj_name,
            "comparison": comp
        })
        
        # Print summary for each object with functional analysis
        symbols_ok = comp["symbols_match"]
        functions_ok = comp["functions_match"] 
        deps_ok = comp["dependencies_match"]
        
        if symbols_ok and functions_ok and deps_ok:
            status = "✅"  # Perfect functional match
        elif symbols_ok and functions_ok:
            status = "🔶"  # Good functional match but dependency differences
        elif symbols_ok:
            status = "⚠️"   # Symbol match but function differences
        else:
            status = "❌"   # Major differences
            
        size_diff = comp["size_diff"]
        size_indicator = f"({'smaller' if size_diff > 0 else 'larger'} by {abs(size_diff)} bytes)"
        
        print(f"  {status} {obj_name:40} Meson: {comp['meson_size']:6} Jam: {comp['jam_size']:6} {size_indicator}")
        
        if verbose or not (symbols_ok and functions_ok and deps_ok):
            # Show functional differences
            if comp["details"]["functions"]["missing_functions"]:
                print(f"     Missing functions: {', '.join(comp['details']['functions']['missing_functions'][:3])}")
            if comp["details"]["functions"]["extra_functions"]:
                print(f"     Extra functions: {', '.join(comp['details']['functions']['extra_functions'][:3])}")
            if comp["details"]["dependencies"]["missing_external"]:
                print(f"     Missing deps: {', '.join(comp['details']['dependencies']['missing_external'][:3])}")
            if comp["details"]["symbols"]["missing_in_meson"]:
                print(f"     Missing symbols: {', '.join(comp['details']['symbols']['missing_in_meson'][:3])}")
            if comp["details"]["symbols"]["extra_in_meson"]:
                print(f"     Extra symbols: {', '.join(comp['details']['symbols']['extra_in_meson'][:3])}")
    
    if results["missing_in_meson"]:
        print(f"\n⚠️  Objects missing in Meson build:")
        for obj in results["missing_in_meson"]:
            print(f"    - {obj}")
    
    if results["extra_in_meson"]:
        print(f"\n⚠️  Extra objects in Meson build:")
        for obj in results["extra_in_meson"]:
            print(f"    - {obj}")
    
    # Summary statistics with functional analysis
    total_symbol_matches = sum(1 for d in results["comparison_details"] if d["comparison"]["symbols_match"])
    total_function_matches = sum(1 for d in results["comparison_details"] if d["comparison"]["functions_match"])
    total_dependency_matches = sum(1 for d in results["comparison_details"] if d["comparison"]["dependencies_match"])
    perfect_matches = sum(1 for d in results["comparison_details"] 
                         if d["comparison"]["symbols_match"] and 
                            d["comparison"]["functions_match"] and 
                            d["comparison"]["dependencies_match"])
    
    print(f"\n📈 Functional Analysis Summary:")
    print(f"  Perfect functional matches: {perfect_matches}/{len(common_names)}")
    print(f"  Symbol-perfect matches: {total_symbol_matches}/{len(common_names)}")
    print(f"  Function matches: {total_function_matches}/{len(common_names)}")
    print(f"  Dependency matches: {total_dependency_matches}/{len(common_names)}")
    print(f"  Total size difference: {sum(d['comparison']['size_diff'] for d in results['comparison_details'])} bytes")
    
    # ALERT SYSTEM - Check for any deviations from perfect match
    alerts = []
    
    if perfect_matches != len(common_names):
        alerts.append(f"🚨 FUNCTIONAL MISMATCH: {len(common_names) - perfect_matches} objects have functional differences!")
    
    if total_symbol_matches != len(common_names):
        alerts.append(f"🚨 SYMBOL MISMATCH: {len(common_names) - total_symbol_matches} objects have symbol differences!")
        
    if total_function_matches != len(common_names):
        alerts.append(f"🚨 FUNCTION MISMATCH: {len(common_names) - total_function_matches} objects have function differences!")
        
    if total_dependency_matches != len(common_names):
        alerts.append(f"🚨 DEPENDENCY MISMATCH: {len(common_names) - total_dependency_matches} objects have dependency differences!")
    
    if len(results["missing_in_meson"]) > 0:
        alerts.append(f"🚨 MISSING OBJECTS: {len(results['missing_in_meson'])} objects missing in Meson build!")
        
    if len(results["extra_in_meson"]) > 0:
        alerts.append(f"⚠️ EXTRA OBJECTS: {len(results['extra_in_meson'])} extra objects in Meson build")
    
    total_size_diff = sum(d['comparison']['size_diff'] for d in results['comparison_details'])
    if abs(total_size_diff) > 1000:  # Alert if size difference > 1KB
        alerts.append(f"🚨 LARGE SIZE DIFFERENCE: {total_size_diff} bytes total difference!")
    elif total_size_diff != 0:
        alerts.append(f"⚠️ SIZE DIFFERENCE: {total_size_diff} bytes total difference")
    
    # Display alerts
    if alerts:
        print(f"\n🔔 ALERTS:")
        for alert in alerts:
            print(f"  {alert}")
        
        # Critical vs Warning classification
        critical_alerts = [a for a in alerts if "🚨" in a]
        warning_alerts = [a for a in alerts if "⚠️" in a]
        
        if critical_alerts:
            print(f"\n❌ RESULT: CRITICAL ISSUES DETECTED ({len(critical_alerts)} critical, {len(warning_alerts)} warnings)")
            print("  Action required: Fix functional differences before deployment")
        else:
            print(f"\n⚠️ RESULT: WARNINGS ONLY ({len(warning_alerts)} warnings)")
            print("  Status: Generally acceptable but review recommended")
    else:
        print(f"\n✅ RESULT: PERFECT MATCH")
        print("  Status: Meson build is functionally identical to Jam build")
    
    # Cleanup
    run_command(f"rm -rf {temp_dir}")
    
    return results

def main():
    parser = argparse.ArgumentParser(description="Deep 1-to-1 comparison of Meson vs Jam object files")
    parser.add_argument("module", help="Module name to compare (e.g., malloc, musl, stdlib)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show detailed symbol differences")
    parser.add_argument("--meson-base", default="/home/ruslan/haiku/builddir", help="Meson build directory")
    parser.add_argument("--jam-base", default="/home/ruslan/haiku/generated/objects/haiku/x86_64/release", help="Jam build directory")
    
    args = parser.parse_args()
    
    print(f"🔬 Deep Object Comparison Tool")
    print(f"📅 {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    
    results = compare_module(args.module, args.meson_base, args.jam_base, args.verbose)
    
    if results:
        # Generate detailed report
        report_path = Path(f"/home/ruslan/haiku/comparison-analysis/{args.module}_deep_comparison.md")
        # Calculate statistics for report
        total_symbol_matches = sum(1 for d in results["comparison_details"] if d["comparison"]["symbols_match"])
        total_function_matches = sum(1 for d in results["comparison_details"] if d["comparison"]["functions_match"])
        total_dependency_matches = sum(1 for d in results["comparison_details"] if d["comparison"]["dependencies_match"])
        perfect_matches = sum(1 for d in results["comparison_details"] 
                             if d["comparison"]["symbols_match"] and 
                                d["comparison"]["functions_match"] and 
                                d["comparison"]["dependencies_match"])
        
        with open(report_path, 'w') as f:
            f.write(f"# Deep Comparison Report: {args.module}\n\n")
            f.write(f"Generated: {datetime.now()}\n\n")
            
            # ALERT SECTION in report
            f.write(f"## 🔔 ALERT STATUS\n\n")
            
            alerts_for_report = []
            common_count = len(results['comparison_details'])
            
            if perfect_matches != common_count:
                alerts_for_report.append(f"🚨 FUNCTIONAL MISMATCH: {common_count - perfect_matches} objects have functional differences!")
            if total_symbol_matches != common_count:
                alerts_for_report.append(f"🚨 SYMBOL MISMATCH: {common_count - total_symbol_matches} objects have symbol differences!")
            if total_function_matches != common_count:
                alerts_for_report.append(f"🚨 FUNCTION MISMATCH: {common_count - total_function_matches} objects have function differences!")
            if total_dependency_matches != common_count:
                alerts_for_report.append(f"🚨 DEPENDENCY MISMATCH: {common_count - total_dependency_matches} objects have dependency differences!")
            if len(results["missing_in_meson"]) > 0:
                alerts_for_report.append(f"🚨 MISSING OBJECTS: {len(results['missing_in_meson'])} objects missing in Meson build!")
            if len(results["extra_in_meson"]) > 0:
                alerts_for_report.append(f"⚠️ EXTRA OBJECTS: {len(results['extra_in_meson'])} extra objects in Meson build")
            
            total_size_diff = sum(d['comparison']['size_diff'] for d in results['comparison_details'])
            if abs(total_size_diff) > 1000:
                alerts_for_report.append(f"🚨 LARGE SIZE DIFFERENCE: {total_size_diff} bytes total difference!")
            elif total_size_diff != 0:
                alerts_for_report.append(f"⚠️ SIZE DIFFERENCE: {total_size_diff} bytes total difference")
            
            if alerts_for_report:
                f.write("**ALERTS DETECTED:**\n\n")
                for alert in alerts_for_report:
                    f.write(f"- {alert}\n")
                
                critical_alerts = [a for a in alerts_for_report if "🚨" in a]
                if critical_alerts:
                    f.write(f"\n**STATUS: ❌ CRITICAL ISSUES** ({len(critical_alerts)} critical issues)\n")
                    f.write("Action required: Fix functional differences before deployment\n\n")
                else:
                    f.write(f"\n**STATUS: ⚠️ WARNINGS ONLY** ({len(alerts_for_report)} warnings)\n")
                    f.write("Status: Generally acceptable but review recommended\n\n")
            else:
                f.write("**STATUS: ✅ PERFECT MATCH**\n")
                f.write("Meson build is functionally identical to Jam build\n\n")
            
            f.write(f"## Summary\n")
            f.write(f"- Meson objects: {results['meson_count']}\n")
            f.write(f"- Jam objects: {results['jam_count']}\n")
            f.write(f"- Common objects: {len(results['comparison_details'])}\n")
            f.write(f"- Perfect functional matches: {perfect_matches}/{common_count}\n")
            f.write(f"- Symbol matches: {total_symbol_matches}/{common_count}\n")
            f.write(f"- Function matches: {total_function_matches}/{common_count}\n")
            f.write(f"- Dependency matches: {total_dependency_matches}/{common_count}\n")
            f.write(f"- Missing in Meson: {len(results['missing_in_meson'])}\n")
            f.write(f"- Extra in Meson: {len(results['extra_in_meson'])}\n\n")
            
            f.write(f"## Object-by-Object Functional Analysis\n\n")
            for detail in results["comparison_details"]:
                comp = detail["comparison"]
                f.write(f"### {detail['name']}\n")
                f.write(f"- Size: Meson {comp['meson_size']} vs Jam {comp['jam_size']} (diff: {comp['size_diff']})\n")
                f.write(f"- **Functional Match**: Symbols={comp['symbols_match']}, Functions={comp['functions_match']}, Dependencies={comp['dependencies_match']}\n")
                f.write(f"- Symbols: Meson {comp['details']['symbols']['meson_defined']} vs Jam {comp['details']['symbols']['jam_defined']}\n")
                f.write(f"- Functions: Meson {comp['details']['functions']['meson_total']} vs Jam {comp['details']['functions']['jam_total']}\n")
                f.write(f"- External deps: Meson {comp['details']['dependencies']['meson_external']} vs Jam {comp['details']['dependencies']['jam_external']}\n")
                
                if comp['details']['functions']['missing_functions']:
                    f.write(f"- Missing functions: {', '.join(comp['details']['functions']['missing_functions'])}\n")
                if comp['details']['functions']['extra_functions']:
                    f.write(f"- Extra functions: {', '.join(comp['details']['functions']['extra_functions'])}\n")
                if comp['details']['dependencies']['missing_external']:
                    f.write(f"- Missing dependencies: {', '.join(comp['details']['dependencies']['missing_external'])}\n")
                if comp['details']['symbols']['missing_in_meson']:
                    f.write(f"- Missing symbols: {', '.join(comp['details']['symbols']['missing_in_meson'])}\n")
                if comp['details']['symbols']['extra_in_meson']:
                    f.write(f"- Extra symbols: {', '.join(comp['details']['symbols']['extra_in_meson'])}\n")
                f.write("\n")
            
            if results['missing_in_meson']:
                f.write(f"## Objects Missing in Meson\n")
                for obj in results['missing_in_meson']:
                    f.write(f"- {obj}\n")
                f.write("\n")
        
        print(f"\n📄 Detailed report saved to: {report_path}")
    
    return 0 if results else 1

if __name__ == "__main__":
    sys.exit(main())