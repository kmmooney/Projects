#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define _UTHREAD_PRIVATE
#include "disk.h"
#include "fs.h"


struct super_block_t{
    char signature[8];
    uint16_t total_blocks;
    uint16_t root_direct_index;
    uint16_t data_block_index;
    uint16_t num_data_blocks;
    uint8_t num_fat_blocks;
    uint8_t padding[4079];
}__attribute__((packed));

struct fat_blocks_t{
    uint16_t block;
};//__attribute__((packed));

struct file{
    char file_name[FS_FILENAME_LEN];
    uint32_t file_size;
    uint16_t file_index; //file DATA index
    uint8_t file_padding[10];
}__attribute__((packed));


struct root_direct_t{
    struct file files[FS_FILE_MAX_COUNT];
}__attribute__((packed));

struct fd_table_entry{
    char file_name[FS_FILENAME_LEN];
    int file_index;
    size_t offset;
    bool available; // true = empty, false = filled
}__attribute__((packed));

struct fd_table_t{
    struct fd_table_entry table[FS_OPEN_MAX_COUNT];
}__attribute__((packed));
                         
struct super_block_t    *super_block;
struct fat_blocks_t     *fat;
struct root_direct_t    *root_direct;
struct fd_table_t       *fd_table;

bool mounted; // true = mounted, false = unmounted
                         
int fs_mount(const char *diskname)
{
    super_block = malloc(BLOCK_SIZE);
    //open file disk
    if(block_disk_open(diskname) == -1){
        return -1;
    }
    
    //read superblock
    if(block_read(0, super_block) == -1) {
        return -1;
    }
    
    if(strncmp(super_block->signature, "ECS150FS", 8) != 0) {
        return -1;
    }
    
    if(super_block->total_blocks != block_disk_count()) {
        return -1;
    }

    //call block_read for FAT
    fat = malloc(super_block->num_fat_blocks * BLOCK_SIZE);

    for(int i = 1; i < (super_block->num_fat_blocks + 1); i++){
        if(block_read(i, (void*)fat + (i-1) * BLOCK_SIZE) == -1){
            return -1;
        }
    }
    root_direct = malloc(sizeof(struct root_direct_t));
    //call block_read for root_direct
    if(block_read(super_block->root_direct_index, root_direct) == -1){
        return -1;
    }
    fd_table = malloc(sizeof(struct fd_table_t));
    // set fd_table to all avaiable
    for(int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        fd_table->table[i].available = true;
    }

    mounted = true;

    return 0;
}

int fs_umount(void)
{
    // check if a FS is currently mounted
    if(!mounted) {
        return -1;
    }
    if(block_write(0, (void*)super_block) == -1) {
        return -1;
    }
    
    for(int i = 1; i < (super_block->num_fat_blocks + 1); i++){
        if(block_write(i, (void*)fat + (i-1) * BLOCK_SIZE) == -1){
            return -1;
        }
    }
    
    if(block_write(super_block->root_direct_index, root_direct) == -1){
        return -1;
    }

    for(int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        fd_table->table[i].available = true;
    }
    free(fd_table);

    free(super_block);
    free(root_direct);
    free(fat);

    if(block_disk_close() == -1) {
        return -1;
    }
    mounted = false;
    return 0;
}

int fs_info(void)
{
    
    int fat_free = 0;
    int rdir_free = 0;
    for(int i = 0; i < super_block->num_data_blocks; i++){
        if(fat[i].block == 0){
            fat_free++;
        }
    }
    
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if(root_direct->files[i].file_size == 0)
            rdir_free++;
    }
    printf("FS Info:\n");
    printf("total_blk_count=%d\n", super_block->total_blocks);
    printf("fat_blk_count=%d\n", super_block->num_fat_blocks);
    printf("rdir_blk=%d\n", super_block->root_direct_index);
    printf("data_blk=%d\n", super_block->data_block_index);
    printf("data_blk_count=%d\n", super_block->num_data_blocks);
    printf("fat_free_ratio=%d/%d\n", fat_free, super_block->num_data_blocks);
    printf("rdir_free_ratio=%d/%d\n", rdir_free, FS_FILE_MAX_COUNT);
    
    return 0;
}

