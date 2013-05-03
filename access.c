#include "spotifs.h"
#include <bio.h>
#include <ndb.h>

/*
 * Security by nonsense?
 */

enum {
	Ntries=		16,

	Rndsz=		16,
	Solsz=		8,
	Pubksz=		96,
	Blobsz=		256,
	Saltsz=		10,
	Puzzlensz=	8,

	Rndind=		0,
	Pubkind=	Rndind+Rndsz,
	Blobind=	Pubkind+Pubksz,
	Saltind=	Blobind+Blobsz,
	Padind=		Saltind+Saltsz,
	Usrlenind=	Padind+1,
	Puzzlenind=	Usrlenind+1,
};

/* 768-bit prime based on digits of π */
static uchar prime[] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xc9, 0x0f, 0xda, 0xa2, 0x21, 0x68, 0xc2, 0x34,
	0xc4, 0xc6, 0x62, 0x8b, 0x80, 0xdc, 0x1c, 0xd1,
	0x29, 0x02, 0x4e, 0x08, 0x8a, 0x67, 0xcc, 0x74,
	0x02, 0x0b, 0xbe, 0xa6, 0x3b, 0x13, 0x9b, 0x22,
	0x51, 0x4a, 0x08, 0x79, 0x8e, 0x34, 0x04, 0xdd,
	0xef, 0x95, 0x19, 0xb3, 0xcd, 0x3a, 0x43, 0x1b,
	0x30, 0x2b, 0x0a, 0x6d, 0xf2, 0x5f, 0x14, 0x37,
	0x4f, 0xe1, 0x35, 0x6d, 0x6d, 0x51, 0xc2, 0x45,
	0xe4, 0x85, 0xb5, 0x76, 0x62, 0x5e, 0x7e, 0xc6,
	0xf4, 0x4c, 0x42, 0xe9, 0xa6, 0x3a, 0x36, 0x20,
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static uchar gen[] = {
	0x02
};

static uchar start[] = {
	0x00, 0x03,				/* protocol version */
	0x00, 0x00,
	0x00, 0x00, 0x03, 0x00,	/* OS */
	0x00, 0x03, 0x0c, 0x00,
	0x00, 0x01, 0x86, 0x9f,	/* client revision */
	0x00, 0x00, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x00,
	0x01, 0x04, 0x01, 0x01,	/* client id */
	0x00, 0x00, 0x00, 0x00,
};

static uchar solhdr[] = {
	0x00, 0x00,
	0x00, 0x08,				/* solution length */
	0x00, 0x00, 0x00, 0x00
};

static char *spotifysrv = "_spotify-client._tcp.spotify.com";

static int	genkey(mpint **pub, mpint **priv);
static void	getaddr(char *buf, int size);
static int	login(Session *s, char *user, char *passwd);
static void	solvepuzzle(uchar *rnd, uchar *sol, uint denom, uint magic);

int
connect(Session *s, char *user, char *passwd)
{
	char url[64];
	int i;

	for(i = 0; i < Ntries; i++){
		memset(s, 0, sizeof(*s));
		getaddr(url, sizeof(url));
		if(debug > 0)
			fprint(2, "connecting to %s\n", url);
		s->conn = dial(url, nil, nil, nil);
		if(s->conn >= 0){
			if(login(s, user, passwd) == 0)
				break;
			hangup(s->conn);
		}
	}

	return 0;
}

void
disconnect(Session *s)
{
	if(s->conn > 0)
		hangup(s->conn);
	if(s->shn[Snd] != nil)
		free(s->shn[Snd]);
	if(s->shn[Rcv] != nil)
		free(s->shn[Rcv]);
	memset(s, 0, sizeof(*s));
}

static int
genkey(mpint **pub, mpint **priv)
{
	int ns, res;
	mpint *p, *g;

	*pub = *priv = nil;
	res = -1;
	p = betomp(prime, sizeof(prime), nil);
	g = betomp(gen, sizeof(gen), nil);
	if(p == nil || g == nil)
		goto out;

	ns = mpsignif(p);
	*priv = mprand(ns, genrandom, nil);
	if(*priv == nil)
		goto out;

	*pub = mpnew(0);
	mpexp(g, *priv, p, *pub);
	res = 0;

out:
	mpfree(p);
	mpfree(g);
	return res;
}

