#include <unistd.h>
#include <assert.h>
void* smalloc(size_t size){
	if(size > 1e8 ||size ==0) {
		return nullptr;
	}
	void* ret =sbrk(size);
	return (ret==(void*)(-1)) ? nullptr : ret ;
}
