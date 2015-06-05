CC=/usr/bin/gcc

VERSION=1

# compilation flags
CFLAGS=-g -Wall -D_GNU_SOURCE
LIBFLAGS=-shared -lc
LDFLAGS=-L ${LIBDIR} -Wl,-rpath=/lib64

# object files
obj=hgshm.o
libobj=hgshm_lib.o

# binary name
bins=hgshm dowork

libname=libhgshm.so
libname_VERSION=${libname}.${VERSION}
LIBS=-lhgshm -lrt -lpthread

# local lib creation dir
LIBDIR=.libs

all: hgshmlib hgshm dowork

${obj}:%.o:%.c
	${CC} ${CFLAGS} -c $^

${libobj}:%.o:%.c
	${CC} ${CFLAGS} -fPIC -c $^

dowork:%:%.c
	${CC} ${CFLAGS} -o $@ $^

hgshm:${obj}
	${CC} ${CFLAGS} ${LDFLAGS} -o hgshm ${obj} ${LIBS}

hgshmlib:${libobj}
	mkdir -p ${LIBDIR}
	${CC} ${LIBFLAGS} -o ${LIBDIR}/${libname_VERSION} ${libobj}
	cd ${LIBDIR} && ln -sf ${libname_VERSION} ${libname}

clean:
	rm -rf *.o ${bins} ${LIBDIR}
