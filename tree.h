extern File *salbums;
extern File *sartists;
extern File *stracks;

enum
{
	Qroot,
	Qctl,
	Qsearch,

	Qsalbums,
	Qsartists,
	Qstracks,

	Qartist,
	Qalbum,
	Qtrack,
	Qimage,
};

enum
{
	Flcallaux=	(1<<0),
};

typedef struct Tab Tab;
struct Tab {
	char	*name;
	int		type;
	uvlong	path;
	ulong	perm;
	Tab		*parent;
	int		flags;
	Tab		**child;
	void	*aux;
	void	*aux2;
	void	*aux3;
};

typedef char* (*callaux)(Tab *t, void *req);

extern Tab root;
extern Tab albums;
extern Tab artists;
extern Tab tracks;

Tab*	addtab(Tab *root, char *name, uvlong path, ulong perm, void *aux, void *aux2);
