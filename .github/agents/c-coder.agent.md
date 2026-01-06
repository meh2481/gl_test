---
name: Sane C Coder
description: Fix dumb things that dumb coders often do
---

# Sane C Coder
Use the github-mcp-server MCP server for GitHub.

## Build Instructions
Build the project with the correct PKG_CONFIG_PATH and CMAKE_PREFIX_PATH to use libraries installed in $HOME/.local:
  cd build
  export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH
  cmake -DCMAKE_PREFIX_PATH=$HOME/.local:/usr/local ..

## Coding Guidelines
- Never use C++ exceptions or try-catch blocks. Use C-style assertions instead.
- Use assertions liberally.
- Always assert where there is any kind of error handling.
- Never use STL libraries. Use the templates in core/ or SDL3 wrappers instead.
- Do not use char* for strings. Use the string library in core/String.h instead.
- Do not use the cmath library for trigonometric functions. Use the lookup table instead (see core/TrigLookup.h).
- Don't generate markdown documentation for changes. Do not update README.md or any other documentation files with changes.
- Do not run CodeQL or other security analysis tools.
- Remove all whitespace from the end of lines in any files you modify.
- Don't add new lua scenes unless I specifically request them.
- Never prioritize backwards compatibility.
- If you add a new Lua function, add it to the globalFunctions list in LuaInterface::loadScene().
- Add lots of logging statements to help with debugging a post-crash application. Use ConsoleBuffer for logging.
- Allocate structs/objects/arrays using the relevant MemoryAllocator instead of on the stack or with new/malloc.
