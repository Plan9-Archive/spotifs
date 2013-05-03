#include "spotifs.h"
#include <auth.h>
#include <fcall.h>
#include <flate.h>
#include <thread.h>
#include <9p.h>
#include "xml.h"
#include "tree.h"

#define AUTHFMT "proto=pass service=spotify server=ap.spotify.com"

int debug;
Session *session;

extern Tab search;
static char *owner;

Tab albums = {"albums", QTDIR, Qsalbums, 0755|DMDIR, &search};
Tab artists = {"artists", QTDIR, Qsartists, 0755|DMDIR, &search};
Tab tracks = {"tracks", QTDIR, Qstracks, 0755|DMDIR, &search};
static Tab *stab[] = {
	&albums,
	&artists,
	&tracks,
	nil
};

static Tab ctl = {"ctl", 0, Qctl, 0644, &root};
Tab search = {"search", QTDIR, Qsearch, 0755|DMDIR, &root, 0, stab};
static Tab *rtab[] = {
	&ctl,
	&search,
	nil
};

Tab root = {"/", QTDIR, Qroot, 0755|DMDIR, nil, 0, rtab};

Qid
mkqid(int type, uvlong path)
{
	Qid q;

	q.type = type;
	q.path = path;
	q.vers = 0;
	return q;
}

static void
fillstat(Dir *dir, char *name, int type, uvlong path, ulong perm)
{
	dir->name = estrdup9p(name);
	dir->uid = estrdup9p(owner);
	dir->gid = estrdup9p(owner);
	dir->mode = perm;
	dir->length = 0;
	dir->qid = mkqid(type, path);
	dir->atime = time(0);
	dir->mtime = time(0);
	dir->muid = estrdup9p("");
}

