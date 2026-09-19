#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdalign.h>
#include <pthread.h>
#include <string.h>

/* 
/ Every memory block contains a header and a memory block  
/ The header tracks the size of the entire block and its allocation status 
/ and contains a pointer to the next block in memory
/ Header file is 16 bytes aligned, and it contains both the size of the memory,
/ and a pointer to the next 16 bytes aligned header
*/
size_t total_allocated_bytes = 0;
size_t total_deallocated_bytes = 0;

struct header {
    size_t sz;
    unsigned is_free;
    struct header* next;
} __attribute__((aligned(16)));
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
    return NULL;
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
    total_allocated_bytes += total_size;
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
        total_deallocated_bytes += sizeof(header_t) + header->sz;
        sbrk(0 - sizeof(header_t) - header->sz); // release the memory to the system by subtracting from the current brk pointer
    }
    header->is_free = 1;
    pthread_mutex_unlock(&global_malloc_lock);
    return;
}

/*
/ Allocates memory for an array of num elements of nsz bytes each and 
/ returns a pionter to the allocated memory. The memory is set to all 0s
*/

void* calloc(size_t num, size_t nsz) {
    void* block;
    if (!num || !nsz) {
        return NULL;
    }
    // Check for multiplicative overflow
    if (num != 0 && nsz > SIZE_MAX / num) {
        return NULL;
    }
    size_t alloc_size = num * nsz;
    block = malloc(alloc_size);
    if (!block) {
        return NULL;
    }
    // fill *ptr with value of length alloc_size 
    memset(block, 0, alloc_size);
    return block;
}

/*
/ Changes the size of the given block to size sz
*/
void* realloc(void* block, size_t sz) {
    header_t* header;
    void* ret;
    if (!block || !sz) {
        return NULL;
    }    
    header = (header_t*)block - 1;
    if (header->sz >= sz) { // the block size already fulfills request
        return block;
    }
    ret = malloc(sz);
    if (ret) {
        // mempcy(*dst, *src, size_t n)
        memcpy(ret, block, header->sz);
        free(block);
    }
    return ret;
}

int main() {
    // Explicitly unbuffer stdout so lines print instantly even if it crashes mid-run
    setvbuf(stdout, NULL, _IONBF, 0);

    void* initial_break = sbrk(0);
    
    printf("Starting system heap pointer: %p\n\n", initial_break);

    printf("[Action] Requesting 2 memory blocks...\n");
    void* p1 = malloc(100);  
    
    printf("Size allocated for p1: %zu bytes\n", ((header_t*)p1 - 1)->sz); // 16 * 7

    void* p2 = malloc(200);  
    
    printf("-> Internally Tracked Allocations: %zu bytes\n", total_allocated_bytes);
    printf("-> Actual System Heap Shift:       %ld bytes\n\n", (char*)sbrk(0) - (char*)initial_break);

    printf("[Action] Releasing tail block (p2)...\n");
    free(p2);
    
    printf("-> Internally Tracked Deallocations: %zu bytes\n", total_deallocated_bytes);
    printf("-> Active System Heap Footprint:     %ld bytes\n\n", (char*)sbrk(0) - (char*)initial_break);

    printf("[Action] Releasing remaining block (p1)...\n");
    free(p1);

    printf("-> Final Unreturned Balance:         %ld bytes (0 = Clean)\n", (char*)sbrk(0) - (char*)initial_break);
    printf("=== Process Completed Successfully ===\n");

    return 0;
}