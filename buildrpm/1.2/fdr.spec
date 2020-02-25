Name:		fdr
URL:		https://github.com/oracle/fdr.git
Version:	1
Release:	2
Summary:	Flight Data Recorder
License:	UPL
Source0:	%{name}-%{version}.tar.gz

%description
The flight data recorder, a daemon which enables ftrace probes and harvests the data

%prep
%setup
%build
make

%install
mkdir -p %{buildroot}/usr/sbin
install -m 755 fdrd %{buildroot}/usr/sbin

mkdir -p %{buildroot}/etc/fdr.d/samples
install -m 644 README.md %{buildroot}/etc/fdr.d/README
install -m 644 samples/nfs %{buildroot}/etc/fdr.d/samples
install -m 644 samples/nfs.logrotate %{buildroot}/etc/fdr.d/samples

mkdir -p %{buildroot}/usr/lib/systemd/system
install -m 644 fdr.service %{buildroot}/usr/lib/systemd/system

%files
/usr/sbin/fdrd
/usr/lib/systemd/system/fdr.service
/etc/fdr.d/README
/etc/fdr.d/samples/nfs
/etc/fdr.d/samples/nfs.logrotate

%preun
systemctl stop fdr
systemctl disable fdr

%changelog
# 1.1 - initial release to github
# 1.2 - Add logrotate sample

