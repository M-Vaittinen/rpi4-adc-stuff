---
description: "Use when writing or editing C code. Enforces Linux kernel coding style from https://www.kernel.org/doc/html/latest/process/coding-style.html"
applyTo: "**/*.c,**/*.h"
---

# Linux Kernel Coding Style

This project uses Linux kernel coding style for all C code.
The full rules are defined in the skill: `.github/skills/linux-kernel-style/SKILL.md`

Reference: https://www.kernel.org/doc/html/latest/process/coding-style.html

## Indentation

- Use **tabs** (not spaces) for indentation. Tab width is 8 characters.
- Do not use 4- or 2-space indentation.
- If you need more than 3 levels of indentation, refactor the code.
- In `switch` statements, align `case` labels with the `switch` keyword (no double-indent).
- No multiple statements on a single line.
- No multiple assignments on a single line.

## Line Length

- Hard limit of **80 columns** per line.
- Break long lines into sensible chunks. Align continuations to the opening parenthesis where possible.
- Never break user-visible strings (e.g. `printf`/`fprintf` messages) — they must remain grep-able.

## Braces

- K&R style: opening brace at end of line for all non-function blocks (`if`, `for`, `while`, `do`, `switch`).
- Functions: opening brace on its **own line**.
- Closing brace on its own line, except where followed by `else`, `while` (do-while), etc.
- Omit braces for single-statement bodies — but if one branch needs braces, brace both branches.
- Always brace loop bodies that contain an `if`.

```c
/* non-function block */
if (condition) {
        do_this();
        do_that();
} else {
        otherwise();
}

/* function */
int foo(int x)
{
        return x;
}
```

## Spaces

- Space after keywords: `if`, `switch`, `case`, `for`, `do`, `while`.
- No space after `sizeof`, `typeof`, `alignof`, `__attribute__`.
- No space inside parenthesized expressions: `sizeof(struct foo)`, not `sizeof( struct foo )`.
- One space around binary and ternary operators: `=`, `+`, `-`, `<`, `>`, `*`, `/`, `%`, `|`, `&`, `^`, `<=`, `>=`, `==`, `!=`, `?`, `:`.
- No space after unary operators: `&`, `*`, `+`, `-`, `~`, `!`.
- No space before/after postfix `++`/`--`; no space after prefix `++`/`--`.
- No space around `.` and `->` member access operators.
- `*` for pointers goes adjacent to the **name**, not the type: `char *buf`, not `char* buf`.
- No trailing whitespace.

## Naming

- Variables and functions: `lowercase_with_underscores`.
- Macros and enum constants: `ALL_CAPS`.
- Global functions and variables must have **descriptive** names.
- Local variables should be short and to the point (`i`, `tmp`, `ret`).
- No Hungarian notation.
- Avoid `master/slave`; prefer `primary/secondary`, `controller/device`, etc.
- Avoid `blacklist/whitelist`; prefer `denylist/allowlist`.

## Typedefs

- Do **not** typedef structs or pointers to structs.
- Acceptable typedef uses: opaque objects, clear integer type aliases (`u8`/`u16`/`u32`/`u64`), sparse type-checking types, userspace-shared types (`__u32`).

## Functions

- Functions should do **one thing** and fit in one or two screenfuls (80×24).
- Maximum ~5–10 local variables per function; split if exceeded.
- Separate functions with one blank line in source files.
- Include parameter names in function prototypes.
- Do not use `extern` in function declarations.

## Error Handling and Goto

- Use `goto` for centralized cleanup/exit in functions with multiple resources.
- Label names should describe what they do: `out_free_buffer:`, not `err1:`.
- Each distinct cleanup step should have its own label to avoid null-pointer bugs.

## Comments

- Use `/* block style */` comments, not `//` line comments.
- Multi-line comment format:
  ```c
  /*
   * This is the preferred style for multi-line
   * comments in the Linux kernel source code.
   */
  ```
- Comments should explain **what** and **why**, not **how**.
- Avoid comments inside function bodies; prefer comments at the function head.
- One data declaration per line, with a short inline comment if needed.

## Macros

- Multi-statement macros must use `do { ... } while (0)`.
- Prefer `static inline` functions over function-like macros.
- Constants defined by macros must parenthesize expressions: `#define CONSTEXP (CONSTANT | 3)`.
- Do not use macros that affect control flow (hidden `return`).
- Do not use macros that depend on magic local variable names.
- Macro names for constants: `ALL_CAPS`. Function-like macros may be lowercase.
- Enums are preferred over multiple `#define` constants for related values.

## Inline

- Do not use `inline` for functions longer than ~3 lines.
- The compiler handles inlining of small static functions automatically.

## Return Values

- Command/action functions return `0` on success, negative error code (`-Exxx`) on failure.
- Predicate functions return non-zero (true) on success, `0` (false) on failure.
- Pointer-returning functions return `NULL` or `ERR_PTR()` on failure.

## Conditional Compilation

- Avoid `#ifdef` inside `.c` function bodies; move conditionals to headers with stub functions.
- Use `IS_ENABLED(CONFIG_X)` in preference to `#ifdef CONFIG_X` where possible.
- Comment non-trivial `#endif` lines: `#endif /* CONFIG_SOMETHING */`.

## General

- No editor modelines in source files.
- Do not use `BUG()` / `BUG_ON()`; use `WARN_ON_ONCE()` with recovery where possible.
- Use `BUILD_BUG_ON()` for compile-time assertions.
