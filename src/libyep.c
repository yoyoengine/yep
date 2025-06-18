/*
    This file is a part of yoyoengine. (https://github.com/yoyoengine/yoyoengine)
    Copyright (C) 2023-2025  Ryan Zmuda

    Licensed under the MIT license. See LICENSE file in the project root for details.
*/

#include <stdbool.h>
#include <string.h>     // for strdup, strcmp, etc.
#include <stdio.h>      // for printf, FILE, etc.
#include <stdlib.h>     // for malloc, free, etc.
#include <stdarg.h>     // for va_list, va_start, va_end

#include <zlib.h>       // zlib compression
#include <SDL3/SDL.h>   // dir traversal

#include "yepfs.h"
#include "libyep.h"

// holds the reference to the currently open yep file
char* yep_file_path = NULL;
FILE *yep_file = NULL;
uint16_t file_entry_count = 0;
uint16_t file_version_number = 0;

struct yep_pack_list yep_pack_list;

/*
    Trivial temp logger
*/
void yep_logf(enum yep_log_level level, const char *fmt, ...) {
    // Print the level prefix
    switch(level) {
        case yep_log_debug:
            printf("[DEBUG] ");
            break;
        case yep_log_info:
            printf("[INFO] ");
            break;
        case yep_log_warning:
            printf("[WARN] ");
            break;
        case yep_log_error:
            printf("[ERROR] ");
            break;
    }

    // Print the actual message
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/*
    Equivalent to ye_path to help get paths on disk
*/

/*
    ========================= COMPRESSION IMPLEMENTATION =========================
*/

int compress_data(const char* input, size_t input_size, char** output, size_t* output_size) {
    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) { // TODO: could add support for the other compression levels
        return -1;
    }

    // Set input data
    stream.next_in = (Bytef*)input;
    stream.avail_in = input_size;

    // Allocate initial output buffer
    *output_size = input_size + input_size / 10 + 12; // Adding some extra space for safety
    *output = (char*)malloc(*output_size);

    // Set output buffer
    stream.next_out = (Bytef*)*output;
    stream.avail_out = *output_size;

    // Compress the data
    if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
        free(*output);
        deflateEnd(&stream);
        return -1;
    }

    // Clean up
    deflateEnd(&stream);
    *output_size = stream.total_out;

    return 0;
}

int decompress_data(const char* input, size_t input_size, char** output, size_t output_size) {
    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    int inflate_result = inflateInit(&stream);
    if (inflate_result != Z_OK) {
        yep_logf(yep_log_error, "inflateInit error: %s\n", zError(inflate_result));
        return -1;
    }

    // Set input data
    stream.next_in = (Bytef*)input;
    stream.avail_in = input_size;

    // Allocate initial output buffer
    *output = (char*)malloc(output_size);

    // Set output buffer
    stream.next_out = (Bytef*)*output;
    stream.avail_out = output_size;

    // Decompress the data
    int res = inflate(&stream, Z_FINISH) != Z_STREAM_END;
    if (res) {
        free(*output);
        inflateEnd(&stream);
        yep_logf(yep_log_error,"Error decompressing data: %s\n",zError(res));
        return -1;
    }

    // Clean up
    inflateEnd(&stream);

    if(output_size != stream.total_out){
        yep_logf(yep_log_error,"Error: decompressed size does not match expected size\n");
        return -1;
    }

    return 0;
}

/*
    ============================= TIMESTAMP TRACKING =============================
*/

bool is_dir_outofdate(const char *target_directory, const char *yep_file_path){
    
    SDL_PathInfo dir_info;
    // check if the directory exists
    if(!yep_get_path_info(target_directory, &dir_info)){
        yep_logf(yep_log_error,"Error: directory %s does not exist\n", target_directory);
        return false;
    }
    // if the directory is not a directory, return false
    if(dir_info.type != SDL_PATHTYPE_DIRECTORY){
        yep_logf(yep_log_error,"Error: %s is not a directory\n", target_directory);
        return false;
    }

    // check if the yep file exists
    SDL_PathInfo yep_info;
    if(!yep_get_path_info(yep_file_path, &yep_info)){
        yep_logf(yep_log_error,"Error: yep file %s does not exist\n", yep_file_path);
        return false;
    }
    // if the yep file is not a file, return false
    if(yep_info.type != SDL_PATHTYPE_FILE){
        yep_logf(yep_log_error,"Error: %s is not a file\n", yep_file_path);
        return false;
    }

    // if the directory is newer than the yep file, return true
    if(dir_info.modify_time > yep_info.modify_time){
        yep_logf(yep_log_debug,"Directory %s is newer than yep file %s\n", target_directory, yep_file_path);
        return true;
    }

    yep_logf(yep_log_debug,"Directory %s is not newer than yep file %s\n", target_directory, yep_file_path);
    return false;
}

