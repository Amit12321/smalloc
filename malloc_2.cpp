#include <unistd.h>
#include <cstring>

typedef struct MallocMetadata {
 size_t size;
 bool is_free;
 MallocMetadata* next;
 MallocMetadata* prev;
}MallocMetadata;

static MallocMetadata* head_pointer = nullptr;
static MallocMetadata* tail_pointer = nullptr;

static void createAlloc(void* point, size_t size){
	MallocMetadata* temp= (MallocMetadata*)point;

	temp->size=size;
	temp->next=nullptr;
	temp->prev=nullptr;
	temp->is_free=false;

	// for the first allocation
	if(head_pointer==nullptr){
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

void* smalloc(size_t size){
	if(size > 1e8 ||size ==0) {
		return nullptr;
	}
	//searching in our global pointer 
	auto temp=head_pointer;
	while (temp!=nullptr){
		if(temp->is_free && temp->size >= size){
			(temp)->is_free=false;
			return (temp+1);
		}
		temp= ((MallocMetadata*)temp)->next;
	}
	//if we are here then we dont have any free memory we already allocated .
	size_t temp_size = size + sizeof(MallocMetadata);
	void* ret =sbrk(temp_size);
	if(ret==(void*)(-1)) return nullptr;
	//here we call help function that will manage our global pointer 
    createAlloc(ret, size);
	//now were done and ready to return the allocation 
	return ((MallocMetadata*)ret +1);
}

void *scalloc(size_t num, size_t size) {
	size_t new_size = num * size;
	void* temp = smalloc(new_size); 
	if(!temp) return nullptr;
	std::memset(temp,0,new_size);
	return temp;
}

void sfree(void* p){
	if(!p) return ;
	MallocMetadata* temp = ((MallocMetadata*)p)-1;
	temp->is_free=true; 
}

void* srealloc(void* oldp, size_t size){
	if(!oldp) return smalloc(size);
	MallocMetadata* temp = ((MallocMetadata*)oldp)-1;
	if(size > 1e8 ||size ==0) return nullptr ; 
	if(temp->size >= size) return oldp;
	//if we are here means we need to allocate more place . 
	void * ret = smalloc(size);
	if(!ret) return nullptr; 
	std::memcpy(ret,oldp,temp->size);
	temp->is_free=true;
	return ret;
}
size_t _num_free_blocks(){
	size_t ret = 0; 
	MallocMetadata* it = (MallocMetadata*)head_pointer;
	while(it!=nullptr){
		if(it->is_free) ret++;
		it = it -> next;
	}
	return ret ;
}

size_t _num_free_bytes(){
	size_t ret = 0; 
	MallocMetadata* it = (MallocMetadata*)head_pointer;
	while(it!=nullptr){
		if(it->is_free) ret+=it->size;
		it = it -> next;
	}
	return ret ;
}

size_t _num_allocated_blocks(){
	size_t ret = 0; 
	MallocMetadata* it = (MallocMetadata*)head_pointer;
	while(it!=nullptr){
		ret++;
		it = it -> next;
	}
	return ret ;
}

size_t _num_allocated_bytes(){
	size_t ret = 0; 
	MallocMetadata* it = (MallocMetadata*)head_pointer;
	while(it!=nullptr){
		ret+=it->size;
		it = it -> next;
	}
	return ret ;
}

size_t _size_meta_data(){
	return sizeof(MallocMetadata);
}

size_t _num_meta_data_bytes(){
	return _num_allocated_blocks()*_size_meta_data();
}
