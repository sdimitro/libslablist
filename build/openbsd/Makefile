# This file should be used with the BSD version of make.
#
# Please email serapheimd@gmail.com for any issues,
# comments or suggestions.
#
# TODO:
# * Changing a header file makes everything recompile.
#   This should not be the case. Whenever a header file
#   has changed, only the source files that include it
#   should change.
# * OpenBSD has not /opt directory at the root of the
#   filesystem so I will have to find a good default
#   to install the library. According to
#   http://www.openbsd.org/faq/ports/specialtopics.html
#   all user-visible libraries should reside in
#   /usr/local/lib
#

include ../Makefile.master

SO_OUT= ${SLDIR}/${SO}

all: ${SO_OUT}

${SO_OUT}: ${C_OBJECTS}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${SO_OUT} ${C_OBJECTS} ${LIBS}

.c.o:
	${CC} ${CFLAGS} -o $@ -c $<

${C_OBJECTS}: ${C_HDRS}

clean:
	rm -f ${C_OBJECTS} ${SO_OUT}

