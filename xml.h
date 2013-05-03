enum {
	Xtsearch=	0,
	Xtartist=	1,
	Xtalbum=	2,
	Xttracks=	3,

	Xfidsz=		20,
	Xidsz=		16,
};

typedef uchar Xfid[Xfidsz];
typedef uchar Xid[Xidsz];

typedef struct Album Album;
typedef struct Artist Artist;
typedef struct Bio Bio;
typedef struct Copy Copy;
typedef struct Disc Disc;
typedef struct Img Img;
typedef struct Search Search;
typedef struct Sfile Sfile;
typedef struct Track Track;

struct Album {
	Xid		id;
	double	pop;
	char	*name;
	char	*aname;
	char	*atype;
	char	*review;
	Copy	*©;
	Disc	**discs;
	Xfid	c;
	Xfid	csmall;
	Xfid	clarge;
	Xid		aid;
};	

struct Artist {
	Xid		id;
	double	pop;
	char	*name;
	Img		*img;
	Bio		**bios;
	char	*genres;
	char	*years;
	Track	**top;
	Artist	**≈;
	Album	**albums;
};

struct Bio {
	char	*text;
};

struct Copy {
	char	*c;
	char	*p;
};

struct Disc {
	int		n;
	char	*name;
	Track	**tracks;
};

struct Img {
	int		width;
	int		height;
	Xfid	id;
	Xfid	small;
	Xfid	large;
};

struct Search {
	Artist	**artists;
	Album	**albums;
	Track	**tracks;
};

struct Sfile {
	Xfid	id;
	char	*fmt;
};

struct Track {
	Xid		id;
	int		n;
	int		year;
	int		len;
	char	*name;
	char	*aname;
	char	*alname;
	char	*aaname;
	Sfile	**files;
	double	pop;
	int		☺;
	Xid		aid;
	Xid		alid;
	Xid		aaid;
	Xfid	c;
	Xfid	csmall;
	Xfid	clarge;
};

void	xmlfree(int type, void *p);
void*	xmlparse(int type, char *s);
