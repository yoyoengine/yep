/*
    This file is a part of yoyoengine. (https://github.com/yoyoengine/yoyoengine)
    Copyright (C) 2023-2025  Ryan Zmuda

    Licensed under the MIT license. See LICENSE file in the project root for details.
*/

#include <stdio.h>
#include <string.h>

#include "libyep.h"

void print_usage() {
    printf("Usage: yep <input_directory> <output_file.yep>\n");
    printf("Pack a directory into a .yep pack file\n\n");
    printf("Arguments:\n");
    printf("  input_directory   Directory to pack\n");
    printf("  output_file.yep   Output pack file path\n");
}

int main(int argc, char **argv) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *input_dir = argv[1];
    const char *output_file = argv[2];

    yep_initialize();
    
    yep_logf(yep_log_info, "Packing directory: %s into %s\n", input_dir, output_file);

    if (!yep_force_pack_directory((char *)input_dir, (char *)output_file)) {
        yep_logf(yep_log_error, "Failed to pack directory %s into %s\n", input_dir, output_file);
        yep_shutdown();
        return 1;
    }

    yep_shutdown();
    return 0;
}
