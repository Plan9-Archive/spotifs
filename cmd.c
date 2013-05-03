#include "spotifs.h"
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include "xml.h"
#include "tree.h"

static uchar cache[] = {
	0xc2, 0xaa, 0x05, 0xe8, 0x25, 0xa7, 0xb5, 0xe4,
	0xe6, 0x59, 0x0f, 0x3d, 0xd0, 0xbe, 0x0a, 0xef,
	0x20, 0x51, 0x95
};

static char*
fixname(char *s)
{
	int i;

	s = strdup(s);
	for(i = 0; s[i] != 0; i++)
		if(s[i] == '/')
			s[i] = '\\';
	return s;
}

static void
aeskey(Xid trackid, Xfid fileid)
{
	Ch *ch;
	Buf *b;

	ch = chnew(chfile, fileid, nil, nil);
	if(ch == nil){
		perror("aeskey");
		return;
	}
	b = Bufnew();
	Bufwrite(b, fileid, Xfidsz);
	Bufwrite(b, trackid, Xidsz);
	Bufu16(b, 0);
	Bufu16(b, ch->id);
	if(pktwrite(Cgetkey, b) != 0)
		perror("aeskey");
	Buffree(b);
}

static void
stream(Ch *ch, int off, int len, uchar *fid)
{
	Buf *b;

	b = Bufnew();
	Bufu16(b, ch->id);
	Bufu16(b, 0x0800);
	Bufu16(b, 0);
	Bufu16(b, 0);
	Bufu16(b, 0);
	Bufu16(b, 0x4e20);
	Bufu32(b, 200000);
	Bufwrite(b, fid, Xfidsz);
	off -= off%4096;
	len -= len%4096;
	off >>= 2;
	len >>= 2;
	Bufu32(b, off);
	Bufu32(b, off+len);
	if(pktwrite(Cstream, b) != 0)
		perror("stream");
	Buffree(b);
}

typedef struct TrackAux TrackAux;
struct TrackAux {
	Ch	*ch;
	Buf	*b;
	int	offset;
};

