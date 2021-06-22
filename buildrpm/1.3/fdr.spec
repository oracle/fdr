Summary:	Flight Data Recorder
Name:		fdr
URL:		https://github.com/oracle/fdr.git
Version:	1.3
Release:	1%{?dist}
License:	UPL
Source0:	http://people.redhat.com/steved/fdr/%{name}-%{version}.tar.xz

BuildRequires:	gcc
BuildRequires:	make
BuildRequires:	sed
BuildRequires:	systemd-rpm-macros
Requires:	systemd

%description
The flight data recorder, a daemon which enables ftrace probes
and harvests the data

%prep
%autosetup

%build
sed -i -e "s:^CFLAGS.*:CFLAGS = %{optflags}:" Makefile
%make_build


%install
mkdir -p %{buildroot}/%{_sbindir}
install -m 755 fdrd %{buildroot}/%{_sbindir}

mkdir -p %{buildroot}%{_datadir}/fdr/samples
install -m 644 samples/nfs %{buildroot}/%{_datadir}/fdr/samples
install -m 644 samples/nfs.logrotate %{buildroot}/%{_datadir}/fdr/samples

mkdir -p %{buildroot}/%{_unitdir}
install -m 644 %{name}.service %{buildroot}/%{_unitdir}/%{name}.service

mkdir -p %{buildroot}/%{_mandir}/man8
install -m 644 fdrd.man %{buildroot}/%{_mandir}/man8

%post
%systemd_post %{name}.service

%preun
%systemd_preun %{name}.service

%postun
%systemd_postun_with_restart %{name}.service

%files
%{_sbindir}/fdrd
%{_unitdir}/fdr.service
%{_datadir}/fdr/samples/nfs
%{_datadir}/fdr/samples/nfs.logrotate
%{_mandir}/man8/*
%doc README.md
%license LICENSE

%changelog
* Tue Jun 22 2021 Steve Dickson <steved@redhat.com>  1.3-1
- Initial commit
