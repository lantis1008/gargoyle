VERSION=1


ifeq ($(CC),)
  CC=gcc
endif

ifeq ($(LD),)
  LD=ld
endif

ifeq ($(AR),)
  AR=ar
endif

ifeq ($(RANLIB),)
  RANLIB=ranlib
endif



OS=$(shell uname)
ifeq ($(OS),Darwin)
  LINK=$(LD)
  SHLIB_EXT=dylib
  SHLIB_FLAGS=-dylib
  SHLIB_FILE=libipttmctl.$(SHLIB_EXT).$(VERSION)
else
  LINK=$(CC)
  SHLIB_EXT=so
  SHLIB_FILE=libipttmctl.$(SHLIB_EXT).$(VERSION)
  SHLIB_FLAGS=-shared -Wl,-soname,$(SHLIB_FILE)
endif

CFLAGS:=$(CFLAGS) -Os
WARNING_FLAGS=-Wall -Wstrict-prototypes



all: libipttmctl

libipttmctl: libipttmctl.$(SHLIB_EXT) libipttmctl.a


libipttmctl.a: ipt_tmctl_static.o ipt_tmctl_safe_malloc_static.o
	if [ -e $@ ] ; then rm $@ ; fi
	$(AR) rc $@ $^
	$(RANLIB) $@
	

libipttmctl.$(SHLIB_EXT) : ipt_tmctl_dyn.o ipt_tmctl_safe_malloc_dyn.o
	if [ -e libipttmctl.$(SHLIB_EXT) ] ; then rm libipttmctl.$(SHLIB_EXT)* ; fi
	$(LINK) $(LDFLAGS) $(SHLIB_FLAGS) -o $(SHLIB_FILE) $^ -lc 
	ln -s $(SHLIB_FILE) libipttmctl.$(SHLIB_EXT)

%_dyn.o: %.c
	$(CC) $(CFLAGS) -fPIC $(WARNING_FLAGS) -o $@ -c $^

%_static.o: %.c
	$(CC) $(CFLAGS) $(WARNING_FLAGS) -o $@ -c $^




clean:
	cd utils
	rm -rf tm_get tm_set *.a *.o *~ .*sw*
	cd ..
	if [ -n "$(SHLIB_EXT)" ] ; then rm -rf *.$(SHLIB_EXT)* ; fi
	rm -rf *.a *.o *~ .*sw*