/*
    ==============================================================================
*/

// utility function via chatgpt - moveme //

void displayProgressBar(int current, int max) {
    // Calculate the percentage completion
    float progress = (float)current / max;
    
    // Determine the length of the progress bar
    int barLength = 50;
    int progressLength = (int)(progress * barLength);
    
    // Clear the current line
    printf("\r");

    // Display the progress bar
    printf("[");
    for (int i = 0; i < barLength; ++i) {
        if (i < progressLength) {
            printf("=");
        } else {
            printf(" ");
        }
    }
    printf("] %.2f%% (%d/%d)", progress * 100, current, max);
    
    // Flush the output to ensure it's immediately displayed
    fflush(stdout);
}

///////////////////////////////////////////

bool _yep_open_file(const char *file){
    // if we already have this file open, don't open it again
    if(yep_file_path != NULL && strcmp(yep_file_path, file) == 0){
        return true;
    }

    yep_file = fopen(file, "rb");
    if (yep_file == NULL) {
        yep_logf(yep_log_error,"Error opening yep file\n");
        return false;
    }

    // set the file path
    if(yep_file_path != NULL)
        free(yep_file_path);

    yep_file_path = strdup(file);

    // read the version number (byte 0-1)
    fread(&file_version_number, sizeof(uint8_t), 1, yep_file);

    // read the entry count (byte 2-3)
    fread(&file_entry_count, sizeof(uint16_t), 1, yep_file);

    if(file_version_number != YEP_CURRENT_FORMAT_VERSION){
        yep_logf(yep_log_error,"Error: file version number (%d) does not match current version number (%d)\n", file_version_number, YEP_CURRENT_FORMAT_VERSION);
        return false;
    }

    return true;
}

void _yep_close_file(){
    if(yep_file != NULL){
        fclose(yep_file);
        yep_file = NULL;

        if(yep_file_path != NULL)
            free(yep_file_path);

        file_entry_count = 0;
        file_version_number = 0;
    }
}

/*
    Takes in references to where to output the data if found, and returns true if found, false if not found
*/
bool _yep_seek_header(const char *handle, char *name, uint32_t *offset, uint32_t *size, uint8_t *compression_type, uint32_t *uncompressed_size, uint8_t *data_type){
    // go to the beginning of the header section (3 byte) offset from beginning
    fseek(yep_file, 3, SEEK_SET);

    /*
        Its simplist to just read the whole header into memory (the name is most of it) to
        keep ourselves aligned with the headers list
    */
    for(size_t i = 0; i < file_entry_count; i++){
        // printf("Searching for %s\n", handle);

        // 64 bytes - name of the resource
        fread(name, sizeof(char), 64, yep_file);

        // printf("Comparing against %s\n", name);

        // 4 bytes - offset of the resource
        fread(offset, sizeof(uint32_t), 1, yep_file);

        // 4 bytes - size of the resource
        fread(size, sizeof(uint32_t), 1, yep_file);

        // 1 byte - compression type
        fread(compression_type, sizeof(uint8_t), 1, yep_file);

        // 4 bytes - uncompressed size
        fread(uncompressed_size, sizeof(uint32_t), 1, yep_file);

        // 1 byte - data type
        fread(data_type, sizeof(uint8_t), 1, yep_file);

        // if the name matches, we found the header
        if(strcmp(handle, name) == 0){
            return true;
        }
    }
    return false;
}

struct yep_data_info yep_extract_data(const char *file, const char *handle){
    if(!_yep_open_file(file)){
        yep_logf(yep_log_warning,"Error opening yep file %s\n", file);
        return (struct yep_data_info){.data = NULL, .size = 0};
    }

    // printf("File: %s\n", yep_file_path);
    // printf("    Version number: %d\n", file_version_number);
    // printf("    Entry count: %d\n", file_entry_count);

