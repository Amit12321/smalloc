#include <unistd.h>
#include <cstring>
#include <sys/mman.h>
#include <stdio.h>

#define SPLIT_MIN 128

typedef struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
} MallocMetadata;

#define MIN_MMAP_SIZE 128*1024
#define META_DATA_SIZE sizeof(MallocMetadata)

static MallocMetadata* head_pointer = nullptr;
static MallocMetadata* tail_pointer = nullptr;
static MallocMetadata* mmap_head_ptr = nullptr;

static void createAlloc(void* point, size_t size) {
    MallocMetadata* temp = (MallocMetadata*)point;
    temp->size = size;
    temp->next = nullptr;
    temp->prev = nullptr;
    temp->is_free = false;

    // for the first allocation 
    if(head_pointer == nullptr){
        head_pointer = temp;
        tail_pointer = temp;
        return;
    }
    // set the prev of the new last block to be the prev tail
    temp->prev = tail_pointer;
    // set the the next of the prev last block to be the new last block and sets the new tail ptr
    tail_pointer-> next = temp;
    tail_pointer = temp;
}

static void split(void* p, size_t size) {
    auto temp = (MallocMetadata*)p;

    auto currNext = temp->next;
    auto prevSize = temp->size;

    // set the new data to the splited block
    temp->size = size;

    // create new metadata
    MallocMetadata* newMeta = (MallocMetadata*)((char*)(temp + 1) + size); //Changed here from temp to temp+1
    newMeta->size = prevSize - size - META_DATA_SIZE;
    newMeta->next = currNext;
    newMeta->prev = temp;
    newMeta->is_free = true;
    if (currNext != nullptr) {
        currNext->prev = newMeta;
    } else {
        tail_pointer = newMeta; // This means we have splitted the last block in the list.
    }
    temp->next = newMeta;
}

void* mmap_smalloc(size_t size){
    auto adr = mmap(NULL,size+META_DATA_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,-1,0);
    if (adr == MAP_FAILED) return nullptr;

    MallocMetadata* it = (MallocMetadata*)adr;
    it->size = size;
    it->is_free = false;
    it->prev = nullptr;
    it->next = nullptr;
    if(mmap_head_ptr == nullptr){
        mmap_head_ptr = it;
    }
    else{
        it->next = mmap_head_ptr;
        mmap_head_ptr->prev = it;
        mmap_head_ptr = it;
    }
    return it + 1;
}

void* enlargment(size_t size){
    auto temp_size = size-tail_pointer->size;
    void* ret = sbrk(temp_size);
    if(ret==(void*)(-1)) return nullptr;
    tail_pointer->size = size;
    tail_pointer->is_free = false;
    return ((MallocMetadata*)tail_pointer +1);
}
void* smalloc(size_t size){
    if(size > 1e8 || size ==0) {
        return nullptr;
    }
    if(size >= MIN_MMAP_SIZE) {
        return mmap_smalloc(size);
    }
    //searching in our global pointer
    auto temp = head_pointer;
    while (temp != nullptr){
        if (temp->is_free && temp->size >= size) {
            if (temp->size >= size + SPLIT_MIN + META_DATA_SIZE) {
                split(temp,size);
            }
            (temp)->is_free=false;
            return (temp+1);
        }
        temp= ((MallocMetadata*)temp)->next;
    }
    // if we got here we dont have allocated block that is free and big enough
    size_t temp_size;
    // if the last block is free but not big enough for the requested block
    if (tail_pointer != nullptr && tail_pointer->is_free) {
        return enlargment(size);
    }

    temp_size = size + sizeof(MallocMetadata);
    //if we are here then we dont have any free memory we already allocated .
    void* ret = sbrk(temp_size);
    if (ret == (void*)(-1)) return nullptr;
    //here we call help function that will manage our global pointer
    createAlloc(ret, size);
    //now were done and ready to return the allocation
    return ((MallocMetadata*)ret +1);
}

void* scalloc(size_t num, size_t size) {
    size_t new_size = num * size;
    void* temp = smalloc(new_size);
    if(!temp) return nullptr;
    std::memset(temp,0,new_size);
    auto it = (MallocMetadata*)temp;
    return it;
}

static void merge(void* first, void* second) {
    //Merging two adjacent block in memory.
    MallocMetadata* first_meta = (MallocMetadata*)first;
    MallocMetadata* second_meta = (MallocMetadata*)second;
    auto newNext = second_meta->next;
    first_meta->size = META_DATA_SIZE + second_meta->size + first_meta->size;
    if (newNext != nullptr) {
        newNext->prev = first_meta;
    }
    else {
        tail_pointer = first_meta;
    }
    first_meta->next = newNext;
}

