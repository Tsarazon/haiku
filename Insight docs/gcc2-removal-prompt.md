# Remove GCC2 and BeOS Binary Compatibility from Haiku

Remove gcc2 compiler support and BeOS binary compatibility from the Haiku codebase without impacting the operability or stability of the overall operating system.

## Instructions

### Initial Checklist
Begin with this conceptual checklist:
- Understand the relationship between gcc2 and BeOS binary compatibility in the codebase
- Analyze how hybrid builds currently function with both gcc2 and modern gcc
- Identify the architectural boundaries between gcc2-specific and shared code
- Map dependencies that might break when gcc2 is removed
- Plan the removal sequence to maintain build system integrity throughout

### Critical Requirement: Contextual Understanding
**DO NOT mechanically delete code containing "gcc2" or "x86_gcc2" strings.**
- Read and understand each code section before removal to determine:
  - Whether the code is gcc2-exclusive or shared with other architectures
  - If removal would break functionality for modern gcc builds
  - Whether the code contains fallback logic that should be preserved
  - If comments explain historical decisions that inform the removal strategy
- For conditional compilation blocks (#if __GNUC__ == 2), analyze both branches to understand what functionality differs and preserve the modern path correctly
- When encountering Reserved functions for ABI compatibility, understand their purpose and verify no modern code relies on these vtable entries

### Execution Steps
1. **Discovery Phase**
   - Create `gcc2-removal.md` to document all findings and decisions
   - Identify all gcc2-related code paths, distinguishing between:
     - Build system configuration (configure scripts, Jam rules)
     - Source code compatibility layers (ABI hacks, Reserved functions)  
     - Compiler-specific workarounds vs. architectural requirements
   - Document the purpose of each gcc2 component before removal

2. **Build System Modification**
   - Remove gcc2 architecture support from configure and Jam build rules
   - Understand how hybrid builds work before dismantling them
   - Preserve build logic that applies to all architectures
   - After each modification, validate: "Build configuration still generates valid rules for x86/x86_64"

3. **Source Code Cleanup**
   - For each gcc2 conditional block, read both code paths to understand differences
   - Remove binary compatibility functions only after verifying no internal usage
   - Update headers while preserving API compatibility for modern builds
   - After each file modification, validate: "File compiles with modern gcc and maintains expected behavior"

4. **Buildtools Repository**
   - Remove legacy gcc 2.95.3 compiler and associated build scripts
   - Understand the relationship between buildtools and main repository
   - Validate: "Buildtools can still build modern cross-compilation toolchain"

5. **Package Management Updates**
   - Remove x86_gcc2 as a supported architecture
   - Understand package dependency chains before removal
   - Validate: "Package system recognizes and builds only modern architectures"

6. **Verification**
   - Run comprehensive tests on x86 and x86_64 architectures
   - Build complete system image without gcc2
   - Validate: "System boots and core functionality works without gcc2"

### Documentation Requirements
For each code removal, document in `gcc2-removal.md`:
- The original purpose of the removed code
- Why it's safe to remove (not just "contains gcc2")
- Any behavioral changes resulting from removal
- Potential risks identified during code analysis

## Out of Scope
- Do not refactor code unrelated to gcc2/BeOS compatibility
- Do not modify support for other architectures (x86_64, ARM, RISC-V, etc.)
- Do not change modern gcc version or C++ standard requirements
- Do not alter source-level BeOS API compatibility (only binary compatibility)
- No need to update HaikuPorts recipes (separate repository)

## Output Format
Provide:
1. **Executive Summary**: High-level description of what was removed and why
2. **Affected Subsystems**: 
   - Build system changes (configure, Jam rules)
   - Source code modifications (by component/kit)
   - Buildtools modifications
   - Package management updates
3. **Code Analysis Insights**: Notable discoveries about how gcc2 support was implemented
4. **Risk Assessment**: Any concerns discovered during contextual code reading
5. **Blockers**: Manual interventions needed or unresolved dependencies
6. **Test Results**: Summary of verification tests performed

## Stop Conditions
Stop when:
- All gcc2 and x86_gcc2 references are removed (except documentation/changelogs)
- Build system no longer accepts gcc2 configuration options
- System builds successfully with only modern gcc
- All tests pass on x86 and x86_64 without regressions
- Complete documentation in `gcc2-removal.md` explains all changes with context

## Validation Checkpoints
After each significant change:
- "Does this removal break any non-gcc2 functionality?" 
- "Have I understood why this code existed before removing it?"
- "Is the modern code path correctly preserved?"
- Self-correct if any validation fails before proceeding.

## Final Verification
Before declaring completion:
- No mechanical deletions without understanding
- Each removal decision is documented with rationale
- System remains stable for all supported architectures
- Build completes without gcc2 references in the binary