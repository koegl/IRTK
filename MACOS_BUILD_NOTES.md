# Building rview on macOS (Apple Silicon, CMake 4)

What we did beyond the standard steps in `INSTALL.markdown`.

Tested on: macOS (Darwin 25), arm64, AppleClang 21, CMake 4.4.5, Homebrew.

## 1. Dependencies (Homebrew)

```bash
brew install cmake fltk gsl boost
```

- `fltk`: the GUI toolkit rview uses
- `gsl`, `boost`: the configure step requires both
- OpenGL and GLUT ship with macOS, so nothing to install for them

## 2. Source fix: `CMakeLists.txt`

CMake 4 no longer accepts `OLD` behaviour for policy CMP0022, so the configure step fails. Line 6 changed:

```diff
-    cmake_policy(SET CMP0022 OLD)
+    cmake_policy(SET CMP0022 NEW)
```

## 3. Configure flags

CMake 4 rejects `cmake_minimum_required(VERSION 2.6)` unless you pass:

```
-DCMAKE_POLICY_VERSION_MINIMUM=3.5
```

<!-- Further steps (build fixes, running rview) to be added as we go. -->