static char*
filltrack(Tab *tab, Req *req)
{
	Track *t;
	TrackAux *a;
	Msg *m;
	Ch *ch;
	int count;
	int offset;
	int have;

	if(r->ifcall.type != Tread)
		return nil;
	t = tab->aux2;
	m = mallocz(sizeof(*m), 1);
	m->req = req;
	a = tab->aux3;
	if(a == nil){
		/* need to get the key */
		m->type = MsgKey;
		m->key.id = t->id;
		m->key.fid = t->fid;
		m->key.ret = chancreate(sizeof(Ch*), 0);
		sendp(session->msg, m);
		ch = recvp(m->key.ret);
		if(ch == nil)
			return "request failed";
		a = mallocz(sizeof(*a), 1);
		a->ch = ch;
		a->b = Bufnew();
		t->aux3 = a;
	}

	if(count > 0){
		have = a->b->size - req->ifcall.offset;
		if(have < 1){
			if(a->b->size > 0)
				a->ch->st = Chsteof;
			else{
				m->type = MsgRead;
				m->read.fid = t->fid;
				
			}
		}
		if(a->b->size < 
		if(offset >= a->offset){
			if(offset+count <= a->offset+a->b->size){
				offset -= a->offset;
				readbuf(req, a->b->data, count);
				return nil;
			}
			/* need more data */
		}
	}
	return nil;
}

static void
addtabtracks(Tab *root, Track **tracks, int disc)
{
	int i;
	Tab *tab;
	Track *t;
	char *name;

	for(i = 0; tracks[i] != nil; i++){
		t = tracks[i];
		if(disc > 0)
			name = smprint("%c%02d - %s.ogg", 'a'+disc, t->n, t->name);
		else
			name = smprint("%02d - %s.ogg", t->n, t->name);
		tab = addtab(root,
			fixname(name), Qtrack, 0444, filltrack, t);
		free(name);
		if(tab != nil){
			tab->flags |= Flcallaux;
			tracks[i] = nil;
		}
	}
}

static void
addtabdiscs(Tab *root, Disc **discs)
{
	int i;
	Disc *d;
	int prefix;

	prefix = (discs[0] != nil && discs[1] != nil) ? 1 : 0;
	for(i = 0; discs[i] != nil; i++){
		d = discs[i];
		if(d->tracks != nil)
			addtabtracks(root, d->tracks, prefix+i);
		discs[i] = nil;
	}
}

static void	addtabalbums(Tab *root, Album **albums);

static int
getyear(Album *al)
{
	Track *t;

	t = nil;
	if(al->discs != nil && al->discs[0] != nil)
		if(al->discs[0]->tracks != nil)
			t = al->discs[0]->tracks[0];
	return (t != nil) ? t->year : 0;
}

static char*
fillalbum(Tab *tab, Req *req)
{
	Album *al;
	Buf *b;
	Msg *m;
	int year;
	char *s;

	al = tab->aux2;
	tab->aux = nil;
	tab->flags &= ~Flcallaux;

	m = mallocz(sizeof(*m), 1);
	m->type = MsgBrowse;
	m->req = req;
	m->browse.kind = BrowseAlbum;
	m->browse.id = al->id;
	m->browse.ret = chancreate(sizeof(Album*), 0);

	sendp(session->msg, m);
	b = recvp(m->browse.ret);
	if(b == nil)
		return "request failed";
	xmlfree(Xtalbum, al);
	al = xmlparse(Xtalbum, (char*)b->data);
	if(al != nil && al->discs != nil){
		if((year = getyear(al)) != 0){
			s = smprint("%04d - %s", year, tab->name);
			free(tab->name);
			tab->name = s;
		}
		addtabdiscs(tab, al->discs);
	}
	xmlfree(Xtalbum, al);
	tab->aux2 = nil;
	return nil;
}

static char*
fillartist(Tab *tab, Req *req)
{
	Artist *a;
	Buf *b;
	Msg *m;

	a = tab->aux2;
	tab->aux = nil;
	tab->flags &= ~Flcallaux;

	m = mallocz(sizeof(*m), 1);
	m->type = MsgBrowse;
	m->req = req;
	m->browse.kind = BrowseArtist;
	m->browse.id = a->id;
	m->browse.ret = chancreate(sizeof(Buf*), 0);

	sendp(session->msg, m);
	b = recvp(m->browse.ret);
	if(b == nil)
		return "request failed";
	xmlfree(Xtartist, a);
	a = xmlparse(Xtartist, (char*)b->data);
	if(a != nil && a->albums != nil)
		addtabalbums(tab, a->albums);
	xmlfree(Xtartist, a);
	tab->aux2 = nil;
	return nil;
}

static void
addtabalbums(Tab *root, Album **albums)
{
	int i, year;
	Tab *tab;
	Album *al;
	char *name;

	for(i = 0; albums[i] != nil; i++){
		al = albums[i];
		year = getyear(al);
		if(year != 0)
			name = smprint("%04d - %s", year, al->name);
		else
			name = strdup(al->name);
		if(al->discs == nil){
			tab = addtab(root,
				fixname(name), Qalbum, 0555|DMDIR, fillalbum, al);
			if(tab != nil)
				tab->flags |= Flcallaux;
		}else{
			tab = addtab(root,
				fixname(name), Qalbum, 0555|DMDIR, nil, al);
			if(tab != nil)
				addtabdiscs(tab, al->discs);
		}
		free(name);
		if(tab != nil)
			albums[i] = nil;
	}
}

static void
addtabartists(Tab *root, Artist **artists)
{
	int i;
	Tab *tab;
	Artist *a;

	for(i = 0; artists[i] != nil; i++){
		a = artists[i];
		if(a->albums == nil){
			tab = addtab(root,
				fixname(a->name), Qartist, 0555|DMDIR, fillartist, a);
			if(tab != nil)
				tab->flags |= Flcallaux;
		}else{
			tab = addtab(root,
				fixname(a->name), Qartist, 0555|DMDIR, nil, a);
			if(tab != nil)
				addtabalbums(tab, a->albums);
		}
		if(tab != nil)
			artists[i] = nil;
	}
}

static void
searchresults(Ch *ch, Buf *p)
{
	Search *r;
	Req *req;

	r = xmlparse(Xtsearch, (char*)p->data);
	if(r == nil)
		sysfatal("%r");

	if(r->artists != nil)
		addtabartists(&artists, r->artists);
	if(r->albums != nil)
		addtabalbums(&albums, r->albums);
	if(r->tracks != nil)
		addtabtracks(&tracks, r->tracks, 0);

	xmlfree(Xtsearch, r);
	Buffree(p);
	req = ch->data2;
	respond(req, nil);
}

static void
browseresults(Ch *ch, Buf *p)
{
	Channel *ret;
	ret = ch->data2;
	sendp(ret, p);
}

static void
browse(int type, Xid hash, void *ret)
{
	Ch *ch;
	Buf *b;

	ch = chnew(chinflate, nil, browseresults, ret);
	if(ch == nil){
		perror("browse");
		return;
	}
	b = Bufnew();;
	Bufu16(b, ch->id);
	Bufu8(b, type);
	Bufwrite(b, hash, Xidsz);
	Bufu32(b, 0);
	if(pktwrite(Cbrowse, b) != 0)
		perror("browse");
	Buffree(b);
}

static void
image(Xfid hash)
{
	Ch *ch;
	Buf *b;

	ch = chnew(chrawread, nil, nil, nil);
	if(ch == nil){
		perror("image");
		return;
	}
	b = Bufnew();
	Bufu16(b, ch->id);
	Bufwrite(b, hash, Xfidsz);
	if(pktwrite(Cimg, b) != 0)
		perror("image");
	Buffree(b);
}

static void
search(char *string, int offset, int limit, Req *r)
{
	int len;
	Ch *ch;
	Buf *b;

	ch = chnew(chinflate, nil, searchresults, r);
	if(ch == nil){
		perror("search");
		return;
	}
	len = strlen(string);
	b = Bufnew();
	Bufu16(b, ch->id);
	Bufu32(b, offset);
	Bufu32(b, limit);
	Bufu16(b, 0);
	Bufu8(b, len);
	Bufwrite(b, (uchar*)string, len);
	if(pktwrite(Csearch, b) != 0)
		perror("search");
	Buffree(b);
}

static void
chfile(Ch *ch, Buf *)
{
	void *f;

	switch(ch->state){
	case Chstinit:
		f = malloc(Xfidsz);
		memcpy(f, ch->data, Xfidsz);
		ch->data = f;
		break;

	case Chstdata:
		break;

	case Chsteof:
		free(ch->data);
		break;
	}
}

static void
aeskey(Xid trackid, Xfid fileid)
{
	Ch *ch;
	Buf *b;

	ch = chnew(chfile, fileid, nil, nil);
	if(ch == nil){
		perror("aeskey");
		return;
	}
	b = Bufnew();
	Bufwrite(b, fileid, Xfidsz);
	Bufwrite(b, trackid, Xidsz);
	Bufu16(b, 0);
	Bufu16(b, ch->id);
	if(pktwrite(Cgetkey, b) != 0)
		perror("aeskey");
	Buffree(b);
}

void
sessproc(void*)
{
	Msg *m;
	Buf *b;
	int id;
	Ch *ch;

	for(;;){
		m = recvp(session->msg);
		if(m == nil)
			break;
		if(m->type == MsgIn){
			b = m->in.b;
			switch(m->in.cmd){
			case Cchdata:
				chfeed(b, 0);
				break;
	
			case Ckey:
				id = b->data[2]>>8 | b->data[3];
				ch = chget(id);
				if(ch != nil){
					/* FIXME -- start streaming */
					/* stream(ch, 0, 65536*8); */
					chaes(ch, &b->data[4], b->size-4);
				}
				break;

			case Ckeyerr:
				/* FIXME */
				break;
	
			case Ccherr:
				chfeed(b, 1);
				break;
	
			case Cping:
				b->size = 0;
				Bufu32(b, time(nil));
				pktwrite(Cpong, b);
				break;
	
			case Csecret:
				if(b->size != 336)
					sysfatal("invalid Csecret length: %d", b->size);
				if(memcmp(session->rsan, &b->data[16], 128) != 0)
					sysfatal("invalid rsa pub key");
				b->size = 0;
				Bufu8(b, rand());
				Bufwrite(b, cache, sizeof(cache));
				pktwrite(Ccache, b);
				break;
			}
			free(b);
		}else
		if(m->type == MsgSearch){
			search(
				m->search.string,
				m->search.offset,
				m->search.limit,
				m->req);
			free(m->search.string);
		}else
		if(m->type == MsgBrowse){
			browse(m->browse.kind, m->browse.id, m->browse.ret);
		}

		free(m);
	}
}