    // setup the data we will seek out of the yep file
    char name[64];
    uint32_t offset;
    uint32_t size;
    uint8_t compression_type;
    uint32_t uncompressed_size;
    uint8_t data_type;

    // try to get our header
    if(!_yep_seek_header(handle, name, &offset, &size, &compression_type, &uncompressed_size, &data_type)){
        yep_logf(yep_log_warning,"Handle \"%s\" does not exist in yep file %s\n", handle, file);
        return (struct yep_data_info){.data = NULL, .size = 0};
    }

    // assuming we didnt fail, we have the header data
    // printf("Resource:\n");
    // printf("    Name: %s\n", name);
    // printf("    Offset: %d\n", offset);
    // printf("    Size: %d\n", size);
    // printf("    Uncompressed size: %d\n", uncompressed_size);
    // printf("    Compression type: %d\n", compression_type);
    // printf("    Data type: %d\n", data_type);

    // seek to the offset
    fseek(yep_file, offset, SEEK_SET);

    // read the data
    char *data = malloc(size + 1); // null terminator
    fread(data, sizeof(char), size, yep_file);

    // null terminate the data
    if(compression_type == YEP_COMPRESSION_NONE)
        data[size] = '\0';

    // printf("DATA VALUE LITERAL: %s\n", data);

    // if the data is compressed, decompress it
    if(compression_type == YEP_COMPRESSION_ZLIB){
        char *decompressed_data;
        if(decompress_data(data, size, &decompressed_data, uncompressed_size) != 0){
            yep_logf(yep_log_warning,"!!!Error decompressing data!!!\n");
            return (struct yep_data_info){.data = NULL, .size = 0};
        }

        // printf("Decompressed %s from %d bytes to %d bytes\n", handle, size, uncompressed_size);
        // printf("    Compression ratio: %f\n", (float)uncompressed_size / (float)size);
        // printf("    Compression percentage: %f%%\n", ((float)uncompressed_size / (float)size) * 100.0f);
        // printf("    Compression savings: %d bytes\n", uncompressed_size - size);
        // printf("    DATA: %s\n", decompressed_data);

        // free the original data
        free(data);

        // set the data to the decompressed data
        data = decompressed_data;
        size = uncompressed_size;
    }

    // create return data
    struct yep_data_info info;
    info.data = data;
    info.size = size;

    // return the data
    return info;
}

void yep_initialize(){
    yep_logf(yep_log_info,"Initializing yep subsystem...\n");
    yep_pack_list.entry_count = 0;
}

void yep_shutdown(){
    _yep_close_file();

    if(yep_pack_list.head != NULL){
        struct yep_header_node *itr = yep_pack_list.head;
        while(itr != NULL){
            struct yep_header_node *next = itr->next;
            free(itr->fullpath);
            free(itr);
            itr = next;
        }
    }

    yep_logf(yep_log_info,"Shutting down yep subsystem...\n");
}

// forward decl
void _yep_walk_directory_v2(char *dir_path);

