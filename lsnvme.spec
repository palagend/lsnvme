Name: lsnvme
Version: 0.1
Release: 1%{?dist}
Summary: list NVMe controllers, devices, and properties
Group: System Environment/Libraries
License: GPLv2
Url: https://github.com/emvn-fabrics/lsnvme
Source: %{name}-%{version}.tar.bz2

BuildRequires: systemd-devel

%description
lsnvme is a utility for displaying devices attached to the NVMe controller(s) 
or the controllers themselves and associated info.

%prep
%setup -q -n %{name}-%{version}

%build
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%makeinstall install

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_mandir}/man8/%{name}.8.gz
%doc AUTHORS COPYING README.md
%doc %{_datadir}/doc/%{name}-%{version}/*

%changelog
* Fri Aug 21 2015 Patrick McCormick <patrick.m.mccormick@intel.com>
- 0.1-1
- Initial spec
