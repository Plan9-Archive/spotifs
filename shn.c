#include "spotifs.h"

/*
 * Shannon cipher & MAC.
 */

enum {
	Shnconst=	0x6996c53a,
};

#define rotl(x, v) (((x)<<(v)) | ((x)>>(32-(v))))

struct ShnState {
	uint	r[Shnsz];	/* shift register */
	uint	s[Shnsz];	/* saved */
	uint	crc[Shnsz];	/* crc accu */
	uint	*p;			/* insertion point */
	uint	c;			/* const */
	uint	e;			/* encrypted */
	uint	m;			/* part-word mac */
	int		n;			/* num of bits in m */
};

static void	cycle(ShnState *shn);
static void	keyload(ShnState *shn, uchar *k, int ksize);
static void	mac(ShnState *shn, uint x);

ShnState*
shnnew(uchar *k, int ksize)
{
	ShnState *shn;
	int i;

	shn = mallocz(sizeof(*shn), 1);
	if(shn == nil)
		return nil;
	shn->p = &shn->r[13];

	shn->r[0] = shn->r[1] = 1;
	for(i = 2; i < Shnsz; i++)
		shn->r[i] = shn->r[i-2] + shn->r[i-1];
	shn->c = Shnconst;

	keyload(shn, k, ksize);
	shn->c = shn->r[0];
	memcpy(shn->s, shn->r, sizeof(shn->r));

	return shn;
}

void
shnnonce(ShnState *shn, uint nonce)
{
	uchar n[4];

	n[0] = nonce>>24;
	n[1] = nonce>>16;
	n[2] = nonce>>8;
	n[3] = nonce;
	memcpy(shn->r, shn->s, sizeof(shn->s));
	shn->c = Shnconst;
	keyload(shn, n, 4);
	shn->c = shn->r[0];
	shn->n = 0;
}

void
shnenc(ShnState *shn, uchar *p, int size)
{
	uchar *end;
	uint x;

	if(shn->n > 0){
		for(; shn->n > 0 && size > 0; shn->n -= 8, size--, p++){
			shn->m ^= *p << (32 - shn->n);
			*p ^= shn->e >> (32 - shn->n);
		}
		if(shn->n > 0)
			return;
		mac(shn, shn->m);
	}

	for(end = &p[size & ~3]; p < end; p += 4){
		cycle(shn);
		x = p[0] | p[1]<<8 | p[2]<<16 | p[3]<<24;
		mac(shn, x);
		x ^= shn->e;
		p[0] = x;
		p[1] = x>>8;
		p[2] = x>>16;
		p[3] = x>>24;
	}

	size &= 3;
	if(size > 0){
		cycle(shn);
		shn->m = 0;
		for(shn->n = 32; shn->n > 0 && size > 0; shn->n -= 8, size--, p++){
			shn->m ^= *p << (32 - shn->n);
			*p ^= shn->e >> (32 - shn->n);
		}
	}
}

void
shndec(ShnState *shn, uchar *p, int size)
{
	uchar *end;
	uint x;

	if(shn->n > 0){
		for(; shn->n > 0 && size > 0; shn->n -= 8, size--, p++){
			*p ^= shn->e >> (32 - shn->n);
			shn->m ^= *p << (32 - shn->n);
		}
		if(shn->n > 0)
			return;
		mac(shn, shn->m);
	}

	for(end = &p[size & ~3]; p < end; p += 4){
		cycle(shn);
		x = p[0] | p[1]<<8 | p[2]<<16 | p[3]<<24;
		x ^= shn->e;
		mac(shn, x);
		p[0] = x;
		p[1] = x>>8;
		p[2] = x>>16;
		p[3] = x>>24;
	}

	size &= 3;
	if(size > 0){
		cycle(shn);
		shn->m = 0;
		for(shn->n = 32; shn->n > 0 && size > 0; shn->n -= 8, size--, p++){
			*p ^= shn->e >> (32 - shn->n);
			shn->m ^= *p << (32 - shn->n);
		}
	}
}

void
shnend(ShnState *shn, uchar *p, int size)
{
	int i;

	if(shn->n > 0)
		mac(shn, shn->m);

	cycle(shn);
	*shn->p ^= Shnconst ^ (shn->n<<3);
	shn->n = 0;

	for(i = 0; i < Shnsz; i++)
		shn->r[i] ^= shn->crc[i];
	for(i = 0; i < Shnsz; i++)
		cycle(shn);

	for(; size > 0;){
		cycle(shn);
		if(size >= 4){
			p[0] = shn->e;
			p[1] = shn->e>>8;
			p[2] = shn->e>>16;
			p[3] = shn->e>>24;
			size -= 4;
			p += 4;
		} else {
			switch(size){
			case 3:
				p[2] = shn->e>>16;
			case 2:
				p[1] = shn->e>>8;
			case 1:
				p[0] = shn->e;
			}
			break;
		}
	}
}

static void
cycle(ShnState *shn)
{
	uint x;

	x = shn->r[12] ^ shn->r[13] ^ shn->c;
	x ^= rotl(x, 5) | rotl(x, 7);
	x ^= rotl(x, 19) | rotl(x, 22);
	x ^= rotl(shn->r[0], 1);
	memcpy(&shn->r[0], &shn->r[1], (Shnsz-1)*4);
	shn->r[Shnsz-1] = x;
	x = shn->r[2] ^ shn->r[15];
	x ^= rotl(x, 7) | rotl(x, 22);
	x ^= rotl(x, 5) | rotl(x, 19);
	shn->r[0] ^= x;
	shn->e = x ^ shn->r[8] ^ shn->r[12];
}

static void
keyload(ShnState *shn, uchar *k, int ksize)
{
	int i;
	uchar *p;
	int psize;

	p = k;
	psize = ksize;
	if(ksize&3 != 0){
		psize = (ksize+3) & ~3;
		if((p = mallocz(psize, 1)) == nil)
			sysfatal("no memory for the key");
		memcpy(p, k, ksize);
	}

	for(i = 0; i < psize; i += 4){
		*shn->p ^= p[i+0] | p[i+1]<<8 | p[i+2]<<16 | p[i+3]<<24;
		cycle(shn);
	}

	*shn->p ^= ksize;
	cycle(shn);

	memcpy(shn->crc, shn->r, sizeof(shn->r));
	for(i = 0; i < Shnsz; i++)
		cycle(shn);
	for(i = 0; i < Shnsz; i++)
		shn->r[i] ^= shn->crc[i];

	if(p != k)
		free(p);
}

static void
mac(ShnState *shn, uint x)
{
	uint t;

	t = shn->crc[0] ^ shn->crc[2] ^ shn->crc[15] ^ x;
	memcpy(&shn->crc[0], &shn->crc[1], (Shnsz-1)*4);
	shn->crc[Shnsz-1] = t;
	*shn->p ^= x;
}
