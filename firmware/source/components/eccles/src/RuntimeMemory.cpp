//core implementation of RuntimeAllocations used to minimize the usage
//of heap on the ESP system, designed and programmed by Nwobodo Ecclesiastes Chidera
//PORT NOTE: this entire allocator is pure C++ over raw FreeRTOS primitives (eccles_mutex/
//eccles_lock/eccles_unlock/eccles_createLock, all of which are xSemaphoreCreateMutex/Take/Give
//under the hood, unchanged between the arduino build and this esp-idf build), so nothing in
//this file's logic needed to change at all

#include "RuntimeMemory.h"

ECCLES_API {

    //holds pool type definitions
    enum class POOL_TYPE : e_uint8 {
        A,B,C
    };

    //pool layout constants: change these and every offset/registry size below
    //recalculates automatically instead of needing manual updates.
    //_SHIFT constants are log2 of their matching _BLOCK size: every block size
    //here (64/128/256) is a power of two, so size*BLOCK and size/BLOCK can be
    //done as size<<SHIFT and size>>SHIFT instead of a real multiply/divide
    constexpr e_uint8  POOL_A_COUNT = 80;  constexpr e_uint16 POOL_A_BLOCK = 64;  constexpr e_uint8 POOL_A_SHIFT = 6;
    constexpr e_uint8  POOL_B_COUNT = 40;  constexpr e_uint16 POOL_B_BLOCK = 128; constexpr e_uint8 POOL_B_SHIFT = 7;
    constexpr e_uint8  POOL_C_COUNT = 40;  constexpr e_uint16 POOL_C_BLOCK = 256; constexpr e_uint8 POOL_C_SHIFT = 8;

    //the static pool
    static e_uint8 pool[((e_uint32)POOL_A_COUNT<<POOL_A_SHIFT) + ((e_uint32)POOL_B_COUNT<<POOL_B_SHIFT) + ((e_uint32)POOL_C_COUNT<<POOL_C_SHIFT)];

    //pool offsets, derived from the layout constants above instead of hardcoded
    constexpr e_uint16 OFFSET_A = 0;
    constexpr e_uint16 OFFSET_B = OFFSET_A + (e_uint16)((e_uint32)POOL_A_COUNT<<POOL_A_SHIFT);
    constexpr e_uint16 OFFSET_C = OFFSET_B + (e_uint16)((e_uint32)POOL_B_COUNT<<POOL_B_SHIFT);

    /*

    we previously intend to use this to index blocks
    like poolA[0][1], but we now found out that its a waste of calculation
    to get this index block from pool reg and addresses, we still leave this though
    incase we might need it later

    //poolA: 80 blocks of 64 bits used for smaller data
    static e_uint8 (*poolA)[64] = (e_uint8 (*)[64]) &pool[OFFSET_A]; //offset to pool A
    
    //poolB: 40 blocks of 128 bits used for medium data
    static e_uint8 (*poolB)[128] = (e_uint8 (*)[128]) &pool[OFFSET_B]; //offset to pool B
    
    //poolC: 40 blocks of 256 data used for bigger data and concatenated for larger ones
    static e_uint8 (*poolC)[256] = (e_uint8 (*)[256]) &pool[OFFSET_C]; //offset to pool C

    */
    
    //poolRegistry: holds the metadata of each pool block
    static e_uint8 reg[(e_uint16)POOL_A_COUNT + POOL_B_COUNT + POOL_C_COUNT]; //one entry per block, across all pools

    //CONT_MARK: written into every block after the first in a multi-block run,
    //kept distinct from a real run-length value so freeBuffer can tell a run's
    //own start apart from a continuation block. a request that legitimately
    //needs CONT_MARK-many blocks is already impossible (no pool has that many
    //slots), so this value can never collide with a genuine run length
    constexpr e_uint8 CONT_MARK = 255;

    //pool index trackers
    static e_uint8 cursorA = 0; //points directry to pool A registry
    static e_uint8 cursorB = POOL_A_COUNT; //points directly to pool B registry
    static e_uint8 cursorC = POOL_A_COUNT + POOL_B_COUNT; //points directly to pool C registry

    //allocatorLock: guards pool, reg, and all three cursors. e_malloc can touch
    //multiple pools in one call (its C->B->A fallback chain) and reg[] is one
    //shared array across all three pools, so one lock for the whole allocator
    //is simpler and safer than three separate per-pool locks, which could let
    //one task's e_malloc fallback interleave with another task's getFree on a
    //different pool and still race on the same underlying reg[] array
    static eccles_mutex allocatorLock = nullptr;

    //get a free buffer from pool and return it, size indicates how many blocks to get
    e_uint8* getFree(e_uint8 size,POOL_TYPE type){
        e_uint8 *ci = nullptr; //pool cursor
        e_uint16 oft = 0; //pool offset
        e_uint8 sft = 0; //log2 of this pool's block size, used for (x << sft) instead of (x * blockSize)
        e_uint8 rsz = 0; //size in register
        e_uint8 rsb = 0; //where this pool registry begin

        //check which pool is requested
        if(type == POOL_TYPE::A){ //pool A
            ci = &cursorA;
            oft = OFFSET_A;
            sft = POOL_A_SHIFT;
            rsz = POOL_A_COUNT;
            rsb = 0;
        } else if(type == POOL_TYPE::B){ //poolB
            ci = &cursorB;
            oft = OFFSET_B;
            sft = POOL_B_SHIFT;
            rsz = POOL_B_COUNT;
            rsb = POOL_A_COUNT;
        } else if(type == POOL_TYPE::C){ //poolC
            ci = &cursorC;
            oft = OFFSET_C;
            sft = POOL_C_SHIFT;
            rsz = POOL_C_COUNT;
            rsb = POOL_A_COUNT + POOL_B_COUNT;
        }

        e_uint8 *bpt = nullptr; //buffer pointer 

        //guard: a request whose size doesn't fit in this pool at all, or whose
        //span would overflow the e_uint8 index, can never be satisfied here
        if(size == 0 || size > rsz || (e_uint16)rsb + (e_uint16)rsz + (e_uint16)size > 255){
            ECCLES_LOG_LINE("getFree: request exceeds this pool's total block count, not just fragmentation");
            return bpt;
        }

        //look through this index in register and get if its unused.
        //the cursor runs circular: step walks 0..rsz-1, and i is *ci advanced
        for(e_uint8 step = 0;step < rsz;step++){
            e_uint8 i = *ci + step;
            if(i >= rsb + rsz) i -= rsz;

            //don't let a multi-block request scan or land past this pool's own boundary
            if((e_uint16)i + (e_uint16)size > (e_uint16)(rsb + rsz)) continue;
            //check if this index is free together with its corresponding len
            if(reg[i] == 0){ //we also check if the corresponding blocks are free too for multiple applications
                e_boolean f = true; //flag to check if the indices are free
                for(e_uint8 e = i;e < (i + size);e++){
                    if(reg[e] != 0){ f = false; break; }
                }
                if(f){ //this means that this buffer and its corresponding requested blocks are free
                    //lets find where this buffer is from the index. (i - rsb) * ct
                    //becomes a shift since ct (64/128/256) is always a power of two
                    bpt = &pool[((e_uint16)(i - rsb) << sft) + oft];

                    //now update the registry
                    reg[i] = size;
                    //CONT_MARK marks continuation blocks of a multi-block run, kept
                    //distinct from a real 1-block run (size==1) so freeBuffer can
                    //tell apart "this is a run start" from "this is mid-run"
                    for(e_uint8 iu = (i + 1);iu < (i + size);iu++){
                        reg[iu] = CONT_MARK;
                    }

                    //move the cursor to just past this allocation (not just
                    //incrementing the old cursor value) so the next search
                    //starts right after the freshest allocation, wrapping
                    //back to this pool's own start (by subtraction, same as
                    //above) if that runs past the end
                    e_uint8 next = i + size;
                    if(next >= rsb + rsz) next -= rsz;
                    *ci = next;

                    break;
                }
            }
        }
        if(bpt == nullptr){
            ECCLES_LOG_LINE("getFree: size fits this pool but no contiguous run of free blocks was found (fragmentation or pool full)");
        }
        return bpt;
    }

    //give this block back to the pool
    void freeBuffer(e_uint8* buffer){
        //get the offset of this buffer to determine which pool it comes from
        e_uint16 oft = buffer - pool;
        e_uint16 bz = 0;
        e_uint8 sft = 0; //log2 of bz, used to replace /bz and %bz with >>sft and &mask
        e_uint8 rs = 0;
        e_uint32 ofs = 0;

        if(oft < OFFSET_B){ //poolA
            bz = POOL_A_BLOCK;
            sft = POOL_A_SHIFT;
            rs = 0;
            ofs = OFFSET_A;
        } else if(oft < OFFSET_C){ //poolB
            bz = POOL_B_BLOCK;
            sft = POOL_B_SHIFT;
            rs = POOL_A_COUNT;
            ofs = OFFSET_B;
        } else if(oft < sizeof(pool)){ //poolC
            bz = POOL_C_BLOCK;
            sft = POOL_C_SHIFT;
            rs = POOL_A_COUNT + POOL_B_COUNT;
            ofs = OFFSET_C;
        } else return; //not from this pool

        //reject pointers that don't land exactly on a block boundary, this is
        //not a real allocation start so we must not touch the registry for it.
        //(oft - ofs) % bz becomes a bitmask since bz is always a power of two
        if(((oft - ofs) & (bz - 1)) != 0){
            ECCLES_LOG_LINE("e_free: pointer is not a block start, ignoring");
            return;
        }

        //get the index from the registry. (oft - ofs) / bz becomes a shift
        //since bz is always a power of two
        e_uint8 rind = ((oft - ofs) >> sft) + rs;

        //get how many blocks is used
        e_uint8 mb = reg[rind];

        //reject already-freed blocks (mb == 0, a double free) and continuation
        //blocks (mb == CONT_MARK, meaning this index is mid-run, not a run
        //start) -- freeing either here would corrupt or silently no-op on
        //the registry, so refuse and let the caller know via the log
        if(mb == 0){
            ECCLES_LOG_LINE("e_free: buffer already free, ignoring double free");
            return;
        }
        if(mb == CONT_MARK){
            ECCLES_LOG_LINE("e_free: pointer is mid-run, not a block start, ignoring");
            return;
        }

        //resets blocks metadata
        for(e_uint8 i = rind;i < rind + mb;i++){
            reg[i] = 0;
        }

        //disabled: zeroing every freed block costs too much (blocks * up to 256 bytes each)
        //callers that need a zero-initialized buffer must memset themselves after e_malloc
        //memset(buffer,0,mb * bz);
    }

    //give a free buffer from the buffer pool
    e_uint8* e_malloc(e_uint32 size){
        //check if initRuntimeMemory has been called before this if not return nullptr and exit
        if(allocatorLock == nullptr) return nullptr;

        //hold the lock for the whole call, including the fallback chain below,
        //since getFree doesn't lock itself -- a partial lock per getFree call
        //would let another task's e_malloc/e_free interleave between this
        //call's C->B->A attempts and corrupt reg[] or double-claim a block
        eccles_lock(allocatorLock,portMAX_DELAY);

        e_uint8* result = nullptr;

        //find which pool we should allocate from based on the requested buffer size
        if(size <= POOL_A_BLOCK){
            result = getFree(1,POOL_TYPE::A);
        } else if(size <= POOL_B_BLOCK){
            result = getFree(1,POOL_TYPE::B);
        } else if(size <= POOL_C_BLOCK){
            result = getFree(1,POOL_TYPE::C);
        } else { //bigger pool,here is the deal, get multiple blocks
            //widen rq/rqm to e_uint32 while computing them: size can be far
            //larger than 255 blocks' worth, and narrowing to e_uint8 too early
            //would silently truncate (e.g. 312 blocks wrapping to 56) and hand
            //back a buffer much smaller than the caller asked for.
            //size/BLOCK and size%BLOCK become a shift and a bitmask since every
            //BLOCK size (64/128/256) is a power of two
            e_uint32 rq = size >> POOL_C_SHIFT;
            e_uint32 rqm = size & (POOL_C_BLOCK - 1);
            e_uint8 *bmp = nullptr;

            /*
                we proposed on a clever multi-block allocator where we used smaller blocks for multi-blocks thats a little bigger than 256
                suppose someone requested a buffer with size 257 its only one byte bigger and using 2 * 256 blocks will waste 255 memory blocks
                but if we use 5 64 blocks we will only waste 63 bytes, but i found out that the logic makes the code heavily complex so we removed
                it for now but we are still looking forward to making it a feature
            */
            
            //pools can fragment over time especially if not freed on time so if the bigger blocks can't give us the amount we need we check smaller ones
            //to see if they got idle block
            //try block C, but only if the block count actually fits in an e_uint8,
            //getFree rejects an oversized count anyway but this avoids truncating first
            e_uint32 needC = (rqm == 0 ? rq : rq + 1);
            bmp = (needC <= 255) ? getFree((e_uint8)needC,POOL_TYPE::C) : nullptr;

            //try block B
            if(!bmp){
                rq = size >> POOL_B_SHIFT;
                rqm = size & (POOL_B_BLOCK - 1);
                e_uint32 needB = (rqm == 0 ? rq : rq + 1);
                bmp = (needB <= 255) ? getFree((e_uint8)needB,POOL_TYPE::B) : nullptr;

                //try A
                if(!bmp){
                    rq = size >> POOL_A_SHIFT;
                    rqm = size & (POOL_A_BLOCK - 1);
                    e_uint32 needA = (rqm == 0 ? rq : rq + 1);
                    bmp = (needA <= 255) ? getFree((e_uint8)needA,POOL_TYPE::A) : nullptr;
                }
            }
            if(bmp == nullptr){
              ECCLES_LOG_LINE("buffer allocation failed: no pool had a large enough contiguous run free");
            }
            result = bmp;
        }

        eccles_unlock(allocatorLock);
        return result;
    }

    //release this buffer to the pool so it can be used again
    void e_free(e_uint8* buffer){
        if(buffer == nullptr || buffer < pool || buffer >= pool + sizeof(pool) || allocatorLock == nullptr) return;
        eccles_lock(allocatorLock,portMAX_DELAY);
        freeBuffer(buffer);
        eccles_unlock(allocatorLock);
    }

    //get a snapshot of how many blocks are used/free in each pool right now.
    //a block is "used" whether it's a run start or a CONT_MARK continuation,
    //since either way that registry slot isn't available for a new allocation
    e_PoolStats e_getStats(){
        if(allocatorLock == nullptr) return {};
        eccles_lock(allocatorLock,portMAX_DELAY);

        e_PoolStats st{};

        for(e_uint8 i = 0;i < POOL_A_COUNT;i++){
            if(reg[i] == 0) st.freeA++; else st.usedA++;
        }
        for(e_uint8 i = POOL_A_COUNT;i < POOL_A_COUNT + POOL_B_COUNT;i++){
            if(reg[i] == 0) st.freeB++; else st.usedB++;
        }
        for(e_uint16 i = (e_uint16)POOL_A_COUNT + POOL_B_COUNT;i < (e_uint16)POOL_A_COUNT + POOL_B_COUNT + POOL_C_COUNT;i++){
            if(reg[i] == 0) st.freeC++; else st.usedC++;
        }

        eccles_unlock(allocatorLock);
        return st;
    }

    //initialize the allocator mutex,this must be called before any 
    //allocator functions can succeed
    void initRuntimeMemory(){
        //must be allocated in main
        allocatorLock = eccles_createLock();
    }
};
