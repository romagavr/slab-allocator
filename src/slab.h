#ifndef SLAB_H
#define SLAB_H
#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>
#include<memory.h>
#include<unistd.h>

#define MAX_NUMBER_OF_SLAB_CLASSES 64
#define BANK_OF_FREE_SLABS 0
#define CHUNK_ALIGN_BYTES 8

//Flags
/* temp */
#define ITEM_SLABBED 4

/* Item was fetched at least once in its lifetime */
#define ITEM_FETCHED 8
/* Appended on fetch, removed on LRU shuffling */
#define ITEM_ACTIVE 16
/* If an item's storage are chained chunks. */
#define ITEM_CHUNKED 32
#define ITEM_CHUNK 64
/* ITEM_data bulk is external to item */
#define ITEM_HDR 128
/* additional 4 bytes for item client flags */
#define ITEM_CFLAGS 256
/* item has sent out a token already */
#define ITEM_TOKEN_SENT 512
/* reserved, in case tokens should be a 2-bit count in future */
#define ITEM_TOKEN_RESERVED 1024
/* if item has been marked as a stale value */
#define ITEM_STALE 2048
//

typedef struct {
	uint16_t maxSlabCount; 	// maximum elements in array of slabs
	uint8_t align;
	uint32_t maxbytes;     	// maximum size of mmaped precache memory
	size_t slabPageSize;         // 1024*1024
	size_t minChunkSize;		// 48
	size_t maxChunkSize;		// settings.slabPageSize / 2;
	float factor;
} settings;


void* slubAlloc(size_t size);
uint8_t slubFree(void *object);

//init custom settings
uint8_t preSetInit(settings *s);

///////////////////////

#endif
