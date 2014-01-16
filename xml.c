#include "spotifs.h"
#include "xml.h"

/*
 * Yet another [retarded XML] parser.
 */

enum {
	Tcmplx,
	Tlist,

	/*
	 * The next type deserves a separate comment.
	 * One expects to get <tracks><track></track><track></track></tracks>
	 * structure in XML. This is what you get on "search" query.
	 * One gets <discs><disc></disc>...</discs>, which is correct as well.
	 * But then THIS:
	 * <discs>
	 *   <disc>
	 *     <disc-number></disc-number>
	 *     <name></name>
	 *     <track></track>
	 *     <track></track>
	 *     <track></track>
	 *     ....
	 */
	Tlist2,

	Tstr,
	Tfloat,
	Tint,
	Tbool,
	Tfid, /* 20 bytes */
	Tid,  /* 16 bytes */

	Stflinl= 1,
	Stflerr= 2,
};

typedef struct Xml Xml;
struct Xml {
	char	*name;
	int		type;
	Xml		*chain;
	ulong	offset;
	int		size;
};

typedef struct {
	int  flags;
	char *t;     /* current tag */
	int  indent; /* to debug stuff */
}State;

#define end {nil, 0, nil}

static char *escmap[] = {
	"\x06\"&quot;",
	"\x06\'&apos;",
	"\x04<&lt;",
	"\x04>&gt;",
	"\x05&&amp;",
};

static Xml track;
static Xml artist;
static Xml album;

static Xml img[] = {
	{"id",     Tfid, nil, offsetof(Img, id[0]),    0},
	{"width",  Tint, nil, offsetof(Img, width),    0},
	{"height", Tint, nil, offsetof(Img, height),   0},
	{"small",  Tfid, nil, offsetof(Img, small[0]), 0},
	{"large",  Tfid, nil, offsetof(Img, large[0]), 0},
	end
};

static Xml biofields[] = {
	{"text", Tstr, nil, offsetof(Bio, text), 0},
	end
};

static Xml bio = {"bio", Tcmplx, biofields, 0, sizeof(Bio)};

static Xml copy[] = {
	{"c", Tstr, nil, offsetof(Copy, c), 0},
	{"p", Tstr, nil, offsetof(Copy, p), 0},
	end
};

static Xml artistfields[] = {
	{"popularity",      Tfloat, nil,     offsetof(Artist, pop),     0},
	{"name",            Tstr,   nil,     offsetof(Artist, name),    0},
	{"portrait",        Tcmplx, img,     offsetof(Artist, img),     sizeof(Img)},
	{"bios",            Tlist,  &bio,    offsetof(Artist, bios),    0},
	{"genres",          Tstr,   nil,     offsetof(Artist, genres),  0},
	{"years-active",    Tstr,   nil,     offsetof(Artist, years),   0},
	{"tophits",         Tlist,  &track,  offsetof(Artist, top),     0},
	{"similar-artists", Tlist,  &artist, offsetof(Artist, similar), 0},
	{"albums",          Tlist,  &album,  offsetof(Artist, albums),  0},
	{"id",              Tid,    nil,     offsetof(Artist, id[0]),   0},
	end
};

static Xml artist = {"artist", Tcmplx, artistfields, 0, sizeof(Artist)};

static Xml discfields[] = {
	{"disc-number", Tint,   nil,    offsetof(Disc, n),      0},
	{"name",        Tstr,   nil,    offsetof(Disc, name),   0},
	{"track",       Tlist2, &track, offsetof(Disc, tracks), 0},
	end
};

static Xml disc = {"disc", Tcmplx, discfields, 0, sizeof(Disc)};