int fs_create(const char *filename)
{
    // check if a FS is currently mounted
    if(!mounted) {
        return -1;
    }
    int i = 0, emptyfileindex = -1;

    //still need to add error check for if fs is not currently mounted

    // check if filename is too long
    if(strlen(filename) > FS_FILENAME_LEN) {
        return -1;
    }

    // check if empty file available
    while(i < FS_FILE_MAX_COUNT && emptyfileindex == -1) {
        if(root_direct->files[i].file_name[0] == '\0') {
            emptyfileindex = i;
        }
        // if filename exists return -1
        if(strcmp(root_direct->files[i].file_name, filename) == 0) {
            return -1;
        }
        i++;
    }
    // if no empty files found
    if(emptyfileindex == -1) {
        return -1;
    }
    //
    strcpy(root_direct->files[emptyfileindex].file_name, filename);
    root_direct->files[emptyfileindex].file_size = 0;
    root_direct->files[emptyfileindex].file_index = 0xFFFF;

    return 0;
}

int fs_delete(const char *filename)
{
    // check if a FS is currently mounted
    if(!mounted) {
        return -1;
    }
    // add error check for file open

    // check if filename is too long
    if(strlen(filename) > FS_FILENAME_LEN) {
        return -1;
    }
    // find filename in root directory
    int deletefileindex = -1;
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if(strcmp(root_direct->files[i].file_name, filename) == 0) {
            deletefileindex = i;
        }
    }
    // if no file of input name found
    if(deletefileindex == -1) {
        return -1;
    }
    uint16_t fat_index = root_direct->files[deletefileindex].file_index;
    
    while(fat_index != 0xFFFF){
        uint16_t next_fat_index = fat[fat_index].block;
        fat[fat_index].block = 0;
        fat_index = next_fat_index;
    }
    
    //set file entry to empty after cleaning data
    root_direct->files[deletefileindex].file_name[0] = '\0';
    root_direct->files[deletefileindex].file_size = 0;
    root_direct->files[deletefileindex].file_index = 0xFFFF;
    
   return 0;
}

int fs_ls(void)
{
    // check if a FS is currently mounted
    if(!mounted) {
        return -1;
    }

    printf("FS Ls:\n");
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if(root_direct->files[i].file_name[0] != '\0') {
            printf("file: %s, size: %d, data_blk: %d\n", root_direct->files[i].file_name, root_direct->files[i].file_size, root_direct->files[i].file_index);
        }
    }
    return 0;
}

int fs_open(const char *filename)
{
    // check if a FS is currently mounted
    if(!mounted) {
        return -1;
    }

    // check if filename is too long
    if(strlen(filename) > FS_FILENAME_LEN) {
        return -1;
    }

    // find filename in root directory
    int RDfileindex = -1;
    for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
        if(strcmp(root_direct->files[i].file_name, filename) == 0) {
            RDfileindex = i;
        }
    }
    // if no file of input name found
    if(RDfileindex == -1) {
        return -1;
    }

    int fd_index = -1;
    int i = 0;
    // check if empty file available
    while(i < FS_OPEN_MAX_COUNT && fd_index == -1) {
        if(fd_table->table[i].available == true) {
            fd_index = i;
        }
        i++;
    }

    // if no available file descriptors
    // (%FS_OPEN_MAX_COUNT files currently open)
    if(fd_index == -1) {
        return -1;
    }
    // enter fd data into available fd index
    strcpy(fd_table->table[fd_index].file_name, filename);
    fd_table->table[fd_index].file_index = RDfileindex;
    // offset initialized as 0
    fd_table->table[fd_index].offset = 0;
    // set as not avaible
    fd_table->table[fd_index].available = false;

    return fd_index;
}

