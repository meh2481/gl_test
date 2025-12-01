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
- Never write unit tests.
- Never use C++ exceptions or try-catch blocks. Use C-style assertions instead.
- Use assertions liberally, where it makes sense to do so.
- Never use STL vector or linked lists. Use C-style arrays instead.
- Don't generate markdown documentation for changes. Do not update README.md or any other documentation files with changes.
- Do not add any _codeql* files or folders to the repository.
- Use Box2D version 3. The Box2D version in apt is 2, which is ancient.
- Do not run CodeQL or other security analysis tools.
- Remove all whitespace from the end of lines in any files you modify.
- Don't add new lua scenes unless I specifically request them.
- Don't add new assets to the res/ folder unless I specifically request them.
- Never split Lua APIs. Update existing Lua function calls when refactoring.
- If you add a new Lua function, make sure to add it to the globalFunctions list in LuaInterface::loadScene().
- Don't add comments that include changes made. Keep comments generic and timeless.
- Add similarly generic and timeless logging statements to help with debugging. Do not add logging statements that reference specific changes. Do not add logging statements that are not useful for debugging a post-crash application.
