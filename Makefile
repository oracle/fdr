# Licensed under the Universal Permissive License v 1.0 as shown at
# https://oss.oracle.com/licenses/upl.
#
CFLAGS       := -g

PREFIX       := /usr
SBINDIR      := $(PREFIX)/sbin
DATADIR      := $(PREFIX)/share
MANDIR8      := $(DATADIR)/man/man8
INSTALL      := install

RPMBUILD_DIR ?= $(HOME)
LATEST_VERS  ?= 1.3

all:		fdrd

fdrd: fdrd.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

clean:
	rm -f fdrd

install:	fdrd
	mkdir -p $(DESTDIR)$(SBINDIR)
	$(INSTALL) -m 0755 fdrd $(DESTDIR)$(SBINDIR)
	mkdir -p $(DESTDIR)$(MANDIR8)
	$(INSTALL) -m 0644 fdrd.man $(DESTDIR)$(MANDIR8)
	mkdir -p $(DESTDIR)$(DATADIR)/fdr/samples
	$(INSTALL) -m 0644 README.md $(DESTDIR)$(DATADIR)/fdr/README
	$(INSTALL) -m 0644 samples/nfs $(DESTDIR)$(DATADIR)/fdr/samples
	$(INSTALL) -m 0644 samples/nfs.logrotate $(DESTDIR)$(DATADIR)/fdr/samples

uninstall:
	rm -rf $(SBINDIR)/fdrd

tarball: clean
	tar --transform "s/^./fdr-$(LATEST_VERS)/" \
		--xz -cf $(RPMBUILD_DIR)/SOURCES/fdr-$(LATEST_VERS).tar.xz .

release:
	git tag -f fdr-$(LATEST_VERS)
	git archive --format=tar --prefix=fdr-$(LATEST_VERS)/ fdr-$(LATEST_VERS) \
		|  ( cd /tmp ; tar xf - )
	(cd /tmp ; tar cJf  $(RPMBUILD_DIR)/SOURCES/fdr-$(LATEST_VERS).tar.xz \
		fdr-$(LATEST_VERS))

rpm: tarball
	cp buildrpm/$(LATEST_VERS)/fdr.spec \
		$(RPMBUILD_DIR)/rpmbuild/SPECS/fdr.spec
	rpmbuild -bb $(RPMBUILD_DIR)/rpmbuild/SPECS/fdr.spec 

srpm: tarball
	cp buildrpm/$(LATEST_VERS)/fdr.spec \
		$(RPMBUILD_DIR)/rpmbuild/SPECS/fdr.spec
	rpmbuild -bs $(RPMBUILD_DIR)/rpmbuild/SPECS/fdr.spec 

