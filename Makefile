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

