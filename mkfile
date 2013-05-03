</$objtype/mkfile

TARG=spotifs
BIN=/$objtype/bin

OFILES=\
	access.$O\
	buf.$O\
	ch.$O\
	cmd.$O\
	pkt.$O\
	shn.$O\
	spotifs.$O\
	xml.$O\

HFILES=\
	spotifs.h\
	tree.h\
	xml.h\

UPDATE=\
	mkfile\
	$HFILES\
	${OFILES:%.$O=%.c}\

default:V:	all

</sys/src/cmd/mkmany
