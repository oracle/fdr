all:		fdrd

clean:
	rm fdrd

install:	fdrd
	mkdir -p /etc/fdr.d
	install -m 755 fdrd /usr/sbin

uninstall:
	rm -rf /etc/fdr.d /usr/sbin/fdrd

