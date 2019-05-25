PKG_CONFIG = pkg-config

CFLAGS = -Wall -Wextra -g -O0

CPPFLAGS = -I/usr/X11R6/include \
       `$(PKG_CONFIG) --cflags fontconfig` \
       `$(PKG_CONFIG) --cflags freetype2`

LDFLAGS = -L/usr/X11R6/lib -lX11 -lutil -lXft \
       `$(PKG_CONFIG) --libs fontconfig` \
       `$(PKG_CONFIG) --libs freetype2`

all: goph

clean:
	rm -f goph

config.h: config.def.h
	cp config.def.h config.h

goph: goph.c config.h
