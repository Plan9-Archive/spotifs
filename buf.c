#include "spotifs.h"
#include <flate.h>

enum {
	Bufsz = 4096
};

static int	Bufinflget(void *src);
static int	Bufinflwr(void *dst, void *src, int size);

Buf*
Bufnew(void)
{
	Buf *b;

	b = malloc(sizeof(*b));
	if(b == nil)
		abort();
	b->allocated = Bufsz;
	b->data = malloc(b->allocated);
	if(b->data == nil)
		abort();
	b->size = 0;
	return b;
}

void
Buffree(Buf *b)
{
	if(b == nil)
		return;
	if(b->data != nil)
		free(b->data);
	free(b);
}

void
Bufwrite(Buf *b, uchar *data, int size)
{
	Bufgrow(b, size);
	memcpy(&b->data[b->size], data, size);
	b->size += size;
}

int
Bufread(Buf *b, int fd, int size)
{
	int r;

	Bufgrow(b, size);
	if((r = read(fd, &b->data[b->size], size)) > 0)
		b->size += r;
	return r;
}

void
Bufu8(Buf *b, uchar u)
{
	Bufwrite(b, &u, 1);
}

void
Bufu16(Buf *b, ushort u)
{
	uchar data[2];

	data[0] = u>>8;
	data[1] = u;
	Bufwrite(b, data, 2);
}

void
Bufu32(Buf *b, uint u)
{
	uchar data[4];

	data[0] = u>>24;
	data[1] = u>>16;
	data[2] = u>>8;
	data[3] = u;
	Bufwrite(b, data, 4);
}

int
Bufinflate(Buf *dst, Buf *src)
{
	int r;

	/* skip the header */
	src->i = 10;

	r = inflate(dst, Bufinflwr, src, Bufinflget);
	if(r != 0)
		werrstr("inflatezlib failed (%s)", flateerr(r));

	return r;
}

void
Bufgrow(Buf *b, int size)
{
	if(b->allocated - b->size < size){
		b->allocated += size+Bufsz - size%Bufsz;
		b->data = realloc(b->data, b->allocated);
		if(b->data == nil)
			abort();
	}
}

static int
Bufinflget(void *src)
{
	Buf *b;

	b = src;
	if(b->i >= b->size)
		return -1;
	return b->data[b->i++];
}

static int
Bufinflwr(void *dst, void *src, int size)
{
	Bufwrite(dst, src, size);
	return size;
}
