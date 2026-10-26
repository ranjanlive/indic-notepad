# Native source

C++20 Win32 application. Build from the **repository root** with
`python scripts/build.py`; see ../docs/BUILD_AND_RELEASE.md. This directory has
the CMake project, checked IO/search core, selected Lexilla catalogue, native
shell, resources and meaningful regression tests. External dependencies are
hash-pinned and populated under ignored build/_deps; do not vendor binaries.
