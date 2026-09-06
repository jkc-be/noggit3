# Reforged AzerothCore integration for Noggit3

Local extension of upstream Noggit3 (GPL-3.0). Upstream authors retain attribution in
source headers, git history and COPYING. This extension adds a server-object layer and
an offline SQL exporter; it is not an upstream release.

Read SERVER-HANDOFF.md for editing and server application instructions.

Build on Ubuntu 24.04 with CMake/Ninja, Qt5 development libraries, OpenGL development
libraries, zlib and bzip2 development libraries. StormLib is fetched at the upstream
pinned version. Configuration used here:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNOGGIT_BINDLESS_TEXTURES=OFF -DNOGGIT_WITH_SCRIPTING=OFF -DUSE_EXTRA_OPTIMIZATION=OFF
    cmake --build build -j 8
    python3 -m unittest discover -s integration -p 'test_*.py'

Install the binary alongside an integration/ directory containing export.py, catalog.json,
and the documentation. Python 3 is required for SQL export. Templates are cataloged
without running any AzerothCore component. The available models are resolved from the
active client's GameObjectDisplayInfo.dbc at runtime.
