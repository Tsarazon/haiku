# Haiku Meson Build System - Commands Reference

**FOR CLAUDE: Use these commands instead of guessing or deleting builddir**

## 🔧 **Essential Meson Commands**

### **Setup and Configuration:**
```bash
# Initial setup (only once)
cd /home/ruslan/haiku
meson setup builddir --cross-file build/meson/haiku-x86_64-cross.ini

# Reconfigure without deleting builddir (USE THIS!)
meson setup --clearcache --reconfigure builddir

# Alternative reconfigure method
meson configure builddir
```

### **Building:**
```bash
# Build all targets
meson compile -C builddir

# Build specific target
meson compile -C builddir target_name

# Clean build (without deleting builddir)
meson compile --clean -C builddir
meson compile -C builddir

# Verbose build output
meson compile -C builddir -v
```

### **Information and Debugging:**
```bash
# List all targets
meson introspect builddir --targets

# Show build options
meson configure builddir

# Show dependencies
meson introspect builddir --dependencies

# Test configuration
meson test -C builddir
```

## 📂 **Directory Structure (NEVER DELETE THESE):**

```
/home/ruslan/haiku/
├── builddir/                          # Meson build directory (KEEP THIS)
│   ├── build.ninja                    # Generated build files
│   ├── meson-*                        # Meson internal files
│   └── src/                           # Built objects and libraries
│       └── kits/                      # Kit object files and libbe.so
├── build/meson/                       # Configuration files
│   ├── haiku-x86_64-cross.ini        # Cross-compilation setup
│   └── modules/HaikuCommon.py         # Custom configuration
└── src/kits/                          # Source with meson.build files
```

## 🎯 **Kit Build Commands:**

```bash
# Build individual kits
meson compile -C builddir src/kits/app/app_kit.o
meson compile -C builddir src/kits/storage/storage_kit.o  
meson compile -C builddir src/kits/support/support_kit.o
meson compile -C builddir src/kits/interface/interface_kit.o
meson compile -C builddir src/kits/locale/locale_kit.o

# Build libbe.so (currently broken)
meson compile -C builddir src/kits/libbe.so.1.0.0
```

## ❌ **NEVER DO THESE:**

```bash
# DON'T DELETE BUILD DIRECTORY
rm -rf builddir                        # ❌ WRONG!

# DON'T USE NINJA DIRECTLY
ninja -C builddir                      # ❌ Use meson compile instead
```

## 🔍 **Debugging Commands:**

```bash
# Check file sizes
ls -la /home/ruslan/haiku/builddir/src/kits/*.o
ls -la /home/ruslan/haiku/builddir/src/kits/*/*.o

# Check libraries
ls -la /home/ruslan/haiku/builddir/src/kits/libbe.so*

# Check symbols
nm file.so | wc -l                     # Count symbols
objdump -h file.so                     # Check sections

# Find files
find /home/ruslan/haiku/builddir -name "*_kit.o" -type f -exec ls -la {} \;
find /home/ruslan/haiku/builddir -name "libbe.so*" -type f
```

## 🚨 **Current Status:**

**WORKING:**
- ✅ Kit compilation (all 5 kits compile to 41MB total)
- ✅ Cross-compilation with Haiku toolchain  
- ✅ Template control and resource compilation

**BROKEN:**
- ❌ libbe.so linking (creates 4.6KB empty stub instead of 4.6MB library)
- ❌ Meson `dependencies` parameter doesn't include object contents
- ❌ `objects` parameter doesn't work with custom_target outputs

## 🎯 **Next Steps:**

1. Research proper Meson approach for linking custom_target objects
2. Consider alternative architectures (static libraries, etc.)
3. Investigate Meson limitations with complex object linking

---

*Created: August 23, 2025*  
*Last Updated: August 26, 2025 - Removed outdated /generated_meson/ references*