static Xml albumfields[] = {
	{"popularity",  Tfloat, nil,   offsetof(Album, pop),       0},
	{"name",        Tstr,   nil,   offsetof(Album, name),      0},
	{"artist-name", Tstr,   nil,   offsetof(Album, aname),     0},
	{"album-type",  Tstr,   nil,   offsetof(Album, atype),     0},
	{"review",      Tstr,   nil,   offsetof(Album, review),    0},
	{"copyright",   Tcmplx, copy,  offsetof(Album, copy),      sizeof(Copy)},
	{"discs",       Tlist,  &disc, offsetof(Album, discs),     0},
	{"cover",       Tfid,   nil,   offsetof(Album, c[0]),      0},
	{"cover-small", Tfid,   nil,   offsetof(Album, csmall[0]), 0},
	{"cover-large", Tfid,   nil,   offsetof(Album, clarge[0]), 0},
	{"artist-id",   Tid,    nil,   offsetof(Album, aid[0]),    0},
	{"id",          Tid,    nil,   offsetof(Album, id[0]),     0},
	end
};

static Xml album = {"album", Tcmplx, albumfields, 0, sizeof(Album)};

static Xml filefields[] = {
	{"id",     Tfid, nil, offsetof(Sfile, id[0]), 0},
	{"format", Tstr, nil, offsetof(Sfile, fmt),   0},
	end
};

static Xml file = {"file", Tcmplx, filefields, 0, sizeof(Sfile)};

static Xml trackfields[] = {
	{"track-number",    Tint,   nil,    offsetof(Track, n),         0},
	{"year",            Tint,   nil,    offsetof(Track, year),      0},
	{"length",          Tint,   nil,    offsetof(Track, len),       0},
	{"title",           Tstr,   nil,    offsetof(Track, name),      0},
	{"artist",          Tstr,   nil,    offsetof(Track, aname),     0},
	{"album",           Tstr,   nil,    offsetof(Track, alname),    0},
	{"album-artist",    Tstr,   nil,    offsetof(Track, aaname),    0},
	{"files",           Tlist,  &file,  offsetof(Track, files),     0},
	{"popularity",      Tfloat, nil,    offsetof(Track, pop),       0},
	{"explicit",        Tbool,  nil,    offsetof(Track, explicit),  0},
	{"id",              Tid,    nil,    offsetof(Track, id[0]),     0},
	{"artist-id",       Tid,    nil,    offsetof(Track, aid[0]),    0},
	{"album-id",        Tid,    nil,    offsetof(Track, alid[0]),   0},
	{"album-artist-id", Tid,    nil,    offsetof(Track, aaid[0]),   0},
	{"cover",           Tfid,   nil,    offsetof(Track, c[0]),      0},
	{"cover-small",     Tfid,   nil,    offsetof(Track, csmall[0]), 0},
	{"cover-large",     Tfid,   nil,    offsetof(Track, clarge[0]), 0},
	end
};

static Xml track = {"track", Tcmplx, trackfields, 0, sizeof(Track)};

static Xml searchfields[] = {
	{"artists", Tlist, &artist, offsetof(Search, artists), 0},
	{"albums",  Tlist, &album,  offsetof(Search, albums),  0},
	{"tracks",  Tlist, &track,  offsetof(Search, tracks),  0},
	end
};

static Xml search = {"result", Tcmplx, searchfields, 0, sizeof(Search)};

static Xml *type2xml[] = {
	[Xtsearch]	&search,
	[Xtartist]	&artist,
	[Xtalbum]	&album,
};

static void
freex(Xml *x, void *p)
{
	void *fp;
	void **lp;
	int i;

	if(p == nil)
		return;

	switch(x->type){
	case Tcmplx:
		for(x = x->chain; x->name != nil; x++){
			fp = *((void**)((uchar*)p + x->offset));
			freex(x, fp);
		}
		free(p);
		break;

	case Tlist:
	case Tlist2:
		lp = p;
		for(i = 0; lp[i] != nil; i++)
			freex(x->chain, lp[i]);
		free(lp);
		break;

	case Tstr:
		free(p);
		break;

	case Tfloat:
	case Tint:
	case Tbool:
	case Tfid:
	case Tid:
		break;
	}
}

