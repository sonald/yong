#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

struct y_mmap{
	void *addr;
	int size;
};


void *y_mmap_new(const char *fn)
{
	struct y_mmap *p;
	p=calloc(1,sizeof(struct y_mmap));
	{
		int fd;
		struct stat st;
		fd=open(fn,O_RDONLY);
		if(fd==-1)
		{
			free(p);
			return NULL;
		}
		fstat(fd,&st);
		p->size=st.st_size;
		if(st.st_size<=0)
		{
			close(fd);
			free(p);
			return NULL;
		}
		p->addr=mmap(NULL,p->size,PROT_READ,MAP_PRIVATE,fd,0);
		close(fd);
		if(!p->addr)
		{
			free(p);
			return NULL;
		}
	}
	return p;
}

void y_mmap_free(void *map)
{
	struct y_mmap *p=map;
	if(!p) return;
	munmap(p->addr,p->size);
	free(p);
}


int y_mmap_length(void *map)
{
	struct y_mmap *p=map;
	return p->size;
}

void *y_mmap_addr(void *map)
{
	struct y_mmap *p=map;
	return p->addr;
}

