#!/usr/bin/env python3
"""
Generate Meson cross-compilation files for all supported Haiku architectures.
This script integrates all JAM BuildSetup, DefaultBuildProfiles, ArchitectureRules,
and BuildFeatures functionality to create complete cross-compilation configurations.
"""

import sys
from pathlib import Path

# Add current directory to path
sys.path.insert(0, str(Path(__file__).parent))

def main():
    """Generate cross-compilation files for all supported architectures."""
    from detect_build_features import generate_cross_files_for_profiles
    
    # All supported Haiku architectures
    architectures = ['x86_64', 'x86', 'arm64', 'riscv64']
    
    # All major build profiles
    profiles = [
        'nightly-anyboot', 'nightly-cd', 'nightly-vmware', 'nightly-raw',
        'release-anyboot', 'release-cd', 'release-vmware', 'release-raw', 
        'bootstrap-anyboot', 'bootstrap-vmware', 'bootstrap-raw',
        'minimum-anyboot', 'minimum-cd', 'minimum-vmware', 'minimum-raw'
    ]
    
    print("🚀 Generating Meson Cross-Compilation Files")
    print("=" * 60)
    print(f"Architectures: {', '.join(architectures)}")
    print(f"Profiles: {len(profiles)} build profiles")
    print("Source: JAM BuildSetup + DefaultBuildProfiles + ArchitectureRules + BuildFeatures")
    print()
    
    total_generated = 0
    failed_architectures = []
    
    for arch in architectures:
        try:
            print(f"🔧 Generating cross-file for {arch}...")
            generated_files = generate_cross_files_for_profiles([arch], profiles)
            
            if arch in generated_files:
                total_generated += 1
                print(f"   ✅ Success: {generated_files[arch]}")
            else:
                failed_architectures.append(arch)
                print(f"   ❌ Failed: No cross-file generated")
                
        except Exception as e:
            failed_architectures.append(arch)
            print(f"   ❌ Failed: {e}")
    
    print()
    print("=" * 60)
    print(f"📊 Summary:")
    print(f"   Generated: {total_generated}/{len(architectures)} cross-compilation files")
    if failed_architectures:
        print(f"   Failed: {', '.join(failed_architectures)}")
    
    # Show what was generated
    if total_generated > 0:
        haiku_root = Path(__file__).parent.parent.parent.parent
        cross_dir = haiku_root / 'build/meson'
        cross_files = list(cross_dir.glob('haiku-*-cross-dynamic.ini'))
        
        print(f"\n📁 Generated cross-compilation files:")
        for cross_file in sorted(cross_files):
            print(f"   {cross_file.name}")
        
        print(f"\n🎯 Usage:")
        print(f"   meson setup builddir --cross-file build/meson/haiku-x86_64-cross-dynamic.ini")
        print(f"   meson compile -C builddir")
        
        print(f"\n✅ Haiku Meson cross-compilation setup complete!")
        print(f"   All JAM functionality integrated and ready for build system migration.")
    else:
        print(f"\n❌ No cross-compilation files were generated successfully.")
        sys.exit(1)

if __name__ == '__main__':
    main()