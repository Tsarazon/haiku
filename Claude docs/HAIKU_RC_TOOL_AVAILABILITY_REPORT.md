# Haiku RC Tool Availability Report

## System PATH Check

**Command:** `which rc`  
**Result:** `rc` command not found in system PATH

**Command:** `rc --version`  
**Result:** `/bin/bash: строка 1: rc: команда не найдена`

## Haiku Tree Search Results

### Primary RC Tool Found

**Location:** `/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/rc/rc`

**Version Information:**
```
Haiku Resource Compiler 1.1

Copyright (c) 2003 Matthijs Hollemans

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

### Complete Tool Set Found

**RC Tool:** `/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/rc/rc`  
**XRES Tool:** `/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/xres`  
**Additional Tools:** `/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/resattr`

### Candidate RC-Related Paths in Haiku Tree

```
/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/rc/rc
/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/xres
/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/resattr
/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/data_to_source
```

## Installation Recommendation

Since the `rc` tool is **NOT available in system PATH** but **IS available in the Haiku build tree**, here are the installation options:

### Option 1: System Installation (Recommended)

```bash
# Copy rc tool to system location
sudo cp /home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/rc/rc /usr/local/bin/rc
sudo cp /home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/xres /usr/local/bin/xres
sudo chmod +x /usr/local/bin/rc /usr/local/bin/xres

# Verify installation
which rc
rc --version
```

### Option 2: Meson Configuration with Custom Path

Add to your Meson project's `meson_options.txt`:

```meson
option('haiku_rc_path', type: 'string', 
       value: '/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/rc/rc',
       description: 'Path to Haiku resource compiler (rc) tool')

option('haiku_xres_path', type: 'string',
       value: '/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/xres', 
       description: 'Path to Haiku resource embedding (xres) tool')
```

Then in your `meson.build`:

```meson
# Get rc tool path from option or find in system
rc_path = get_option('haiku_rc_path')
if rc_path == ''
    rc_tool = find_program('rc', required: true)
else
    rc_tool = find_program(rc_path, required: true)
endif

xres_path = get_option('haiku_xres_path')
if xres_path == ''
    xres_tool = find_program('xres', required: true)
else
    xres_tool = find_program(xres_path, required: true)
endif
```

Usage:

```bash
# Configure with custom tool paths
meson setup builddir -Dhaiku_rc_path=/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/rc/rc \
                     -Dhaiku_xres_path=/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/xres

# Or use default paths if tools are in PATH
meson setup builddir
```

### Option 3: Environment Variables

```bash
# Set environment variables
export HAIKU_RC_PATH="/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/rc/rc"
export HAIKU_XRES_PATH="/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/xres"

# Then in meson.build:
rc_tool = find_program(get_env('HAIKU_RC_PATH', 'rc'), required: true)
xres_tool = find_program(get_env('HAIKU_XRES_PATH', 'xres'), required: true)
```

## Conclusion

**Status:** ✅ **HAIKU RC TOOLS FOUND** but not in system PATH

**Recommendation:** Use **Option 1** (system installation) for simplicity, or **Option 2** (Meson options) for development flexibility.

The Haiku Resource Compiler (rc) v1.1 and associated tools (xres, resattr) are available in the Haiku build tree and ready for use in Meson-based resource processing workflows.

---

**Generated:** August 16, 2025  
**RC Tool Version:** Haiku Resource Compiler 1.1  
**Location:** `/home/ruslan/haiku/generated/objects/linux/x86_64/release/tools/`