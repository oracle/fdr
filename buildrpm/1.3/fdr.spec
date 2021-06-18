Summary:	Flight Data Recorder
Name:		fdr
URL:		https://github.com/oracle/fdr.git
Version:	1.3
Release:	0%{?dist}
License:	UPL
Source0:	http://people.redhat.com/steved/fdr/%{name}-%{version}.tar.xz

%description
The flight data recorder, a daemon which enables ftrace probes
and harvests the data

%prep
%setup -q

%build
%make_build all

%install
mkdir -p %{buildroot}/%{_sbindir}
install -m 755 fdrd %{buildroot}/%{_sbindir}

mkdir -p %{buildroot}%{_datadir}/fdr/samples
install -m 644 README.md %{buildroot}/%{_datadir}/fdr/README
install -m 644 samples/nfs %{buildroot}/%{_datadir}/fdr/samples
install -m 644 samples/nfs.logrotate %{buildroot}/%{_datadir}/fdr/samples

mkdir -p %{buildroot}/%{_unitdir}
install -m 644 fdr.service %{buildroot}/%{_unitdir}

mkdir -p %{buildroot}/%{_mandir}/man8
install -m 644 fdrd.man %{buildroot}/%{_mandir}/man8

%files
%{_sbindir}/fdrd
%{_unitdir}/fdr.service
%{_datadir}/fdr/README
%{_datadir}/fdr/samples/nfs
%{_datadir}/fdr/samples/nfs.logrotate
%{_mandir}/man8/*

%preun
systemctl stop fdr
systemctl disable fdr

%changelog
* Thu Jun 17 2021 Steve Dickson <steved@redhat.com>  1.3-0
- Initial commit