void
xmlfree(int type, void *p)
{
	assert(type < nelem(type2xml) && type >= 0);
	freex(type2xml[type], p);
}

static char*
skiptag(char **s, State state)
{
	char *p;
	int len;

	p = *s;
	if((state.flags&Stflinl) != 0){
		p = utfutf(p, "/>");
		if(p != nil)
			p += 2;
	}else{
		len = strlen(state.t);
		for(; p != nil;){
			p = utfutf(p, "</");
			if(p != nil){
				if(strncmp(p+2, state.t, len) == 0 && p[2+len] == '>'){
					p += 2+len;
					break;
				}
				p += 2;
			}
		}
	}

	*s = p;
	return p;
}

static char*
gettag(char **s, State *state)
{
	char *p, *tag;
	int inl;

	inl = state->flags & Stflinl;

	for(p = *s; *p != 0; p++){
		if(!inl){
			if(*p != '<')
				continue;
			p++;
		}else
		if(*p == ' ')
			continue;

		if(*p >= 'a' && *p <= 'z')
			break;
		if(*p == '/' && (!inl || p[1] == '>')){
			*s = inl ? p+2 : p+1;
			state->flags &= ~Stflinl;
			return nil;
		}

		werrstr("unexpected '%c%c'", *p, *(p+1));
		goto error;
	}

	if(*p == 0){
		werrstr("unexpected EOF");
		goto error;
	}

	for(tag = p, p++; *p != 0; p++){
		if((*p >= 'a' && *p <= 'z') || *p == '-')
			continue;

		if(inl){
			if(*p == '='){
				*p = 0;
				break;
			}
		}else{
			if(*p == ' '){
				*p = 0;
				state->flags |= Stflinl;
				break;
			}
			if(*p == '>'){
				*p = 0;
				break;
			}
			if(*p == '/' && p[1] == '>'){
				*s = p+2;
				return gettag(s, state);
			}
		}

		werrstr("unexpected '%c' as tag name end", *p);
		goto error;
	}

	*s = p+1;
	return tag;

error:
	werrstr("gettag: %r");
	state->flags |= Stflerr;
	return nil;
}

static char*
getvalue(char **s, State *state)
{
	char *p, *v;
	int len;

	p = *s;
	if((state->flags&Stflinl) != 0){
		if(*p != '"'){
			werrstr("expected '\"'");
			goto error;
		}
		p++;
		v = p;
		p = utfrune(p, '"');
		if(p != nil){
			*p = 0;
			*s = p+1;
			return v;
		}
		werrstr("unclosed double quote");
		goto error;
	}

	v = p;
repeat:
	p = utfrune(p, '<');
	if(p == nil){
		werrstr("unexpected EOF");
		goto error;
	}

	p++;
	len = strlen(state->t);
	if(strncmp(p, "![CDATA[", 8) == 0){
		v = p+8;
		p = utfutf(v, "]]");
		if(p == nil){
			werrstr("CDATA not closed");
			goto error;
		}
		*p = 0;
		p += 3;
		goto repeat;
	}

	if(*p != '/' || strncmp(p+1, state->t, len) != 0){
		werrstr("tag not closed");
		goto error;
	}
	*(p-1) = 0;
	*s = p+1+len+1;
	return v;

error:
	state->flags |= Stflerr;
	werrstr("getvalue: %r");
	return nil;
}

static void
hex2hash(char *hex, uchar *hash, int size)
{
	static uchar h2d[] = {
		['0'] 0,
		['1'] 1,
		['2'] 2,
		['3'] 3,
		['4'] 4,
		['5'] 5,
		['6'] 6,
		['7'] 7,
		['8'] 8,
		['9'] 9,
		['a'] 10,
		['b'] 11,
		['c'] 12,
		['d'] 13,
		['e'] 14,
		['f'] 15,
	};
	int i;

	for(i = 0; i < size*2; i += 2)
		hash[i>>1] = h2d[hex[i]]<<4 | h2d[hex[i+1]];
}

