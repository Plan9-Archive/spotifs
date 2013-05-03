#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>

enum {
	Shnsz=	16,
	Rsansz=	128,
	Numch=	16,

	/* shn */
	Rcv=	0,
	Snd=	1,

	/* channels */
	Chstinit=	0,
	Chstheader=	1,
	Chstdata=	2,
	Chsteof=	3,
	Chsterr=	4,
};

/* commands */
enum {
	Csecret=	0x02,
	Cping=		0x04,
	Cstream=	0x08,
	Cchdata=	0x09,
	Ccherr=		0x0a,
	Cchbr=		0x0b,
	Cgetkey=	0x0c,
	Ckey=		0x0d,
	Ckeyerr=	0x0e,
	Ccache=		0x0f,
	Cimg=		0x19,
	Cbrowse=	0x30,
	Csearch=	0x31,
	Cpong=		0x49,
	Cpongack=	0x4a,
};

typedef struct Buf Buf;
typedef struct Ch Ch;
typedef struct Msg Msg;
typedef struct Session Session;
typedef struct ShnState ShnState;
#pragma incomplete ShnState

struct Buf {
	int		size;
	int		allocated;
	int		i;			/* for Bufinflate */
	uchar	*data;
};

typedef void (*Chfunc) (Ch *ch, Buf *p);

struct Ch {
	int			state;
	int			id;
	AESstate	*aes;
	uchar		aesstream[16];
	Chfunc		f;
	void		*data;
	Chfunc		f2;
	void		*data2;
};

enum {
	MsgSearch,
	MsgBrowse,
	MsgIn,
	MsgFill,
};

enum {
	BrowseArtist=	1,
	BrowseAlbum=	2,
	BrowseTrack=	3,
};

typedef struct Msg Msg;
struct Msg {
	int type;
	void *req;
	union {
		struct {
			char	*string;
			int		offset;
			int		limit;
		}search;

		struct {
			int		kind;
			uchar	*id;
			void	*ret;
		}browse;

		struct {
			int		cmd;
			Buf		*b;
		}in;

		struct {
			void	*id;
			int		type;
		}fill;
	};
};

struct Session {
	int			conn;
	void		*msg;
	uchar		rsan[Rsansz];
	ShnState	*shn[2];
	uint		shniv[2];
	Ch			ch[Numch];
};

extern int debug;
extern Session *session;

Buf*		Bufnew(void);
void		Buffree(Buf *b);
void		Bufwrite(Buf *b, uchar *data, int size);
int			Bufread(Buf *b, int fd, int size);
int			Bufinflate(Buf *dst, Buf *src);
void		Bufgrow(Buf *b, int size);
void		Bufu8(Buf *b, uchar u);
void		Bufu16(Buf *b, ushort u);
void		Bufu32(Buf *b, uint u);

ShnState*	shnnew(uchar *k, int ksize);
void		shnnonce(ShnState *shn, uint nonce);
void		shnenc(ShnState *shn, uchar *p, int size);
void		shndec(ShnState *shn, uchar *p, int size);
void		shnend(ShnState *shn, uchar *p, int size);

int			pktread(int *cmd, Buf *b);
int			pktwrite( int cmd, Buf *b);

Ch*			chnew(Chfunc f, void *data, Chfunc f2, void *data2);
Ch*			chget(int id);
int			chfeed(Buf *b, int err);
void		chaes(Ch *ch, uchar *key, int size);
void		chinflate(Ch *ch, Buf *p);
void		chrawread(Ch *ch, Buf *p);

int			connect(Session *s, char *user, char *passwd);
void		sessproc(void*);
