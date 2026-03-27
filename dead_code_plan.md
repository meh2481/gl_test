## Plan: High-Confidence C++ Dead Code Report

Produce a conservative, report-only dead-code scan focused on C++ symbols in src/, limited to high-confidence candidates that are provably unused in current scripts/call paths. No deletions in this pass.

**Steps**
1. Consolidate proven high-confidence candidates from discovery into a single report list, with evidence per symbol (declaration, implementation, registration, and absence of call sites).
2. Validate each candidate against runtime entry points and dynamic boundaries: main entry path, Lua registration table, and scene scripts that can invoke exposed APIs.
3. Classify candidates by risk:
- Safe report candidates: no call sites in C++ or Lua scripts, only registration/definition references.
- Excluded for now: template/inline methods, debug-only code paths, and anything with potential external/plugin invocation.
4. Produce report sections:
- Confirmed high-confidence unused symbols
- Evidence links
- Exclusions and rationale
- Recommended next deletion batch (separate execution phase)
5. Add verification checklist for the later deletion phase:
- Build command to run after removals
- Grep checks to ensure no orphaned declarations/registrations remain
- Runtime smoke checks for scene loading and Lua binding initialization

**Relevant files**
- /home/mark/repos/gl_test/src/core/String.h — declaration for utf8Length candidate
- /home/mark/repos/gl_test/src/core/String.cpp — implementation for utf8Length candidate
- /home/mark/repos/gl_test/src/scene/LuaInterface.h — declarations for Lua binding candidates
- /home/mark/repos/gl_test/src/scene/LuaInterface.cpp — lua_register lines and implementations for candidate bindings
- /home/mark/repos/gl_test/res/scenes/physics.lua — commented references that confirm non-active usage for layer APIs
- /home/mark/repos/gl_test/src/main.cpp — runtime entry anchor for call-path sanity checks

**Verification**
1. Symbol reference checks in src/: run regex search for each candidate symbol and confirm only definition/registration matches remain.
2. Script reference checks in res/: confirm no active (non-comment) script calls for candidate Lua APIs.
3. Build-readiness check for future deletion pass: run the build task after deletions and confirm successful link.
4. Runtime sanity check for future deletion pass: launch app and load default + physics scene to ensure Lua interface startup is intact.

**Decisions**
- Included scope: C++ functions/symbols only.
- Confidence threshold: high confidence only.
- Deliverable shape: report-only plan (no implementation/removal in this pass).
- Excluded scope: Lua file cleanup, asset cleanup, and CMake/build target cleanup.

**Further Considerations**
1. If you want a fast follow-up after this report, the next pass can remove only symbols with 100% no-call-site evidence and keep SceneLayer internals untouched unless their only caller is removed.
2. If runtime safety is critical, the deletion pass can be split into two PR-sized batches: String utility cleanup first, Lua binding cleanup second.