# Building rview on macOS (Apple Silicon, CMake 4)

What we did beyond the standard steps in `INSTALL.markdown`, with the exact
commands. Run them from the repo root unless noted.

Tested on: macOS (Darwin 25), arm64, AppleClang 21, CMake 4.4.5, Homebrew,
FLTK 1.4.5, GSL 2.8, Boost 1.92.

## 1. Dependencies (Homebrew)

```bash
brew install cmake fltk gsl boost
```

- `fltk`: the GUI toolkit rview uses
- `gsl`, `boost`: the configure step requires both
- OpenGL and GLUT ship with macOS, so nothing to install for them

Check:

```bash
cmake --version && fltk-config --version && gsl-config --version
```

## 2. Source fixes

These are already committed in this fork. `git diff 06d1a6d` shows them
against the original BioMedIA/IRTK.

### 2a. `CMakeLists.txt`: policy CMP0022

CMake 4 no longer accepts `OLD` behaviour for CMP0022.

```bash
sed -i '' 's/cmake_policy(SET CMP0022 OLD)/cmake_policy(SET CMP0022 NEW)/' CMakeLists.txt
```

### 2b. `config/IRTKInclude.cmake`: libraries linking to themselves

A global `LINK_LIBRARIES(segmentation++ ... common++)` tells every IRTK library
to link to itself. Old CMake ignored that; modern CMake fails with
`Target "common++" links to itself`. Fix: append an `add_library` wrapper that
removes the self-reference.

```bash
cat >> config/IRTKInclude.cmake <<'EOF'

# macOS/CMake 4 fix: the global LINK_LIBRARIES above make every IRTK library
# link to itself, which modern CMake rejects. Strip the self-reference.
macro(add_library _irtk_name)
  _add_library(${_irtk_name} ${ARGN})
  foreach(_irtk_prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(_irtk_libs ${_irtk_name} ${_irtk_prop})
    if(_irtk_libs)
      list(REMOVE_ITEM _irtk_libs ${_irtk_name} "$<LINK_ONLY:${_irtk_name}>")
      set_target_properties(${_irtk_name} PROPERTIES ${_irtk_prop} "${_irtk_libs}")
    endif()
  endforeach()
endmacro()
EOF
```

### 2c. `config/FindFLTK.cmake`: AGL framework

Recent macOS SDKs no longer include `AGL.framework`.

```bash
sed -i '' 's/ -framework AGL//' config/FindFLTK.cmake
```

### 2d. `packages/registration/src/irtkDemonsRegistration.cc`: pointer compared with 0

Clang rejects `if (_TargetResolution > 0)`, a comparison between a pointer and 0.
Replaced with `!= NULL`, which keeps the old behaviour (always true).

```bash
sed -i '' 's/if (_TargetResolution > 0)/if (_TargetResolution != NULL)/; s/if (_SourceResolution > 0)/if (_SourceResolution != NULL)/' packages/registration/src/irtkDemonsRegistration.cc
```

## 3. Configure and build

```bash
mkdir -p build && cd build
```

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-Wno-c++11-narrowing" -DCMAKE_EXE_LINKER_FLAGS="-L/opt/homebrew/lib" -Wno-dev
```

```bash
make -j8 rview
```

Flags beyond the defaults:

- `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`: CMake 4 rejects `cmake_minimum_required(VERSION 2.6)` without it
- `-DCMAKE_CXX_FLAGS="-Wno-c++11-narrowing"`: clang treats old `int`→`size_t` brace-initializers as errors
- `-DCMAKE_EXE_LINKER_FLAGS="-L/opt/homebrew/lib"`: FLTK links `-ljpeg`, which lives in Homebrew's lib folder
- `-DBUILD_TESTS=OFF`: skips the bundled gtest unit tests (not needed for rview)
- `-Wno-dev`: hides CMake's deprecation warnings for this old code

`make rview` builds only rview and the libraries it needs. The binary is
`build/bin/rview`.

The build prints many compiler warnings and an `ld: warning: ignoring duplicate
libraries` line. Both are harmless.

## 4. Run

From `build/`:

```bash
./bin/rview /path/to/image.nii.gz
```

`build/` is listed in `.gitignore`, so build output stays out of git.
