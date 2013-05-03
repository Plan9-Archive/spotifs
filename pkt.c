#include "spotifs.h"

enum {
	Pkthdsz=	3,
};

int
pktread(int *cmd, Buf *b)
{
	uchar p[Pkthdsz];
	int size, r;

	if((r = read(session->conn, p, Pkthdsz)) != Pkthdsz){
		werrstr("packet header short read (%d bytes)", r);
		return -1;
	}

	shnnonce(session->shn[Rcv], session->shniv[Rcv]);
	shndec(session->shn[Rcv], p, Pkthdsz);

	*cmd = p[0];
	size = p[1]<<8 | p[2];
	size += 4;

	b->size = 0;
	for(; size > 0;){
		if((r = Bufread(b, session->conn, size)) < 0){
			werrstr("packet short read (%d < %d): %r", r, size);
			return -1;
		}
		size -= r;
	}
	shndec(session->shn[Rcv], b->data, b->size);
	session->shniv[Rcv]++;
	b->size -= 4;

	if(debug > 0)
		fprint(2, "> cmd: 0x%02x (0x%02x 0x%02x)\n", *cmd, p[1], p[2]);

	return 0;
}

int
pktwrite(int cmd, Buf *b)
{
	uchar p[Pkthdsz];
	int size;

	size = b->size;
	shnnonce(session->shn[Snd], session->shniv[Snd]);

	p[0] = cmd;
	p[1] = size>>8;
	p[2] = size;

	if(debug > 0)
		fprint(2, "< cmd: 0x%02x (0x%02x 0x%02x)\n", cmd, p[1], p[2]);

	Bufu32(b, 0);
	shnenc(session->shn[Snd], p, Pkthdsz);
	shnenc(session->shn[Snd], b->data, size);
	shnend(session->shn[Snd], &b->data[size], 4);
	session->shniv[Snd]++;

	if(write(session->conn, p, Pkthdsz) != Pkthdsz){
		werrstr("packet header short write: %r");
		return -1;
	}
	if(write(session->conn, b->data, b->size) != b->size){
		werrstr("packet short write: %r");
		return -1;
	}

	return 0;
}
