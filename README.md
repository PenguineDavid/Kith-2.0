# Kith-2.0

A small compiled language that transpiles to C. Clean syntax, fast output, no runtime dependencies.

```kith
func main()
{
    str name = "world"
    print($"Hello, {name}!")
}
```

---

## Install

**Requirements:** A C++ compiler (g++ / clang++) and gcc on your PATH.

```bash
git clone https://github.com/PenguineDavid/Kith-2.0
cd Kith-2.0/src
g++ -std=c++17 main.cpp lexer.cpp parser.cpp codegen.cpp -o kith
```

Add `kith` to your PATH or run it directly from the project folder.

---

## Usage

```bash
kith source.kith              # compiles to ./output
kith source.kith myprogram    # compiles to ./myprogram

# Flags
kith --keep-c source.kith     # keeps the intermediate output.c for debugging
kith --bounds source.kith     # enables runtime bounds checking on array accesses
kith --help
```

---

## Language Reference

### Types

| Kith | Description |
|------|-------------|
| `int` | 32-bit integer |
| `float` | 64-bit double |
| `bool` | Boolean (`true` / `false`) |
| `str` | String (`char*`) |
| `int*` `str*` `float*` | Pointer types |
| `int[]` `float[]` `str[]` | Stack arrays |
| `MyStruct` | User-defined struct |

### Variables

```kith
int   x = 10
float pi = 3.14159
bool  flag = true
str   msg = "hello"

int*  px  = &x          // pointer
int[] arr = [1, 2, 3]   // array (stack-allocated)
```

### Structs

```kith
struct Vec2
{
    float x
    float y
}

Vec2 pos = Vec2(1.0, 2.5)
print(pos.x)
pos.y = 9.9

Vec2* ppos = &pos
print(ppos->x)
ppos->x = 0.0
```

Struct constants can be accessed with `::`:

```kith
// Define a named constant the :: way:
// Declare at global scope as  TypeName__ConstantName
int Math__PI = 3    // accessed as Math::PI in expressions... coming soon
```

### Functions

```kith
func add(int a, int b)
{
    return a + b
}

func greet(str name)
{
    print($"Hello, {name}!")
}
```

### Control Flow

```kith
// if / else
if (x > 0) {
    print("positive")
} else {
    print("non-positive")
}

// while
while (x > 0) {
    x = x - 1
}

// do-while
do {
    x = x + 1
} while (x < 10)

// for
for (int i = 0; i < 10; i = i + 1) {
    print(i)
}

// foreach (requires an array with a known length)
int[] nums = [10, 20, 30]
foreach (int n in nums) {
    print(n)
}

// switch — works on both int and str
switch (code) {
    case 1      { print("one")   }
    case 2      { print("two")   }
    default     { print("other") }
}

switch (lang) {
    case "kith" { print("Kith!") }
    case "c"    { print("C")     }
    default     { print("?")     }
}
```

### Operators

| Category | Operators |
|----------|-----------|
| Arithmetic | `+ - * / % **` |
| Comparison | `< > <= >= == != === !==` |
| Logical | `&& \|\| ^^ ! and or xor not` |
| Bitwise | `& \| ^ ~ << >> >>>` |
| Ternary | `cond ? a : b` |
| Null-coalescing | `a ?? b` |
| Increment | `x++ x-- ++x --x` |
| Member access | `. ->` |
| Index | `arr[i]` (read and write) |
| Address / deref | `&x` `*px` |
| Cast | `int(x)` `float(x)` `str(x)` `bool(x)` |
| Typeof | `typeof x` |
| Delete | `delete x` — calls `free(x)` |

### Strings

```kith
str a = "normal string"
str b = r"raw \n no escape"          // backslash not processed
str c = $"x is {x}, y is {y}"        // interpolated — heap allocated, remember to free()
str d = """
multi
line
"""

free(c)    // free heap-allocated strings when done
```

### Exceptions

```kith
try {
    throw "something went wrong"
} catch (e) {
    print(e)
} finally {
    print("always runs")
}
```

Exceptions are implemented with `setjmp`/`longjmp`. An uncaught `throw` prints to stderr and exits with code 1.

### Arrays

```kith
int[] arr = [1, 2, 3, 4, 5]

print(arr[0])       // read
arr[2] = 99         // write
print(len(arr))     // 5 -- companion _len variable

foreach (int x in arr) {
    print(x)
}
```

Compile with `--bounds` to get runtime bounds checking:

```bash
kith --bounds myprogram.kith
# arr[10] on a 3-element array prints:
# Bounds check failed: index 10 out of range [0,3)
# and exits with code 1
```