static void
getaddr(char *buf, int size)
{
	int i, n;
	Ndbtuple *srv, *s, *target, *port;

	snprint(buf, size, "tcp!ap.spotify.com!4070");
	if((srv = dnsquery(nil, spotifysrv, "srv")) == nil)
		return;

	/* pick up random host */
	s = srv;
	for(i = 0; s != nil; s = s->entry)
		if(strcmp(s->attr, "dom") == 0)
			i++;
	i = nrand(i);
	s = srv;
	for(n = 0; n != i && s != nil; s = s->entry)
		if(strcmp(s->attr, "dom") == 0)
			n++;

	target = ndbfindattr(s, s->line, "target");
	port = ndbfindattr(s, s->line, "port");
	if(target != nil && port != nil)
		snprint(buf, size, "tcp!%s!%s", target->val, port->val);

	ndbfree(srv);
}

static int
login(Session *s, char *user, char *passwd)
{
	RSApriv *rsa;
	DigestState *ds;
	uchar rnd[Rndsz];
	uchar keydata[Pubksz];
	uchar hmacmsg[53];
	uchar hmac[5*SHA1dlen];
	uchar authhmac[SHA1dlen];
	uchar shksh1[SHA1dlen];
	uchar sol[Solsz];
	uchar *p, *m;
	Buf *req, *resp;
	uint denom, magic;
	int n, r, puzzlen, res;
	mpint *pubk, *privk, *shk, *prime768;

	res = -1;
	privk = nil;
	req = nil;
	resp = nil;

	/*
	 * first request, send a bunch of nonsense
	 */
	genrandom(rnd, sizeof(rnd));
	if(genkey(&pubk, &privk) != 0)
		goto error;
	mptobe(pubk, keydata, sizeof(keydata), nil);
	mpfree(pubk);
	if((rsa = rsagen(1024, 17, 0)) == nil)
		goto error;
	mptobe(rsa->pub.n, s->rsan, sizeof(s->rsan), nil);
	rsaprivfree(rsa);

	req = Bufnew();
	Bufwrite(req, start, sizeof(start));
	Bufwrite(req, rnd, sizeof(rnd));
	Bufwrite(req, keydata, sizeof(keydata));
	Bufwrite(req, s->rsan, sizeof(s->rsan));
	Bufu8(req, 0);
	n = strlen(user);
	Bufu8(req, n);
	Bufu16(req, 0x0100);
	Bufwrite(req, (uchar*)user, n);
	Bufu8(req, 0x40);
	req->data[2] = req->size>>8;
	req->data[3] = req->size;
	if(write(s->conn, req->data, req->size) != req->size){
		werrstr("exch request write failed: %r");
		goto error;
	}

	/*
	 * first response, receive a bunch of nonsense
	 */
	resp = Bufnew();
	n = Rndsz+Pubksz+Blobsz+Saltsz+1+1+Puzzlensz;
	r = Bufread(resp, s->conn, n);
	if(r < 2){
		werrstr("exch response status (%d < %d): %r", r, 2);
		goto error;
	}
	if(resp->data[0] != 0){
		werrstr("exch response status=%d error=%d", resp->data[0], resp->data[1]);
		goto error;
	}
	if(r != n){
		werrstr("exch response (%d < %d)", r, n);
		goto error;
	}
	p = &resp->data[Puzzlenind];
	puzzlen = 0;
	puzzlen += p[0]<<8 | p[1];
	puzzlen += p[2]<<8 | p[3];
	puzzlen += p[4]<<8 | p[5];
	puzzlen += p[6]<<8 | p[7];
	n = resp->data[Padind] + resp->data[Usrlenind] + puzzlen;
	r = Bufread(resp, s->conn, n);
	if(r < n){
		werrstr("pad+username+puzzle (%d < %d): %r", r, n);
		goto error;
	}

	p = &resp->data[resp->size-puzzlen];
	if(p[0] != 1 || puzzlen < 6){
		werrstr("invalid exch response puzzle (0x%02x, size=%d)", p[0], puzzlen);
		goto error;
	}
	denom = p[1];
	p = &p[2];
	magic = ((uint)p[0]<<24) | ((uint)p[1]<<16) | ((uint)p[2]<<8) | p[3];
	solvepuzzle(&resp->data[Rndind], sol, denom, magic);

	/*
	 * initialize the keys
	 */
	if((pubk = betomp(&resp->data[Pubkind], Pubksz, nil)) == nil){
		werrstr("pubk: %r");
		goto error;
	}
	prime768 = betomp(prime, sizeof(prime), nil);
	shk = mpnew(0);
	mpexp(pubk, privk, prime768, shk);
	mptobe(shk, keydata, sizeof(keydata), nil);
	mpfree(shk);
	sha1(keydata, sizeof(keydata), shksh1, nil);
	mpfree(prime768);
	mpfree(pubk);

	m = hmacmsg;
	ds = sha1(&resp->data[Saltind], Saltsz, nil, nil);
	ds = sha1((uchar*)" ", 1, nil, ds);
	sha1((uchar*)passwd, strlen(passwd), m, ds);
	m += SHA1dlen;
	memcpy(m, rnd, Rndsz);
	m += Rndsz;
	memcpy(m, &resp->data[Rndind], Rndsz);
	m += Rndsz;

	p = hmac;
	for(n = 0; n < 5; n++){
		*m = n+1;
		hmac_sha1(hmacmsg, sizeof(hmacmsg), shksh1, SHA1dlen, p, nil);
		memcpy(hmacmsg, p, SHA1dlen);
		p += SHA1dlen;
	}

	s->shn[Snd] = shnnew(&hmac[SHA1dlen], 32);
	s->shn[Rcv] = shnnew(&hmac[SHA1dlen+32], 32);

	/*
	 * authentication request
	 */
	ds = hmac_sha1(req->data, req->size, hmac, SHA1dlen, nil, nil);
	ds = hmac_sha1(resp->data, resp->size, hmac, SHA1dlen, nil, ds);
	ds = hmac_sha1(solhdr, sizeof(solhdr), hmac, SHA1dlen, nil, ds);
	hmac_sha1(sol, Solsz, hmac, SHA1dlen, authhmac, ds);

	req->size = 0;
	Bufwrite(req, authhmac, sizeof(authhmac));
	Bufwrite(req, solhdr, sizeof(solhdr));
	Bufwrite(req, sol, Solsz);
	if(write(s->conn, req->data, req->size) != req->size){
		werrstr("auth req: %r");
		goto error;
	}

	/*
	 * authentication response
	 */
	resp->size = 0;
	if(Bufread(resp, s->conn, 2) != 2){
		werrstr("auth response: %r");
		goto error;
	}
	p = resp->data;
	if(p[0] != 0){
		werrstr("auth response: status=%d error=%d", p[0], p[1]);
		goto error;
	}
	n = p[1];
	if(n == 0){
		werrstr("auth response: empty payload");
		goto error;
	}
	if((r = Bufread(resp, s->conn, n)) != n){
		werrstr("auth response short payload (%d < %d): %r", r, n);
		goto error;
	}
	res = 0;

error:
	if(res != 0)
		werrstr("login: %r");
	mpfree(privk);
	Buffree(req);
	Buffree(resp);
	return res;
}

static void
solvepuzzle(uchar *rnd, uchar *sol, uint denom, uint magic)
{
	DigestState *ctx;
	uchar d[SHA1dlen], *p, tmpp;
	uint *nom;

	denom = (1<<denom) - 1;
	p = &d[SHA1dlen-4];
	nom = (void*)p;
	for(;;){
		genrandom(sol, Solsz);
		ctx = sha1(rnd, Rndsz, nil, nil);
		sha1(sol, Solsz, d, ctx);

		/* swap */
		tmpp = p[0];
		p[0] = p[3];
		p[3] = tmpp;
		tmpp = p[1];
		p[1] = p[2];
		p[2] = tmpp;

		*nom ^= magic;
		if((*nom&denom) == 0)
			break;
	}
}
