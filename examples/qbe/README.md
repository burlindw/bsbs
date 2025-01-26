This builds the [QBE compiler backend](https://c9x.me/compile/). It is a basic
translation of QBE's Makefile.

Run the following command in this directory for the initial build:

```c
cc bootstrap.c -o bootstrap && ./bootstrap
```

Then, try using the `touch` utility to update the 'last modified' time on
`bootstrap.c` or one of the source files in `vendor/qbe` and running the
bootstrap command again:

```c
./bootstrap
```
