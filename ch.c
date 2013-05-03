#include "spotifs.h"

static void	chf(Ch *ch, uchar *p, int size);

static uchar aesiv[] = {
	0x72, 0xe0, 0x67, 0xfb, 0xdd, 0xcb, 0xcf, 0x77,
	0xeb, 0xe8, 0xbc, 0x64, 0x3f, 0x63, 0x0d, 0x93,
};

Ch*
chnew(Chfunc f, void *data, Chfunc f2, void *data2)
{
	Ch *ch;
	int i;

	for(i = 0; i < Numch; i++)
		if(session->ch[i].f == nil)
			break;

	if(i >= Numch){
		werrstr("chan overflow");
		return nil;
	}

	ch = &session->ch[i];
	ch->id = i;
	ch->f = f;
	ch->f2 = f2;
	ch->data = data;
	ch->data2 = data2;

	ch->state = Chstinit;
	chf(ch, nil, 0);
	ch->state = Chstheader;

	return ch;
}

Ch*
chget(int id)
{
	if(id >= Numch){
		werrstr("chan %d not set", id);
		return nil;
	}

	return &session->ch[id];
}

int
chfeed(Buf *b, int err)
{
	uchar *p;
	int size, done, secsz;
	Ch *ch;
	int i;

	secsz = 0;
	if(b->size < 2){
		werrstr("short chan data");
		return -1;
	}
	i = b->data[0]<<8 | b->data[1];
	ch = &session->ch[i];
	if(i >= Numch || ch->f == nil){
		werrstr("chan %d not set", i);
		return -1;
	}
	if(err != 0)
		ch->state = Chsterr;

	p = &b->data[2];
	size = b->size - 2;

	if(ch->state == Chstheader){
		for(done = 0; done < size;) {
			if(done+2 > size){
				werrstr("bad chan header");
				return -1;
			}
			secsz = p[0]<<8 | p[1];
			done += 2;
			p += 2;
			if(secsz == 0)
				break;
			if(done+secsz > size){
				werrstr("bad chan header len");
				return -1;
			}

			chf(ch, p, secsz);
			done += secsz;
			p += secsz;
		}
		if(secsz == 0)
			ch->state = Chstdata;
		return 0;
	}

	if(size == 0)
		ch->state = Chsteof;

	chf(ch, p, size);

	if(ch->state == Chsteof || ch->state == Chsterr){
		ch->state = -1;
		ch->f = nil;
		ch->data = nil;
		if(ch->aes != nil){
			free(ch->aes);
			ch->aes = nil;
		}
	}

	return 0;
}

void
chaesread(Ch *ch, Buf *p)
{
	int b, i, numb, bi, j;
	uchar *plain, *c, *w, *x, *y, *z;
	Buf *buf;

	switch(ch->state){
	case Chstinit:
		ch->data = Bufnew();
		break;

	case Chstdata:
		buf = ch->data;
		Bufgrow(buf, p->size + 1024);
		buf->size = p->size;
		plain = buf->data;
		numb = p->size / 1024;
		for(b = 0; b < numb; b++){
			bi = b*1024;
			c = &plain[bi];
			w = &p->data[bi + 0*256];
			x = &p->data[bi + 1*256];
			y = &p->data[bi + 2*256];
			z = &p->data[bi + 3*256];
			for(i = 0; i < 1024 && ((bi+i) < p->size); i+=4){
				*c++ = *w++;
				*c++ = *x++;
				*c++ = *y++;
				*c++ = *z++;
			}
			for(i = 0; i < 1024 && ((bi+i) < p->size); i+=16){
				aes_encrypt(
					ch->aes->ekey,
					10,
					ch->aes->ivec,
					ch->aesstream);
				for(j = 15; j >= 0; j--){
					ch->aes->ivec[j] += 1;
					if(ch->aes->ivec[j] != 0)
						break;
				}
				for(j = 0; j < 16; j++){
					plain[bi+i+j] ^= ch->aesstream[j];
				}
			}
		}
		ch->f2(ch, buf);
		break;

	case Chsteof:
	case Chsterr:
		Buffree(ch->data);
		break;
	}
}

void
chaes(Ch *ch, uchar *key, int size)
{
	ch->aes = malloc(sizeof(AESstate));
	setupAESstate(ch->aes, key, size, aesiv);
	memset(ch->aesstream, 0, sizeof(ch->aesstream));
	ch->f = chaesread;
	ch->state = Chstinit;
	chf(ch, nil, 0);
	ch->state = Chstheader;
}

void
chinflate(Ch *ch, Buf *p)
{
	Buf *dst;

	switch(ch->state){
	case Chstinit:
		ch->data = Bufnew();
		break;

	case Chstdata:
		Bufwrite(ch->data, p->data, p->size);
		break;

	case Chsteof:
		if(ch->data != nil){
			dst = Bufnew();
			if(Bufinflate(dst, ch->data) != 0){
				perror("Bufinflate");
				Buffree(dst);
			}
			else{
				Buffree(ch->data);
				ch->data = ch->data2;
				ch->f2(ch, dst);
			}
		}
		break;

	case Chsterr:
		Buffree(ch->data);
		break;
	}
}

void
chrawread(Ch *ch, Buf *p)
{
	Buf *src;

	switch(ch->state){
	case Chstinit:
		ch->data = Bufnew();
		break;

	case Chstdata:
		Bufwrite(ch->data, p->data, p->size);
		break;

	case Chsteof:
		src = ch->data;
		ch->data = ch->data2;
		ch->f2(ch, src);
		break;

	case Chsterr:
		Buffree(ch->data);
		break;
	}
}

static void
chf(Ch *ch, uchar *p, int size)
{
	Buf data;

	data.data = p;
	data.size = size;
	ch->f(ch, &data);
}
