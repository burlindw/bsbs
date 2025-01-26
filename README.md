# Bootstrapping Build System (BSBS)

This is an stb-style header for creating projects that only depends on a gcc
compatible C compiler and the source code. This is intended for relatively
simple projects.

BSBS is "immediate" in the sense that there defining the steps and executing the
steps are not separate. This is in contrast to more complex "deferred" build
systems, where you define a dependency graph, which is then interpreted in a
second stage.

The benefit of an "immediate" system is that it is significantly simpler than a
"deferred" system. However, it means that the system cannot determine the order
that items must be built in on its own; you must ensure that items are created
in the correct order.

> [!IMPORTANT]
> I do not plan to support Windows.

## Usage

Copy the `include/bsbs.h` file into your project at a location of your choice.

Create a C source file in the root of your project. By convention, this should
be called `bootstrap.c`, but the name can be anything.

Include the `bsbs.h` file in the source file. Be sure to define a `BSBS_IMPL`
macro before the include directive. Do not define `BSBS_IMPL` in any other
compilation units.

```c
#define BSBS_IMPL
#include "vendor/bsbs/include/bsbs.h"
```

Call `bsbs_init()` at the beginning of the main function and create the commands
or generated files needed to build the project. See the `examples` directory for
examples of how this is used.

> [!NOTE]
> Some examples use git submodules. To run the examples, you must run `git
> submodule update --init --recursive` after cloning this repository.

The first time the project is built, you must explicitly compile the build
script.

```sh
cc bootstrap.c -o bootstrap && ./bootstrap
```

For all subsequent builds, you only need to rerun the executable. It will
detect if the source file has changed and recompile itself automatically.
