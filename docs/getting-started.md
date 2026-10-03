# Getting started

## Requirements

- CMake 3.20+, a C++17 compiler (GCC 9+, Clang 10+; MSVC is untested), a build tool (Ninja or
  Make). No third-party libraries.
- Optional: Emscripten (library builds for the web), Graphviz (rendering `adlib graph` DOT output).

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The test suite needs no external data:

| test | checks |
|---|---|
| `adlib_test` | runtime, PBN reader/writer, vocabulary |
| `adlibc_test` | the compiler: all 185 fixtures in `fixtures/compiler/` (exact bytes or exact diagnostic, as the original 1996 compiler produced them with the symbol files in `fixtures/vocab/`), unit checks, the vending machine via files and via a `Vocabulary` |
| `adlib_tools_test` | decompiler, graphs, lint, compare, trace filters |
| `vending_machine` | the example host runs to completion |
| `cli_version`, `cli_graph_mermaid` | the command-line tools |

## CMake options

| option | default | |
|---|---|---|
| `ADLIB_BUILD_TOOLS` | ON | `adlibc`, `adlib` and the `adlib_tools` library |
| `ADLIB_BUILD_EXAMPLES` | ON when top-level | the vending machine (needs the tools: its plans are compiled by adlibc) |
| `ADLIB_BUILD_TESTS` | ON when top-level | the test suite |
| `ADLIB_INSTALL` | ON when top-level | install rules and the CMake package |

The library alone (for example for the web):

```sh
emcmake cmake -S . -B build-web -DADLIB_BUILD_TOOLS=OFF -DADLIB_BUILD_TESTS=OFF -DADLIB_BUILD_EXAMPLES=OFF
cmake --build build-web
```

## Install and use from CMake

```sh
cmake --install build --prefix /opt/adlib
```

```cmake
find_package(adlib 0.1 CONFIG REQUIRED)            # CMAKE_PREFIX_PATH=/opt/adlib
target_link_libraries(mygame PRIVATE adlib::adlib)  # runtime + compiler API
target_link_libraries(mytool PRIVATE adlib::tools)  # decompiler, graphs, lint, trace sink
```

Or vendor the source tree and `add_subdirectory(adlib)`; the targets are then `adlib` and
`adlib_tools` (aliases `adlib::adlib`, `adlib::tools`), and tests/examples are off by default.
`#include "adlib/version.h"` gives `ADLIB_VERSION_STRING`; `adlibc --version` and
`adlib --version` print it.

## Your first plan

1. Write a vocabulary header: C enums, nothing else. The enum *names* bind the roles the runtime
   understands (`ID_MSG`, `ActionFunctionIDs`, `DecisionFunctionIDs`, `Behavior_IDs`, ...); a
   symbol's value is its position. Start from
   [examples/vending_machine/VENDING.H](../examples/vending_machine/VENDING.H).
2. Write the plan ([writing-a-plan.md](writing-a-plan.md)) and compile it:
   `adlibc --enumids MYGAME.H --strict -o PLAN.PBN PLAN.txt`.
3. Look at it: `adlib --enum MYGAME.H graph --mermaid PLAN.PBN`, `adlib --enum MYGAME.H lint PLAN.PBN`.
4. Load it in your game: register the actions and decisions by name, implement `PlanHost`, call
   `PlanScene::update()` every frame ([host-api.md](host-api.md)).
