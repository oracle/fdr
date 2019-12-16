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

tarball:
	tar --transform "s/^./fdr-1/" \
		-czf $(RPMBUILD_DIR)/rpmbuild/SOURCES/fdr-1.tar.gz .

rpm:
	cp rpmbuild/1.1/fdr.spec $(RPMBUILD_DIR)/rpmbuild/SPECS/fdr.spec
	rpmbuild -bb $(RPMBUILD_DIR)/rpmbuild/SPECS/fdr.spec 

