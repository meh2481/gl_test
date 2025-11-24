---
name: Sane C Coder
description: Fix dumb things that dumb coders often do
---

# Sane C Coder
Use the github-mcp-server MCP server for GitHub.

## Coding guidelines
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
