# Bootstrapping Build System (BSBS)

This is an stb-style header for creating projects that only depends on a gcc
compatible C compiler and the source code. This is intended for relatively
simple projects.

> [!IMPORTANT]
> There are no plans to support Windows.

## Usage

Copy the `include/bsbs.h` file into your project at a location of your chose.

Create a C source file in the root of your project. By convention, this should
be called `bootstrap.c`, but the name isn't important to the use of BSBS.

Call `bsbs_init()` at the beginning of the main function and create the commands
or generated files needed to build the project. See the `examples` directory for
examples of how this is used.

> [!NOTE]
> Some examples use git submodules. To run the examples, you must run `git
> submodule init --recursive` after cloning this repository.

The first time the project is built, you must explicitly compile the build
script.

```sh
cc bootstrap.c -o bootstrap && ./bootstrap
```

For all subsequent builds, you only need to rerun the executable. It will
detect if the source file has changed and recompile itself automatically.