int fs_close(int fd)
{
    // check if a FS is currently mounted
    if(!mounted) {
        return -1;
    }
    // check if fd is out of bounds
    if(fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
        return -1;
    }
    // check if fd is not currently used
    if(fd_table->table[fd].available == true) {
        return -1;
    }
    // set as available
    fd_table->table[fd].available = true;
    
    return 0;
}

int fs_stat(int fd)
{
    // check if a FS is currently mounted
    if(!mounted) {
        return -1;
    }
    // check if fd is not currently open
    if(fd_table->table[fd].available == true) {
        return -1;
    }
    // check if filename is too long
    if(strlen(fd_table->table[fd].file_name) > FS_FILENAME_LEN) {
        return -1;
    }
    // use fd table to get file's RD index
    int index = fd_table->table[fd].file_index;
    // return file size from root directory
    return root_direct->files[index].file_size;
}

int fs_lseek(int fd, size_t offset)
{
    // check if a FS is currently mounted
    if(!mounted) {
        return -1;
    }
    // check if fd is not currently open
    if(fd_table->table[fd].available == true) {
        return -1;
    }
    // check if filename is too long
    if(strlen(fd_table->table[fd].file_name) > FS_FILENAME_LEN) {
        return -1;
    }

    int file_size = fs_stat(fd);

    // check if offset is too large
    if ((int)offset > file_size) {
        return -1;
    }

    fd_table->table[fd].offset = offset;

    return 0;
}

//helper function that returns correct index block
uint16_t correct_block(uint16_t index, int block){
    if(block == 0){
        return index;
    }
    
    for(int i = 0; i < block; i++){
        index = fat[index].block;
    }
    return index;
}

//helper function that determines length to read from bounce_buf
size_t get_length(size_t count, size_t bytes_to_read) {
    
    size_t length;
    if(count < BLOCK_SIZE){
        length = count;
    } else {
        length = BLOCK_SIZE - bytes_to_read;
    }
    return length;
}

int first_avail_fat(){
    for(int i = 0; i < super_block->num_data_blocks; i++){
        if(fat[i].block == 0)
            return i;
    }
    return -1; //no fat available
}