### Includes

```kith
include "utils.kith"    // parsed and merged inline
include "mylib.h"       // passed through as #include "mylib.h" in generated C
```

### Built-in Functions

| Function | Description |
|----------|-------------|
| `print(x)` | Print value followed by newline |
| `len(arr)` | Length of a named array |
| `free(x)` | Free a heap-allocated value |
| `toInt(s)` | Parse string to int |
| `toFloat(s)` | Parse string to float |
| `toStr(n)` | Convert int to string (heap-allocated) |
| `input()` | Read a line from stdin (heap-allocated, strips newline) |
| `sqrt(x)` | Square root |
| `abs(x)` | Absolute value |
| `floor(x)` `ceil(x)` | Floor / ceiling |
| `min(a, b)` `max(a, b)` | Minimum / maximum |

---

## How It Works

Kith is a transpiler. It compiles your source to C, then calls gcc to produce the final binary. The intermediate `output.c` file is deleted automatically unless you pass `--keep-c`.

```
source.kith  ->  [Lexer]  ->  [Parser]  ->  [AST]  ->  [CodeGen]  ->  output.c  ->  gcc  ->  binary
```

This means you get fast native binaries with zero runtime overhead, and you can drop in any C library via `include "header.h"`.

---

## Example Programs

### FizzBuzz

```kith
func main()
{
    for (int i = 1; i <= 20; i = i + 1) {
        if (i % 15 == 0) {
            print("FizzBuzz")
        } else if (i % 3 == 0) {
            print("Fizz")
        } else if (i % 5 == 0) {
            print("Buzz")
        } else {
            print(i)
        }
    }
}
```

### Fibonacci

```kith
func fib(int n)
{
    if (n <= 1) { return n }
    return fib(n - 1) + fib(n - 2)
}

func main()
{
    for (int i = 0; i < 10; i = i + 1) {
        print(fib(i))
    }
}
```

### Structs and methods

```kith
struct Point
{
    int x
    int y
}

func distance(Point a, Point b)
{
    int dx = a.x - b.x
    int dy = a.y - b.y
    return sqrt(dx * dx + dy * dy)
}

func main()
{
    Point origin = Point(0, 0)
    Point target = Point(3, 4)
    print(distance(origin, target))    // 5
}
```

### Exception handling

```kith
func divide(int a, int b)
{
    if (b == 0) {
        throw "division by zero"
    }
    return a / b
}

func main()
{
    try {
        print(divide(10, 2))    // 5
        print(divide(10, 0))    // throws
    } catch (e) {
        print(e)                // division by zero
    }
}
```

---

## Compiler Flags

| Flag | Description |
|------|-------------|
| `--keep-c` | Keep `output.c` after compilation (useful for debugging generated C) |
| `--bounds` | Enable runtime bounds checking for array index operations |
| `--help` | Print usage information |

---

## Building the Test Suite

```bash
g++ -std=c++17 test.cpp lexer.cpp parser.cpp codegen.cpp -o test_runner
./test_runner
```

---

## Known Limitations

- All functions emit `int` as their C return type regardless of what they actually return
- No static type checking — type errors are caught by gcc, not the Kith compiler
- Stack-only arrays — no dynamic resizing
- `print(arr[i])` on a float array requires assigning to a local variable first: `float v = arr[i]` then `print(v)`
- Global array elements must be compile-time constants

---

## License

polyform noncommercial license 1.0.0
https://github.com/PenguineDavid/Kith-2.0/blob/master/LICENSE.md

---

## Coming Soon Ish(I hope)

- Make sure interpolated strings get freed properly. Currently only interpolated locals are freed, and even then only when the variable goes out of scope - if you assign an interpolated string to a global variable, it will never be freed. A more robust solution would be to track all allocated strings and free them at program exit, but that might be overkill for this project.
- Add python like for loop sytax like for i in range(10) { ... } and for item in collection { ... }. This would be syntactic sugar over the existing for and foreach, but it would make loops more concise and readable.
- Add support for heap-allocated arrays. Currently all arrays are stack-allocated C arrays, which means their size must be known at compile time and they can't be resized. Adding heap arrays would allow for dynamic resizing and more flexible data structures, but it would also require implementing a memory management strategy (e.g. reference counting or garbage collection) to avoid leaks.
- Add support for C++ like syntax for functions with func int add(int a, int b) { ... } and func void print(string s) { ... }. This would require changes to the parser to allow optional return types and to the codegen to emit the correct C function signatures.
- Add support for classes and inheritance. This would be a major feature that would require a lot of design work to figure out how to map class concepts to C structures and functions.
