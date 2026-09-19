#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdalign.h>
#include <pthread.h>

/* 
/ Every memory block contains a header and a memory block  
/ The header tracks the size of the entire block and its allocation status 
/ and contains a pointer to the next block in memory
/ Header file is 16 bytes aligned, and it contains both the size of the memory,
/ and a pointer to the next 16 bytes aligned header
*/

alignas(16) struct header {
    size_t sz;
    unsigned is_free;
    struct header* next;
};
typedef struct header header_t;

header_t *head, *tail;

header_t* get_free_block(size_t size) {
    header_t* curr = head;
    /*
    / Check if the current block is free and that the block size is 
    / greater than requested memory size
    */
    while (curr) {
        if (curr->is_free && curr->sz >= size) {
            return curr;
        }
        curr = curr->next; 
    }
}
/*
/ brk pointer points to the end of the heap
/ sbrk is used to request extra memory on the heap
/ sbrk(+x) requests the system to allocate more memory on the heap,
/ and sbrk(-x) is used to decrement memory on the heap
/ use mmap for modern version
*/ 
pthread_mutex_t global_malloc_lock;
void* malloc(size_t sz) {
    size_t total_size;
    void* block;
    header_t *header;

    if (!sz) {
        return NULL;
    }
    pthread_mutex_lock(&global_malloc_lock); // acquire lock
    header = get_free_block(sz); // start of a free block
    if (header) {
        header->is_free = 0;
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(header + 1); // +1 = 1 * sizeof(*header) -> moves past header struct
    }

    size_t aligned_size = (sz + 15) & ~15; // rounds sz up the nearest multiple of 16
    total_size = sizeof(header_t) + aligned_size;
    block = sbrk(total_size); // alloc and moves pointer on the heap upward
    if (block == (void*)-1) {
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }
    header = block; // move header to where block is
    header->sz = aligned_size;
    header->is_free = 0;
    header->next = NULL;
    if (!head) {
        head = header;
    }
    if (tail) {
        tail->next = header; // before, tail represents the current last node of list
    }
    tail = header;
    pthread_mutex_unlock(&global_malloc_lock);
    return (void*)(header + 1);
}

void free(void *block) {
    header_t *header, *tmp;
    void* program_break;

    if (!block) {
        return;
    }
    pthread_mutex_lock(&global_malloc_lock);
    header = (header_t*)block - 1; // block is memory section, block - 1 is the header metadata

    program_break = sbrk(0); // the end of the heap memory pointer
    if ((char*)block + header->sz == program_break) {
        if (head == tail) { // both point to same memory block
            head = tail = NULL;
        }
        else {
            tmp = head; // singly list, need to traverse through every node to find the last node
            while (tmp->next && tmp->next != tail) {
                tmp = tmp->next;
            }
            // If we reach here, then we haved the second to last node
            // The next one is the block to be freed
            tmp->next = NULL;
            tail = tmp;
        }
        sbrk(0 - sizeof(header_t) - header->sz); // release the memory to the system by subtracting from the current brk pointer
    }
    header->is_free = 1;
    pthread_mutex_unlock(&global_malloc_lock);
    return;
}