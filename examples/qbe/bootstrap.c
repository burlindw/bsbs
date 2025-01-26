#define BSBS_IMPL
#include "../../include/bsbs.h"

#define arrlen(arr) (sizeof(arr) / sizeof(*(arr)))

// The names of the compilation units (without the .o or .c) file extensions.
// You could also use preprocessor macros to do something similar, or just
// list all the calls to `compile_qbe_object` explicitly, but this is easier
// for this use case.
char const *qbe_common[] = {
  "main", "util", "parse", "abi", "cfg", "mem", "ssa", "alias", "load", "copy",
  "fold", "simpl", "live", "spill", "rega", "emit", "amd64/targ", "amd64/sysv",
  "amd64/isel", "amd64/emit", "arm64/targ", "arm64/abi", "arm64/isel",
  "arm64/emit", "rv64/targ", "rv64/abi", "rv64/isel", "rv64/emit",
};

// QBE includes a header that is generated at build time. This file only
// contains a single preprocessor definition that determines the default target,
// which is based on the output of the `uname` utility. We accomplish the same
// thing here with the equivalent compiler macros.
void gen_qbe_config(void *userdata, FILE* fp) {
  #if defined(__APPLE__)
    #if defined(__arm64__)
      fprintf(fp, "#define Deftgt T_arm64_apple\n");
    #else
      fprintf(fp, "#define Deftgt T_amd64_apple\n");
    #endif
  #else
    #if defined(__arm64__)
      fprintf(fp, "#define Deftgt T_arm64\n");
    #elif defined(__riscv)
      fprintf(fp, "#define Deftgt T_rv64\n");
    #else
      fprintf(fp, "#define Deftgt T_amd64_sysv\n");
    #endif
  #endif
}

void compile_qbe(char const *binary) {
  // This grabs the current state of the global arena allocator.
  // We can use it to free the memory this used once we are done.
  void *save = bsbs_alloc_save();

  // Create the config.h file if it doesn't exist and populate it using the
  // gen_qbe_config callback that we provide.
  bsbs_generate("vendor/qbe/config.h", NULL, gen_qbe_config);

  // Create the run step for linking the final binary. BSBS provides the
  // `bsbs_cc` global variable for the name of the system's C compiler based
  // on the value of the `CC` environment variable. If the environment variable
  // isn't set, it will default to "cc";  bsbs_run *link = bsbs_run_create();
  bsbs_run *link = bsbs_run_create();
  bsbs_run_add_arg(link, bsbs_cc);
  
  for (size_t i = 0; i < arrlen(qbe_common); i++) {
    // Create the complete filepaths for the inputs and outputs.
    char const *unit = qbe_common[i];
    char *source = bsbs_alloc_print("vendor/qbe/%s.c", unit);
    char *object = bsbs_alloc_print("output/obj/qbe/%s.o", unit);

    // Add the object as an input to the linking step
    bsbs_run_add_input_file_arg(link, object);

    // Create the compile step and add the arguments. 
    bsbs_run *compile = bsbs_run_create();
    bsbs_run_add_arg(compile, bsbs_cc);
    bsbs_run_add_arg(compile, "-std=c99");
    bsbs_run_add_arg(compile, "-g");
    bsbs_run_add_arg(compile, "-c");
    bsbs_run_add_input_file_arg(compile, source);
    bsbs_run_add_arg(compile, "-o");
    bsbs_run_add_output_file_arg(compile, object);

    // All of the files depend on some of the headers, but the headers aren't
    // passed to the compiler as arguments. This is handled using the separation
    // between an input_file_arg and an input_file.
    bsbs_run_add_input_file(compile, "vendor/qbe/all.h");
    bsbs_run_add_input_file(compile, "vendor/qbe/ops.h");

    // To fully recreate the dependencies that QBE's Makefile describes, we need
    // to add some of the headers conditionally. Luckily QBE is structured well
    // enough that we can just check what the compilation unit name starts with
    // to determine this. There are other ways that you could do this - such as
    // making a function that takes this information as parameters - but this is
    // the cleanest aproach for this situation.
    if (strncmp("main", unit, 4) == 0) {
      bsbs_run_add_input_file(compile, "vendor/qbe/config.h");
    } else if (strncmp("arm64", unit, 5) == 0) {
      bsbs_run_add_input_file(compile, "vendor/qbe/arm64/all.h");
    } else if (strncmp("amd64", unit, 5) == 0) {
      bsbs_run_add_input_file(compile, "vendor/qbe/amd64/all.h");
    } else if (strncmp("rv64", unit, 4) == 0) {
      bsbs_run_add_input_file(compile, "vendor/qbe/rv64/all.h");
    }

    // Check the inputs and outputs to determine if the output needs to be
    // rebuilt, then execute the command if it's necessary.
    bsbs_run_execute(compile);
  }

  // Add the output args of the link step
  bsbs_run_add_arg(link, "-o");
  bsbs_run_add_output_file_arg(link, binary);

  // Link the final executable.
  bsbs_run_execute(link);

  // Free the memory we used.
  bsbs_alloc_load(save);
}

int main(int argc, char **argv) {
  bsbs_init(1024 * 1024, argv[0], __FILE__);
  compile_qbe("output/bin/qbe");
}