void mmap_sfree(void* p){
    MallocMetadata* temp =(MallocMetadata*)p - 1;
    auto prev = temp->prev;
    auto next = temp->next;
    auto ret = munmap(temp,temp->size+META_DATA_SIZE);
    if (ret == -1) {
        perror("munmap FAILED\n");
        return;
    }
    if (prev  == nullptr) {
        mmap_head_ptr = next;
        return;
    } else if (next != nullptr) {
        next->prev = prev;
    }
    prev->next = next;
}

void sfree(void* p){
    if(!p) return ;
    MallocMetadata* temp = ((MallocMetadata*)p)-1;
    if(temp->size >= MIN_MMAP_SIZE){
        mmap_sfree(p);
        return;
    }
    MallocMetadata* next = temp->next;
    MallocMetadata* prev = temp->prev;
    if ((prev != nullptr && prev->is_free) && (next != nullptr && next->is_free)) {
        merge(prev, temp);
        merge(prev, next);
        prev->is_free = true;
    } else if (temp->prev != nullptr && temp->prev->is_free){
        merge(prev, temp);
        prev->is_free = true;
    } else if (temp->next != nullptr &&temp->next->is_free){
        merge(temp, next);
        temp->is_free = true;
    }else{
        temp->is_free = true;
    }
}

void* mmap_realloc(void* oldp, size_t size){
    mmap_sfree(oldp);
    return mmap_smalloc(size);
}

void* srealloc(void* oldp, size_t size){
    if (!oldp) return smalloc(size);
    MallocMetadata* temp = ((MallocMetadata*)oldp)-1;
    if (size > 1e8 || size == 0) return nullptr;
    if (temp->size >= MIN_MMAP_SIZE) {
        return mmap_realloc(oldp, size);
    }
    if (temp->size >= size){
        if (temp->size >= size + META_DATA_SIZE + SPLIT_MIN) {
            split(temp, size);
        }
        return oldp;
    }
    //if we are here means we need to allocate more place .
    /// check if the prev is not null and free
    auto prev = temp->prev;
    auto next = temp->next;
    MallocMetadata* newblock = nullptr;
    size_t newSize;
    if (prev != nullptr && prev->is_free && temp->size + prev->size + META_DATA_SIZE >= size) {
        merge(prev, temp);
        newblock = prev;
        newblock->is_free = false;
    } else if(next != nullptr && next->is_free && temp->size + next->size + META_DATA_SIZE >= size){
        merge(temp, next);
        newblock = temp;
    } else if (prev != nullptr && prev->is_free && next != nullptr && next->is_free && temp->size + prev->size + next->size + 2*META_DATA_SIZE >= size){
        merge(prev, temp);
        merge(prev, next);
        newblock = prev;
        newblock->is_free = false;
    }
    if (newblock != nullptr) {
        std::memmove(newblock + 1, oldp, temp->size);
        if (newblock->size >= size + META_DATA_SIZE + SPLIT_MIN) {
            split(newblock, size);
        }
        return newblock+1;
    }

    if (temp == tail_pointer) {
        auto it = head_pointer;

        while (it != nullptr){
            if (it->is_free && it->size >= size) {
                if (it->size >= size + SPLIT_MIN + META_DATA_SIZE) {
                    split(it,size);
                }
                it->is_free=false;
                sfree(tail_pointer);
                std::memmove(it + 1, oldp, temp->size);
                temp->is_free = true;
                return it+1;
            }
            it = ((MallocMetadata*)it)->next;
        }
        return enlargment(size);
    }
    void * ret = smalloc(size);
    if(!ret) return nullptr;
    std::memmove(ret, oldp, temp->size);
    temp->is_free = true;
    sfree(oldp);
    return ret;
}

size_t _num_free_blocks() {
    size_t ret = 0;
    MallocMetadata* it = (MallocMetadata*)head_pointer;
    while (it != nullptr){
        if (it->is_free) ret++;
        it = it -> next;
    }
    return ret;
}

size_t _num_free_bytes() {
    size_t ret = 0;
    MallocMetadata* it = (MallocMetadata*)head_pointer;
    while (it!=nullptr) {
        if (it->is_free) ret += it->size;
        it = it -> next;
    }
    return ret;
}

size_t _num_allocated_blocks(){
    size_t ret = 0;
    MallocMetadata* it = (MallocMetadata*)head_pointer;
    while (it != nullptr) {
        ret++;
        it = it -> next;
    }
    it = (MallocMetadata*)mmap_head_ptr;
    while (it != nullptr) {
        ret++;
        it = it -> next;
    }
    return ret;
}

size_t _num_allocated_bytes(){
    size_t ret = 0;
    MallocMetadata* it = (MallocMetadata*)head_pointer;
    while (it != nullptr) {
        ret += it->size;
        it = it -> next;
    }
    it = (MallocMetadata*)mmap_head_ptr;
    while (it != nullptr) {
        ret += it->size;
        it = it -> next;
    }
    return ret;
}

size_t _size_meta_data(){
    return sizeof(MallocMetadata);
}

size_t _num_meta_data_bytes(){
    return _num_allocated_blocks() * _size_meta_data();
}