// Function to normalize path separators to forward slashes
static void normalize_path_separators(char *path) {
    for (char *p = path; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
}

// Global variable to store the original root directory path for relative path calculation
static char *yep_pack_root_path = NULL;

static SDL_EnumerationResult SDLCALL _recurse_dir_callback(void *userdata, const char *dirname, const char *fname) {
    (void)userdata; // unused
    
    char full_path[4096];
    snprintf(full_path, sizeof(full_path), "%s%s", dirname, fname);
    
    // Normalize path separators in full_path for consistent comparison
    normalize_path_separators(full_path);

    // Check if path is a file
    SDL_PathInfo path_info;
    if (!yep_get_path_info(full_path, &path_info)) {
        yep_logf(yep_log_error,"yep traverse: Error getting path info for file %s\n", full_path);
        return SDL_ENUM_CONTINUE;
    }
    if (path_info.type == SDL_PATHTYPE_FILE) {
        // Calculate the relative path from the original root directory
        char *relative_path;
        if (yep_pack_root_path != NULL) {
            // Normalize the root path for comparison
            char normalized_root[4096];
            strncpy(normalized_root, yep_pack_root_path, sizeof(normalized_root) - 1);
            normalized_root[sizeof(normalized_root) - 1] = '\0';
            normalize_path_separators(normalized_root);
            
            // Calculate relative path from the original root
            size_t root_len = strlen(normalized_root);
            if (strncmp(full_path, normalized_root, root_len) == 0) {
                relative_path = full_path + root_len;
                // Skip leading path separator if present
                if (*relative_path == '/' || *relative_path == '\\') {
                    relative_path++;
                }
            } else {
                yep_logf(yep_log_error,"Error: file %s is not within the root directory %s\n", full_path, normalized_root);
                return SDL_ENUM_CONTINUE;
            }
        } else {
            // Fallback to old behavior if root path is not set
            relative_path = full_path + strlen(dirname) + 1;
        }        // Convert backslashes to forward slashes for consistent storage
        char normalized_relative_path[256];
        strncpy(normalized_relative_path, relative_path, sizeof(normalized_relative_path) - 1);
        normalized_relative_path[sizeof(normalized_relative_path) - 1] = '\0';
        
        for (char *p = normalized_relative_path; *p; p++) {
            if (*p == '\\') {
                *p = '/';
            }
        }

        // Skip any leading path separators that might still be present
        char *final_relative_path = normalized_relative_path;
        while (*final_relative_path == '/' || *final_relative_path == '\\') {
            final_relative_path++;
        }        // if the relative path plus its null terminator is greater than 64 bytes, we reject packing this and alert the user
        if(strlen(final_relative_path) + 1 > 64){
            yep_logf(yep_log_error,"Error: file %s has a relative path that is too long to pack into a yep file\n", full_path);
            return SDL_ENUM_CONTINUE;
        }

        // add a yep header node with the relative path
        struct yep_header_node *node = malloc(sizeof(struct yep_header_node));

        // set the name field to zeros so I dont lose my mind reading hex output
        memset(node->name, 0, 64);

        // set the full path
        node->fullpath = strdup(full_path);

        // set the name
        sprintf(node->name, "%s", final_relative_path);
        node->name[strlen(final_relative_path)] = '\0'; // ensure null termination

        // add the node to the LL
        node->next = yep_pack_list.head;
        yep_pack_list.head = node;

        // increment the entry count
        yep_pack_list.entry_count++;
    }
    else if (path_info.type == SDL_PATHTYPE_DIRECTORY) {
        // If it's a directory, recurse into it
        _yep_walk_directory_v2(full_path);
    } else {
        yep_logf(yep_log_debug,"yep traverse: Skipping non-file path %s\n", full_path);
    }

    return SDL_ENUM_CONTINUE;
}

/*
    Recursively walk the target pack directory and create a LL of files to be packed
*/
void _yep_walk_directory_v2(char *dir_path) {
    SDL_PathInfo path_info;
    if(!yep_get_path_info(dir_path, &path_info)) {
        yep_logf(yep_log_error,"yep traverse: Error getting path info for directory %s\n", dir_path);
        return;
    }

    // Check if the path is a directory
    if (path_info.type != SDL_PATHTYPE_DIRECTORY) {
        yep_logf(yep_log_error,"yep traverse: Path %s is not a directory\n", dir_path);
        return;
    }

    SDL_EnumerateDirectory(dir_path, _recurse_dir_callback, NULL);
}

/*
    Returns the size of a file in bytes
*/
uint32_t get_file_size(FILE *file) {
    fseek(file, 0L, SEEK_END);
    uint32_t size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    return size;
}

/*
    Reads a full file into memory and returns a pointer to it

    Assumes the file is open and seeked to the beginning
*/
char* read_file_data(FILE *file, uint32_t size) {
    char *data = malloc(size);
    fread(data, sizeof(char), size, file);
    return data;
}

/*
    Writes data to a pack file at a given offset
*/
void write_data_to_pack(FILE *pack_file, uint32_t offset, char *data, uint32_t size) {
    fseek(pack_file, offset, SEEK_SET);
    fwrite(data, sizeof(char), size, pack_file);
}

/*
    Updates a pack file header with details of data just written
*/
void update_header(FILE *pack_file, int entry_index, uint32_t offset, uint32_t size, uint8_t compression_type, uint32_t uncompressed_size, uint8_t data_type) {
    int header_start = 3;
    
    // get where this specific header starts, and move to its offset field (name is already set)
    int header_offset = header_start + (entry_index * YEP_HEADER_SIZE_BYTES) + 64;
    fseek(pack_file, header_offset, SEEK_SET);

    // write the data offset and data size
    fwrite(&offset, sizeof(uint32_t), 1, pack_file);
    fwrite(&size, sizeof(uint32_t), 1, pack_file);

    // write the compression type, uncompressed size and data type
    fwrite(&compression_type, sizeof(uint8_t), 1, pack_file);
    fwrite(&uncompressed_size, sizeof(uint32_t), 1, pack_file);
    fwrite(&data_type, sizeof(uint8_t), 1, pack_file);
}

void write_pack_file(FILE *pack_file) {
    // holds the start of the header for our current entry
    uint32_t data_start = 3 + (yep_pack_list.entry_count * YEP_HEADER_SIZE_BYTES);

    // holds the end of the data pack
    uint32_t data_end = data_start;

    // holds the current entry
    int current_entry = 0;

    printf("\n"); // start the progress bar on a new line

    struct yep_header_node *itr = yep_pack_list.head;
    while(itr != NULL){

        FILE *file_to_write = fopen(itr->fullpath, "rb");
        if (file_to_write == NULL) {
            yep_logf(yep_log_error,"Error opening yep file to pack yep: %s\n", itr->fullpath);
            exit(1);
        }

        uint32_t data_size = get_file_size(file_to_write);
        uint32_t uncompressed_size = data_size;
        char *data = read_file_data(file_to_write, data_size);
        fclose(file_to_write);

        // somewhere here is where we would perform our compression or
        // manipulation of the data depending on its format
        uint8_t compression_type = (uint8_t)YEP_COMPRESSION_NONE;
        uint8_t data_type = (uint8_t)YEP_DATATYPE_MISC;

        if(
            data_size > 256
            // here is where we can && exclusion conditions, like bytecode
        ){
            compression_type = (uint8_t)YEP_COMPRESSION_ZLIB;
        }

        // compress this data with zlib
        if(compression_type == YEP_COMPRESSION_ZLIB){
            char *compressed_data;
            size_t compressed_size;
            compress_data(data, data_size, &compressed_data, &compressed_size);

            // printf("Compressed %s from %d bytes to %d bytes\n", itr->fullpath, data_size, compressed_size);
            // printf("    Compression ratio: %f\n", (float)compressed_size / (float)data_size);
            // printf("    Compression percentage: %f%%\n", ((float)compressed_size / (float)data_size) * 100.0f);
            // printf("    Compression savings: %d bytes\n", data_size - compressed_size);
            // printf("    DATA: %s\n", compressed_data);

            // free the original data
            free(data);

            // set the data to the compressed data
            data = compressed_data;
            data_size = compressed_size;
        }

        // write the actual data from our data file to the pack file
        write_data_to_pack(pack_file, data_end, data, data_size);

        // update the pack file header with the location and information about the data we wrote
        update_header(pack_file, current_entry, data_end, data_size, compression_type, uncompressed_size, data_type);

        // free the data
        free(data);

        // shift the end pointer of the data pack file
        data_end += data_size;

        // incr
        itr = itr->next;
        current_entry++;

        displayProgressBar(current_entry, yep_pack_list.entry_count);
    }
    printf("\n\n"); // let next log start on new line
    fclose(pack_file);

    // clean up global pack list and variables
    struct yep_header_node *itr2 = yep_pack_list.head;
    while(itr2 != NULL){
        struct yep_header_node *next = itr2->next;
        free(itr2->fullpath);
        free(itr2);
        itr2 = next;
    }
    yep_pack_list.head = NULL;
    yep_pack_list.entry_count = 0;
}

bool yep_item_exists(const char* file, const char* handle) {
    // open the file
	if(!_yep_open_file(file)){
		yep_logf(yep_log_warning,"Error opening yep file %s\n", file);
		return false;
	}

	// setup the data we will seek out of the yep file
	char name[64];
	uint32_t offset;
	uint32_t size;
	uint8_t compression_type;
	uint32_t uncompressed_size;
	uint8_t data_type;

	// try to get our header
	if(!_yep_seek_header(handle, name, &offset, &size, &compression_type, &uncompressed_size, &data_type)){
		return false;
	}

	return true;
}

bool _yep_pack_directory(char *directory_path, char *output_name){
    yep_logf(yep_log_debug,"Packing directory %s...\n", directory_path);

    // Set the root path for relative path calculation and normalize separators
    yep_pack_root_path = strdup(directory_path);
    normalize_path_separators(yep_pack_root_path);

    // call walk directory (first arg is root, second is current - this is for recursive relative path knowledge)
    _yep_walk_directory_v2(directory_path);

    // Clear the root path after traversal
    free(yep_pack_root_path);
    yep_pack_root_path = NULL;

    yep_logf(yep_log_debug,"Built pack list...\n");

    // print out all the LL nodes
    // struct yep_header_node *itr = yep_pack_list.head;
    // while(itr != NULL){
        // printf("    %s\n", itr->name);
        // printf("    %s\n", itr->fullpath);
        // itr = itr->next;
    // }

    yep_logf(yep_log_debug,"Detected %d entries\n", yep_pack_list.entry_count);

    /*
        Now, we know exactly the size of our entry list, so we can write the headers for each
        with zerod data for the rest of the fields other than its name
    */

    // open the output file
    FILE *file = fopen(output_name, "wb");
    if (file == NULL) {
        yep_logf(yep_log_error,"Error opening yep file %s\n", output_name);
        return false;
    }

    // write the version number (byte 0-1)
    uint8_t version_number = YEP_CURRENT_FORMAT_VERSION;
    fwrite(&version_number, sizeof(uint8_t), 1, file);

    // write the entry count (byte 2-3)
    uint16_t entry_count = yep_pack_list.entry_count;
    fwrite(&entry_count, sizeof(uint16_t), 1, file);

    yep_logf(yep_log_debug,"Writing headers...\n");

    // write the headers
    struct yep_header_node *itr = yep_pack_list.head;
    while(itr != NULL){
        // 64 bytes - name of the resource
        fwrite(itr->name, sizeof(char), 64, file);

        // 4 bytes - offset of the resource
        uint32_t offset = 0;
        fwrite(&offset, sizeof(uint32_t), 1, file);

        // 4 bytes - size of the resource
        uint32_t size = 0;
        fwrite(&size, sizeof(uint32_t), 1, file);

        // 1 byte - compression type
        uint8_t compression_type = 0;
        fwrite(&compression_type, sizeof(uint8_t), 1, file);

        // 4 bytes - uncompressed size
        uint32_t uncompressed_size = 0;
        fwrite(&uncompressed_size, sizeof(uint32_t), 1, file);

        // 1 byte - data type
        uint8_t data_type = 0;
        fwrite(&data_type, sizeof(uint8_t), 1, file);

        // printf("Wrote header for %s\n", itr->name);

        itr = itr->next;
    }

    yep_logf(yep_log_debug,"Writing data...\n");

    // write the data
    write_pack_file(file);

    yep_logf(yep_log_debug,"Done!\n");

    return true;
}

bool yep_force_pack_directory(char *directory_path, char *output_name){
    yep_logf(yep_log_debug,"Forcing pack of directory \"%s\"...\n", directory_path);
    return _yep_pack_directory(directory_path, output_name);
}

bool yep_pack_directory(char *directory_path, char *output_name){
    if(is_dir_outofdate(directory_path, output_name)){
        yep_logf(yep_log_debug,"Target directory \"%s\" is out of date, packing...\n", directory_path);
        return _yep_pack_directory(directory_path, output_name);
    } else {
        yep_logf(yep_log_debug,"Target directory \"%s\" is up to date, skipping...\n", directory_path);
        return true;
    }
}

/*
    YEP TODO:
    - native animation functionality (this will cutout a lot of headers)
    - actually hook an API so engine can get certain types
    - encode the RGBA and PCM data rather than the file binary
*/

/*
    ENGINE API
*/

/*
    Backend impl that takes in full details
*/

struct yep_data_info _yep_misc(const char *handle, const char *file){
    // get and validate the data
    struct yep_data_info data = yep_extract_data(file, handle);
    if(data.data == NULL){
        yep_logf(yep_log_error,"Error: could not get misc data for %s\n", handle);
        return data;
    }

    // return the data
    return data;
}

// /*
//     Accessor functions that abstract the file they come from
// */

// struct yep_data_info yep_resource_misc(const char *handle){
//     return _yep_misc(handle, yep_path("resources.yep"));
// }

// /*
//     Just for ease of use (who cares about LOC) lets provide some accessors for engine resources
// */

// struct yep_data_info yep_engine_resource_misc(const char *handle){
//     return _yep_misc(handle, yep_path("engine.yep"));
// }