static char*
unxml(char *orig)
{
	char *s;
	int i, n;

	n = 0;
	for(s = orig; *s; s++, n++){
		orig[n] = *s;
		for(i = 0; i < nelem(escmap); i++){
			if(strncmp(s, &escmap[i][2], escmap[i][0]) == 0){
				orig[n] = escmap[i][1];
				s += escmap[i][0] - 1;
				break;
			}
		}
	}

	orig[n] = 0;
	return orig;
}

static void*
parse(Xml *x, char **s, State state, void **r)
{
	State st;
	Xml *subx;
	char *t, *v;
	void **pa;
	int num;

	assert(r != nil);
	if(!**s)
		return nil;

	st = state;
	switch(x->type){
	case Tcmplx:
		if(debug > 1)
			fprint(2, "%*c%s\n", state.indent*4, 0, state.t);
		*r = mallocz(x->size, 1);
		assert(x->chain != nil);
		for(;;){
			st = state;
			t = gettag(s, &st);
			if((state.flags&Stflerr) != 0)
				goto error;
			if(t == nil)
				break;
			st.t = t;
			st.indent++;
			for(subx = x->chain; subx->name != nil; subx++){
				if(strcmp(subx->name, t) == 0){
					parse(subx, s, st, (void**)((uchar*)*r + subx->offset));
					break;
				}
			}
			if(subx->name == nil && skiptag(s, st) == nil)
					goto error;
		}
		break;

	case Tlist2:
		subx = x->chain;
		assert(subx != nil);
		pa = *r;
		for(num = 1; pa != nil && pa[num-1] != nil; num++);
		pa = realloc(pa, sizeof(void*)*(num+1));
		*r = pa;
		pa[num] = pa[num-1] = nil;
		parse(subx, s, st, &pa[num-1]);
		break;

	case Tlist:
		if(debug > 1)
			fprint(2, "%*c%s\n", state.indent*4, 0, state.t);
		subx = x->chain;
		assert(subx != nil);
		for(num = 1;;){
			st = state;
			t = gettag(s, &st);
			if((state.flags&Stflerr) != 0)
				goto error;
			if(t == nil)
				break;
			st.t = t;
			st.indent++;
			if(strcmp(subx->name, t) == 0){
				*r = realloc(*r, sizeof(void*)*(num+1));
				pa = *r;
				pa[num] = pa[num-1] = nil;
				if(nil != parse(subx, s, st, &pa[num-1]))
					num++;
			}
		}
		break;

	default:
		v = getvalue(s, &state);
		if((state.flags&Stflerr) != 0)
			goto error;
		if(debug > 1)
			fprint(2, "%*c%s=%s\n", state.indent*4, 0, state.t, v);
		switch(x->type){
		case Tstr:
			*(char**)r = unxml(strdup(v));
			break;
		case Tfloat:
			*(double*)r = atof(v);
			break;
		case Tint:
			*(int*)r = atoi(v);
			break;
		case Tfid:
			hex2hash(v, (uchar*)r, Xfidsz);
			break;
		case Tid:
			hex2hash(v, (uchar*)r, Xidsz);
			break;
		case Tbool:
			*(int*)r = (strcmp(v, "true") == 0);
			break;
		}
	}

	return *r;
error:
	perror("parse");
	return nil;
}

void*
xmlparse(int type, char *s)
{
	Xml *x;
	State state;
	void *r;

	assert(type < nelem(type2xml) && type >= 0);
	x = type2xml[type];

	for(; *s; s++){
		if(s[0] == '?' && s[1] == '>'){
			s += 2;
			break;
		}
	}
	if(*s == 0)
		return nil;

	state.t = nil;
	state.flags = 0;
	state.indent = 0;
	state.t = gettag(&s, &state);
	if(strcmp(state.t, x->name) != 0){
		if(debug > 0)
			fprint(2, "unexpected tag '%s', expected '%s'\n", state.t, x->name);
		return nil;
	}

	return parse(x, &s, state, &r);
}
