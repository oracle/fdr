# Licensed under the Universal Permissive License v 1.0 as shown at
# https://oss.oracle.com/licenses/upl.
#
all:		fdrd

clean:
	rm fdrd

install:	fdrd
	mkdir -p /etc/fdr.d
	install -m 755 fdrd /usr/sbin

uninstall:
	rm -rf /etc/fdr.d /usr/sbin/fdrd

RPMBUILD_DIR ?= $(HOME)
LATEST_VERS ?= 1.2

tarball:
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

