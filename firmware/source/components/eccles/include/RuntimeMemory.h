/*
    we ran into a problem in our previous version where some form of persistent
    storage is needed,we tried malloc but because bluetooth a2dp api has to queue
    copy and queue audio buffers before sending them to audio task and conversation
    api has to also create and copy buffer from the wifi api to queue to audio tasks
    these are frequent operation that uses malloc on this system, very dangerous here
    so we resault to using a preallocated buffer pool and an allocator to reduce the
    risk of memory errors,and we dedicate this file here to solve this problem
*/

#ifndef ECCLES_ESP_RUNTIME_MEMORY
#define ECCLES_ESP_RUNTIME_MEMORY

#include "EcclesTypes.h"

ECCLES_API {


    /*
        search and return an available buffer from the buffer pool NOTE: buffer pools
        are contiguos slice of fixed sized memory, this function will find a pool that fits
        your requested size and if none concatenate pools for you.
        NOTE: make sure to call e_free once the buffer is done so that you will give the buffer
        to any other system that may need it
    */
    e_uint8* e_malloc(e_uint32 size); //returns nullptr if all means failed must call e_free when done

    /*
     give this memory back to the memory managements
     this is very very important,forgetting to call this risks fragmentatio and starves other systems memory
    */
    void e_free(e_uint8* buffer);

    /*
        struct returned by e_getStats, reports how many blocks are currently
        used vs free in each pool, useful for debugging memory pressure
        without having to read the allocator's internals directly
    */
    struct e_PoolStats {
        e_uint8 usedA, freeA; //pool A: 64-byte blocks
        e_uint8 usedB, freeB; //pool B: 128-byte blocks
        e_uint8 usedC, freeC; //pool C: 256-byte blocks
    };

    //get a snapshot of how many blocks are used/free in each pool right now
    e_PoolStats e_getStats();

    //initialize the Runtime Memory guarding mutex, this should be called before
    //any e_malloc or e_free
    void initRuntimeMemory();
};

#endif
