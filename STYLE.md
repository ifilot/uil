# C++ Style

UIL uses the Google C++ formatting rules with project-specific naming conventions that make
project code visually distinct from Qt APIs.

## Naming

- Source and header filenames use lowercase `snake_case` with the existing `.cpp` and `.hpp`
  extensions.
- Classes, structs, enums, and type aliases use `PascalCase`.
- Functions, methods, signals, slots, variables, parameters, and namespaces defined by UIL use
  `snake_case`.
- Private data members use `snake_case_` with a trailing underscore.
- Constants use `kPascalCase`; macros use `UPPER_SNAKE_CASE`.
- Qt APIs retain their original camelCase spelling.
- Qt virtual overrides retain the exact spelling required by Qt, such as `paintEvent()` and
  `eventFilter()`.
- Language and framework hooks retain their required spelling, including constructors,
  destructors, operators, `main()`, and Qt's `qHash()` overload.

## Documentation

Every function declaration must have a Doxygen comment that describes its purpose. Document
parameters, return values, side effects, and error reporting when they are not clear from the
brief description. Internal functions declared only in a `.cpp` file follow the same rule.

## Formatting

Run `clang-format -i` on changed C++ source and header files. The repository's `.clang-format`
uses Google formatting with a 100-column limit and left-aligned pointer and reference markers.