int fs_write(int fd, void *buf, size_t count)
{
    
    if(!mounted) {
         return -1;
    }
    if(buf == NULL){
         return -1;
    }
    // check if fd is not currently open
    if(fd_table->table[fd].available == true) {
        return -1;
    }
    // check if fd is out of bounds
    if(fd < 0 || fd >= FS_OPEN_MAX_COUNT) {
        return -1;
    }
    int write_file_index = fd_table->table[fd].file_index;
    size_t current_offset = fd_table->table[fd].offset;
    uint16_t data_index = root_direct->files[write_file_index].file_index;
    char bounce[BLOCK_SIZE]; //bounce buffer

    if(count == 0){
        printf("hello\n");
        return 0;
    }
    if(data_index == 0xFFFF && count != 0) {//writing to file for first time
        root_direct->files[write_file_index].file_index = first_avail_fat();
        data_index = root_direct->files[write_file_index].file_index;
        fat[data_index].block = 0xFFFF;
    }

    size_t block_start = current_offset / BLOCK_SIZE; //if it equals zero then read from first block
    uint16_t cur_data_block = correct_block(data_index, block_start); //current data block based off offset

    
    
    size_t blocks_to_write = (count + (current_offset % BLOCK_SIZE)) / BLOCK_SIZE; //bytes before end of block
    
    if((count + (current_offset % BLOCK_SIZE)) % BLOCK_SIZE != 0) {
        blocks_to_write++;
    }
    
    
    uint16_t bytes_to_read = current_offset % BLOCK_SIZE;
    uint16_t length = get_length(count, bytes_to_read);
    uint16_t bytes_read = 0;
    int file_size = fs_stat(fd);
    size_t num_blocks = file_size / BLOCK_SIZE;
    
    if(file_size % BLOCK_SIZE != 0){
        num_blocks++;
    }
    
  //  if(cur_data_block == 0xFFFF) {//writing to file for first time
  //      cur_data_block = first_avail_fat();
  //  }
    
 //   if(current_offset + count > num_blocks * BLOCK_SIZE){ //need to allocated new block
   //     int first_avail = first_avail_fat();
   // }
  
    for(size_t i = 0; i < blocks_to_write; i++) {
        block_read(cur_data_block + super_block->data_block_index, (void*)bounce);
        memcpy(bounce + bytes_to_read, (char*)buf + bytes_read, length);
        block_write(cur_data_block + super_block->data_block_index, (void*)bounce);
        
        
        bytes_read = bytes_read + length;
        count = count - length;
        current_offset = current_offset + length;
        bytes_to_read = current_offset % BLOCK_SIZE;
        length = get_length(count, bytes_to_read);
        
        if(fat[cur_data_block].block == 0xFFFF && i != blocks_to_write - 1 ) {
            int first_avail = first_avail_fat();
            fat[first_avail].block = 0xFFFF;
            fat[cur_data_block].block = first_avail;
            cur_data_block = fat[cur_data_block].block;
        } else {
            cur_data_block = fat[cur_data_block].block;
        }
    }
    
    file_size += bytes_read;
    root_direct->files[write_file_index].file_size = file_size;
    fd_table->table[fd].offset += bytes_read;
    return bytes_read;
}

// The number of bytes read can be smaller than @count if there are less than
// @count bytes until the end of the file (it can even be 0 if the file offset
// is at the end of the file)
int fs_read(int fd, void *buf, size_t count)
{
    if(!mounted) {
        return -1;
    }
    
    if(buf == NULL){
        return -1;
    }
    // check if fd is not currently open
    if(fd_table->table[fd].available == true) {
        return -1;
    }
    // check if filename is too long
    if(strlen(fd_table->table[fd].file_name) > FS_FILENAME_LEN) {
        return -1;
    }
    
    int read_file_index = fd_table->table[fd].file_index; //look
    size_t current_offset = fd_table->table[fd].offset;
    uint16_t data_index = root_direct->files[read_file_index].file_index;
    char bounce[BLOCK_SIZE]; //bounce buffer
    
    if((int)(count + current_offset) > (int)root_direct->files[read_file_index].file_size) {
            count = (size_t)root_direct->files[read_file_index].file_size - current_offset;
    }

    size_t blocks_to_read = (count + (current_offset % BLOCK_SIZE)) / BLOCK_SIZE; //bytes before end of block
    
    if((count + (current_offset % BLOCK_SIZE)) % BLOCK_SIZE != 0) {
        blocks_to_read++;
    }
    
    size_t block_start = current_offset / BLOCK_SIZE;
    uint16_t cur_data_block = correct_block(data_index, block_start); //current data block based off offset
    
    
    size_t bytes_to_read = current_offset % BLOCK_SIZE;
    size_t length = get_length(count, bytes_to_read);

    
    size_t bytes_read = 0; //must be returned
    
    for(size_t i = 0; i < blocks_to_read; i++){
        //get block
        
        block_read(cur_data_block + super_block->data_block_index, (void*)bounce);
        
        memcpy((char*)buf + bytes_read, bounce + bytes_to_read, length);
        bytes_read = bytes_read + length;
        count = count - length;
        current_offset = current_offset + length;
        
        bytes_to_read = current_offset % BLOCK_SIZE;
        
        length = get_length(count, bytes_to_read);
        
        cur_data_block = fat[cur_data_block].block;
    }
    
    fd_table->table[fd].offset += bytes_read;
    return bytes_read;
}
