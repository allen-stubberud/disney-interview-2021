# Overview

This is my interview assignment for Disney Streaming Services. It reproduces
the Disney+ home screen using an example web API and OpenGL. See
[Building](#Building) for compilation instructions and [Features](#Features)
for more information.

## Building

C++17 support is required. These platforms are supported:
- g++ (Linux)
- g++ (MinGW)

The following dependencies are required to build the project:
- CMake (v3.3+)
- CURL (v7.71.0+)
- GLEW
- glm
- OpenGL (fixed-function)
- RapidJSON
- SDL2
- SDL2_image
- SDL2_ttf
- libsigc++ (v3.x)

It is easiest to install these with your package manager. For example, to
install them with MSYS2 on Windows (x86 toolchain), use this command:
```sh
$ pacman -S                                                                   \
    mingw-w64-i686-SDL2                                                       \
    mingw-w64-i686-SDL2_image                                                 \
    mingw-w64-i686-SDL2_ttf                                                   \
    mingw-w64-i686-cmake                                                      \
    mingw-w64-i686-curl                                                       \
    mingw-w64-i686-glew                                                       \
    mingw-w64-i686-glm                                                        \
    mingw-w64-i686-libsigc++-3.0                                              \
    mingw-w64-i686-rapidjson
```

Hint: the MSYS2 `mingw-w64-i686` toolchain can be installed with this command:
```sh
pacman -S mingw-w64-i686-toolchain
```

This is a standard CMake project so building is easy if the dependencies are
in the default search path. Just run CMake and then invoke the build tool like
this:
```sh
$ mkdir build && cd build
$ cmake ..
$ cmake --build .
```

## Code

There are six modules:
- `Graphics` - 2D render graph
- `Json` - parse the web API
- `Main` - main loop and event queue
- `Network` - asynchronous downloads (threaded)
- `Viewer` - quick layout engine for render graph
- `Worker` - asynchronous image/API decoding (threaded)

## Features

- 2D scene graph with OpenGL (transformations, culling, clipping)
- Asynchronous network operations with signal/slot notifications
- Keyboard navigation (arrow keys)
- Horizontal and vertical scrolling
- Dynamic layout that responds to changes in aspect ratio (try resizing)
- Preserves the aspect ratio of all reference artwork (try resizing)
- Download tiles only when needed (driven by frustum culling)
- Download additional rows only when needed (also driven by culling)

## Bugs

- Duplicate *New to Disney+* rows <br>
  This is actually caused by an error in the data returned by the web API. If
  you look closely you can see title of the row change from its original value
  to *New to Disney+* shortly after starting up. This happens because the home
  screen API mislabels the relevant "set" reference. It also happens to refer
  to the same set as the first row but the content inside the reference is
  actually different, hence the missing second tile (caused by HTTP 404).
- Purple squares <br>
  These are just placeholders for the tile images. They are displayed while
  images are downloaded from the internet. I originally intended to use a
  placeholder image but it turned out to be even uglier.
- Temporary file spam <br>
  The cache files are not deleted when references are retained in the event
  queue during shutdown. This is probably acceptable because real applications
  need to periodically clean their caches anyway to deal with unsafe shutdowns.

## Screenshots

Before any user input:
![](doc/screenshot-1.png)
Scrolling and selection on right edge:
![](doc/screenshot-3.png)
Scrolling with render graph BVH shown in red:
![](doc/screenshot-4.png)
Dynamic layout:
![](doc/screenshot-2.png)
