//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------
#include "ar6000_drv.h"

#ifdef AR6K_STATIC_ALLOC

#define MEMORY_BLOCK_SIZE 1500
#define MEMORY_ARRAY_SIZE 100
#define MEMORY_LARGE_ARRAY_SIZE 5
#define MEMORY_LARGE_BLOCK_SIZE 3000

typedef struct _MEMORY_BLOCK {
    A_UINT8 free;
    void *ptr;
} MEMORY_BLOCK;

MEMORY_BLOCK memory_array[MEMORY_ARRAY_SIZE];
MEMORY_BLOCK memory_large_array[MEMORY_LARGE_ARRAY_SIZE];
struct semaphore        memSem;

#ifdef MEM_DEBUG_PRINT
#define mem_debug_printk printk
#else
#define mem_debug_printk
#endif

void a_mem_helper_init(void)
{
    A_UINT32 counter = 0;
    sema_init(&memSem, 1);

    if (down_interruptible(&memSem)) {
        printk("AR6K ERROR - a_mem_helper_init unable to obtain memSem\n");
        BUG();
    }
	
    for (counter = 0; counter < MEMORY_ARRAY_SIZE; counter++)
    {
        memory_array[counter].free = 1;
        memory_array[counter].ptr = NULL;
        memory_array[counter].ptr = kmalloc((MEMORY_BLOCK_SIZE), GFP_KERNEL);
        if (memory_array[counter].ptr == NULL)
        {
            printk("AR6K ERROR - a_mem_helper_init unable to alloc memory\n");
            BUG();
        }
        mem_debug_printk("a_mem_helper_init alloc %x\n",memory_array[counter].ptr);
    }

    for (counter = 0; counter < MEMORY_LARGE_ARRAY_SIZE; counter++)
    {
        memory_large_array[counter].free = 1;
        memory_large_array[counter].ptr = NULL;
        memory_large_array[counter].ptr = kmalloc((MEMORY_LARGE_BLOCK_SIZE), GFP_KERNEL);
        if (memory_large_array[counter].ptr == NULL)
        {
            printk("AR6K ERROR - a_mem_helper_init unable to alloc memory\n");
            BUG();
        }
        mem_debug_printk("a_mem_helper_init alloc %x\n",memory_large_array[counter].ptr);
    }

    mem_debug_printk("a_mem_helper_init exit\n");
    up(&memSem);	
}

void a_mem_helper_destroy(void)
{
    A_UINT32 counter = 0;

    if (down_interruptible(&memSem)) {
        printk("AR6K ERROR - a_mem_helper_init unable to obtain memSem\n");
        BUG();
    }
	
    for (counter = 0; counter < MEMORY_ARRAY_SIZE; counter++)
    {
        kfree(memory_array[counter].ptr);
    }

    for (counter = 0; counter < MEMORY_LARGE_ARRAY_SIZE; counter++)
    {
        kfree(memory_large_array[counter].ptr);
    }

    mem_debug_printk("a_mem_helper_destroy exit\n");
    up(&memSem);
}


void* a_mem_alloc_helper(size_t msize, int type, const char *func, int lineno)
{
    A_UINT32 counter = 0;

    if (down_interruptible(&memSem)) {
        printk("AR6K ERROR - a_mem_alloc_helper unable to obtain memSem\n");
        return NULL;
    }

    mem_debug_printk("a_mem_alloc_helper %s ask for %d\n",func, msize);

    if (msize > MEMORY_BLOCK_SIZE)
    {
        if (msize < MEMORY_LARGE_BLOCK_SIZE)
        {
            for (counter = 0; counter < MEMORY_LARGE_ARRAY_SIZE; counter++)
            {
                if (memory_large_array[counter].free)
                {
                    memory_large_array[counter].free = 0;
                    mem_debug_printk("a_mem_alloc_helper return %x\n",memory_large_array[counter].ptr);
                    up(&memSem);			
                    return memory_large_array[counter].ptr;
                }
            }
        }
        else
        {
            BUG();
            printk("AR6K ERROR - a_mem_alloc_helper unable to alloc size %d\n",msize);
            up(&memSem);
        }
    }
    else
    {

        for (counter = 0; counter < MEMORY_ARRAY_SIZE; counter++)
        {
            if (memory_array[counter].free)
            {
                memory_array[counter].free = 0;
                mem_debug_printk("a_mem_alloc_helper return %x\n",memory_array[counter].ptr);
                up(&memSem);			
                return memory_array[counter].ptr;
            }
        }
    }
	
    mem_debug_printk("a_mem_alloc_helper failed to find memory\n");
    up(&memSem);
    return NULL;
}

void a_mem_free_helper(void *ptr)
{
    A_UINT32 counter = 0;

    if (down_interruptible(&memSem)) {
        printk("AR6K ERROR - a_mem_alloc_helper unable to obtain memSem\n");
    }

    for (counter = 0; counter < MEMORY_ARRAY_SIZE; counter++)
    {
        if (memory_array[counter].ptr == ptr)
        {
            mem_debug_printk("a_mem_free_helper return %x\n",memory_array[counter].ptr);
            memory_array[counter].free = 1;
            up(&memSem);
            return;
        }
    }

    for (counter = 0; counter < MEMORY_LARGE_ARRAY_SIZE; counter++)
    {
        if (memory_large_array[counter].ptr == ptr)
        {
            mem_debug_printk("a_mem_free_helper return %x\n",memory_large_array[counter].ptr);
            memory_large_array[counter].free = 1;
            up(&memSem);
            return;
        }
    }

    printk("AR6K ERROR - a_mem_free_helper received invalid ptr %x\n",ptr);
    BUG();
    up(&memSem);
}

#endif


