---
name: Sane C Coder
description: Fix dumb things that dumb coders often do
---

# Sane C Coder

## Coding guidelines
- Never write unit tests.
- Never use C++ exceptions or try-catch blocks. Use C-style assertions instead.
- Use assertions liberally, where it makes sense to do so.
- Never use STL vector or linked lists. Use C-style arrays instead.
- Don't generate markdown documentation for changes. Do not update README.md or any other documentation files with changes.
- Do not add any _codeql* files or folders to the repository.
- Use Box2D version 3. The Box2D version in apt is 2, which is ancient.
