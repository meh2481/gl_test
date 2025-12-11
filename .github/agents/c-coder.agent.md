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
- Use assertions liberally, where it makes sense to do so.
- Always assert where there is any kind of error handling, including std::cerr calls.
- Never use STL std::vector. Use the vector in core/Vector.h instead.
- Never use STL std::map. Use the map in core/HashTable.h instead.
- Never use STL std::string. Use the string in core/String.h instead.
- Do not use char* for strings. Use the string library in core/String.h instead.
- Don't generate markdown documentation for changes. Do not update README.md or any other documentation files with changes.
- Do not run CodeQL or other security analysis tools.
- Remove all whitespace from the end of lines in any files you modify.
- Don't add new lua scenes unless I specifically request them.
- Never check lua interface function parameter types to have multiple versions of a lua interface function. Update existing Lua function calls when refactoring.
- If you add a new Lua function, make sure to add it to the globalFunctions list in LuaInterface::loadScene().
- Don't add code comments that mention changes made.
- Do not add logging statements that reference specific changes.
- Do not add logging statements that are not useful for debugging.
- Add lots of logging statements to help with debugging a post-crash application.