static void
fsattach(Req *r)
{
	r->fid->qid = mkqid(root.type, root.path);
	r->fid->aux = &root;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static int
dirgen(int n, Dir *dir, void *tab)
{
	Tab *t;

	t = tab;
	if(t->child == nil)
		return -1;
	t = t->child[n];
	if(t == nil)
		return -1;
	fillstat(dir, t->name, t->type, t->path, t->perm);
	return 0;
}

static void
fsread(Req *r)
{
	Tab *t;
	char *s;

	t = r->fid->aux;
	if(t->flags & Flcallaux){
		s = ((callaux)t->aux)(t, r);
		if(s != nil){
			respond(r, s);
			return;
		}
	}

	if(t->type == QTDIR){
		dirread9p(r, dirgen, t);
		respond(r, nil);
		return;
	}

	respond(r, nil);
}

static void
fsopen(Req *r)
{
	ulong perm, n;
	static int need[4] = {4, 2, 6, 1};
	Tab *t;

	t = r->fid->aux;
	if(t == nil){
		respond(r, "bug in fsopen");
		return;
	}

	perm = t->perm;
	if(strcmp(r->fid->uid, owner) == 0)
		perm >>= 6;
	n = need[r->ifcall.mode & 3];
	if((perm&n) != n){
		respond(r, "permission denied");
		return;
	}
	respond(r, nil);
}

static void
fswrite(Req *r)
{
	char *c[2];
	char *str;
	int nc;
	Msg *m;

	if(r->fid->qid.path == Qctl){
		str = malloc(r->ifcall.count + 1);
		memcpy(str, r->ifcall.data, r->ifcall.count);
		str[r->ifcall.count] = 0;
		nc = getfields(str, c, 2, 1, " \t");
		m = mallocz(sizeof(*m), 1);
		if(nc == 2 && strcmp(c[0], "search") == 0){
			m->type = MsgSearch;
			m->search.string = strdup(c[1]);
			m->search.offset = 0;
			m->search.limit = -1;
		}else{
			free(m);
			m = nil;
		}
		if(m == nil)
			respond(r, "wat?");
		else{
			m->req = r;
			sendp(session->msg, m);
		}
		free(str);
		return;
	}

	respond(r, "no");
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	Tab *t, *x;
	int i;

	t = fid->aux;
	if(strcmp(name, "..") == 0){
		x = t->parent;
	}else
	if(t->type == QTDIR){
		if(t->child == nil)
			return nil;
		x = nil;
		for(i = 0; t->child[i] != nil; i++){
			if(strcmp(t->child[i]->name, name) == 0){
				x = t->child[i];
				break;
			}
		}
	}else{
		x = t;
	}
	if(x == nil)
		return nil;

	fid->aux = x;
	fid->qid = mkqid(x->type, x->path);
	if(qid != nil)
		*qid = fid->qid;
	return nil;
}

static char*
fsclone(Fid *fid, Fid *newfid)
{
	newfid->aux = fid->aux;
	return nil;
}

static void
fsdestroy(Fid *fid)
{
	fid->aux = nil;
}

static void
fsstat(Req *r)
{
	Tab *t;

	t = r->fid->aux;
	fillstat(&r->d, t->name, t->type, t->path, t->perm);
	respond(r, nil);
}

static Srv fs = {
	.attach=	fsattach,
	.open=		fsopen,
	.read=		fsread,
	.write=		fswrite,
	.walk1=		fswalk1,
	.stat=		fsstat,
	.clone=		fsclone,
};

static void
usage(void)
{
	fprint(2, "usage: spotifs [-u user] [-s srvname] [-m mtpt]\n");
	exits("usage");
}

static void
netproc(void*)
{
	int cmd;
	Msg *m;
	Buf *b;

	for(;;){
		b = Bufnew();
		if(pktread(&cmd, b) != 0)
			sysfatal("netproc: %r");
		m = malloc(sizeof(*m));
		m->type = MsgIn;
		m->in.cmd = cmd;
		m->in.b = b;
		if(sendp(session->msg, m) < 0)
			break;
	}
}

Tab*
addtab(Tab *root, char *name, uvlong path, ulong perm, void *aux, void *aux2)
{
	Tab *t;
	int i;

	t = mallocz(sizeof(*t), 1);
	if(perm & DMDIR)
		t->type = QTDIR;
	t->name = strdup(name);
	t->path = path;
	t->perm = perm;
	t->parent = root;
	t->aux = aux;
	t->aux2 = aux2;
	for(i = 0; root->child != nil && root->child[i] != nil; i++){
		if(cistrcmp(root->child[i]->name, name) == 0){
			free(t);
			return nil;
		}
	}
	root->child = realloc(root->child, sizeof(Tab*)*(i+2));
	root->child[i] = t;
	root->child[i+1] = nil;
	return t;
}

void
threadmain(int argc, char **argv)
{
	UserPasswd *up;
	char *user, *mtpt, *srvname;
	int r;

	user = nil;
	mtpt = nil;
	srvname = "spotifs";
	owner = getuser();

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	case 'd':
		debug++;
		break;
	case 'u':
		user = EARGF(usage());
		break;
	case 's':
		srvname = EARGF(usage());
		break;
	case 'm':
		mtpt = EARGF(usage());
		break;
	}ARGEND

	if(argc > 1)
		usage();
	srand(time(nil));
	if(inflateinit() != 0)
		sysfatal("fail");

	if(user == nil)
		up = auth_getuserpasswd(auth_getkey, AUTHFMT);
	else
		up = auth_getuserpasswd(auth_getkey, AUTHFMT " user=%q", user);
	if(up == nil)
		sysfatal("no password: %r");

	session = malloc(sizeof(*session));
	r = connect(session, up->user, up->passwd);
	memset(up->passwd, 0, strlen(up->passwd));
	free(up);
	if(r != 0)
		sysfatal("connect failed: %r");

	session->msg = chancreate(sizeof(Msg*), 8);

	procrfork((void*)sessproc, session, 16384, RFNAMEG|RFNOTEG);
	procrfork((void*)netproc, session, 65536, RFNAMEG|RFNOTEG);
	threadpostmountsrv(&fs, srvname, mtpt, MBEFORE);
}
