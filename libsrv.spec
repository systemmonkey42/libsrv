Name: libsrv
Version: 0.0
Release: 1
Source: http://dns.vanrein.org/srv/lib/libsrv-0.0.tgz
Copyright: GNU or BSD
Group: System Environment/Libraries
Summary: Support for failproof Internet service connection

%description
This library offers support for failproof connections to Internet services,
for those domains supporting such schemes. The library is used in Internet
tools (clients as well as servers) that wish to support an extremely high level
of robustness in connection establishment.

%prep
%setup

%build
cd src
make

%install
cd src
make install

%files
%doc README
%doc doc
%doc TODO
%doc FAQ
%doc netana
/usr/sbin/hello_server
/usr/sbin/hello_client
/lib/libsrv.so
/lib/libsrv.so.0
/lib/libsrv.so.0.0
/lib/libsrv.a
/usr/include/arpa/srvname.h
