#include<stdio.h>
#include<stdlib.h>
#include"slub.h"

kmem_cache* kmem_cache_create(char *name, size_t size, uint8_t align,
                              void (*constructor)(void *, size_t),
                              void (*destructor)(void *, size_t)) {
                              
    if (!align || !size || align > size) {
        exit(EXIT_FAILURE);
    }

    kmem_cache *cache = malloc(sizeof(kmem_cache));
    if (!cache) {
        exit(EXIT_FAILURE);
    }

    cache->name = name;
    cache->size = size;

    //cache->effsize = align * ((size-1)/align + 1);
    cache->constructor = constructor;
    cache->destructor = destructor;
    cache->slabs = 0;
    cache->slabs_back = 0;
    cache->alBSize = (size_t)(size/align);
    cache->alSize = align * cache->alBSize;
    if (size % align) cache->alSize += align;
    
    if (cache->alSize <= SMALL_OBJ_SZ){
    	cache->maxBufCount = (uint32_t)((PAGE_SZ - sizeof(kmem_slab))/cache->alBSize);
    } else {
    	//TODO: continue
    	cache->maxBufCount = 1;
    }

    return cache; 
}

void kmem_cache_grow(kmem_cache *cp) {
    void *base = 0;

    if (cp->alSize <= SMALL_OBJ_SZ) {
        if (posix_memalign(&base, PAGE_SZ, PAGE_SZ)){
            exit(EXIT_FAILURE);
        }
        // base - char or void ???
        kmem_slab *slab = (kmem_slab *)base + PAGE_SZ - sizeof(kmem_slab);
        slab->next = slab->prev = slab;        
        slab->bufcount = 0u;
        slab->free_list = base;
        
        size_t offset = (cp->maxBufCount > 1) ? (cp->alBSize * (cp->maxBufCount-1)) : 0;
        if (offset > 0) {
        	uint8_t *lastbuf = (uint8_t*)base + offset;
	        for (uint8_t *p=base; p < lastbuf; p+=cp->alBSize) 
             		*((void **)p) = p + cp->alBSize;
        }
    }
}

