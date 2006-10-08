%define snap 1909

%bcond_without	fedora

Name:		openpbx
Version:	0
Release:	1.svn%{snap}%{?dist}
Summary:	The Truly Open Source PBX

Group:		Applications/Internet
License:	GPL
URL:		http://www.openpbx.org/
# svn://svn.openpbx.org/openpbx/trunk
Source0:	openpbx-r%{snap}.tar.gz
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:	spandsp-devel >= 0.0.3-1.pre24
BuildRequires:	libtool automake autoconf
BuildRequires:	fedora-usermgmt-devel bluez-libs-devel openldap-devel
BuildRequires:	libjpeg-devel loudmouth-devel nspr-devel js-devel ncurses-devel
BuildRequires:	unixODBC-devel openssl-devel zlib-devel

Requires:	fedora-usermgmt /sbin/chkconfig

%{?FE_USERADD_REQ}

%description
OpenPBX is an Open Source PBX and telephony development platform that
can both replace a conventional PBX and act as a platform for developing
custom telephony applications for delivering dynamic content over a
telephone similarly to how one can deliver dynamic content through a
web browser using CGI and a web server.

OpenPBX talks to a variety of telephony hardware including BRI, PRI,
POTS, Bluetooth headsets and IP telephony clients using SIP and IAX
protocols protocol (e.g. ekiga or kphone).  For more information and a
current list of supported hardware, see www.openpbx.com.


%package devel
Group:		Applications/Internet
Summary:	Development package for %{name}
Requires:	%{name} = %{version}-%{release}

%description devel
Use this package for developing and building modules for OpenPBX


%package postgresql
Group:		Applications/Internet
Summary:	PostgreSQL support for OpenPBX
Requires:	%{name} = %{version}-%{release}

%description postgresql
This package contains modules for OpenPBX which make use of PostgreSQL.

%package odbc
Group:		Applications/Internet
Summary:	ODBC support for OpenPBX
Requires:	%{name} = %{version}-%{release}

%description odbc
This package contains modules for OpenPBX which make use of ODBC.

%package ldap
Group:		Applications/Internet
Summary:	LDAP support for OpenPBX
Requires:	%{name} = %{version}-%{release}

%description ldap
This package contains modules for OpenPBX which make use of LDAP.

%package bluetooth
Group:		Applications/Internet
Summary:	Bluetooth channel driver for OpenPBX
Requires:	%{name} = %{version}-%{release}

%description bluetooth
This package contains a Bluetooth channel driver for OpenPBX, which
allows Bluetooth headsets and telephones to be used for communication.

%package jabber
Group:		Applications/Internet
Summary:	Jabber support for OpenPBX
Requires:	%{name} = %{version}-%{release}

%description jabber
This package contains Jabber protocol support for OpenPBX.


%prep
%setup -q -n openpbx

%build
./bootstrap.sh

# res_sqlite seems to use internal functions of sqlite3 which don't 
# even _exist_ in current versions. Disable it until it's fixed.

%configure --with-directory-layout=lsb --with-chan_bluetooth \
	   --with-chan_fax --with-chan_capi --with-chan_alsa --with-app_ldap \
	   --disable-zaptel --enable-t38 --enable-postgresql --with-cdr-pgsql \
	   --with-res_config_pqsql --with-cdr-odbc --with-res_config_odbc \
	   --with-perl-shebang='#! /usr/bin/perl' --disable-builtin-sqlite3 \
	   --enable-javascript --with-res_js --enable-fast-install \
	   --enable-jabber --with-res_jabber # --with-res_sqlite

# Poxy fscking libtool is _such_ a pile of crap...
sed -i 's/^CC="gcc"/CC="gcc -Wl,--as-needed"/' libtool

# Poxy fscking autocrap isn't much better
sed -i 's:^/usr/bin/perl:#! /usr/bin/perl:' ogi/fastogi-test ogi/ogi-test.ogi

make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
rm -f $RPM_BUILD_ROOT/%{_libdir}/openpbx.org/modules/*.la
rm -f $RPM_BUILD_ROOT/%{_libdir}/openpbx.org/*.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/openpbx.org/*.la
mkdir -p $RPM_BUILD_ROOT%{_initrddir}
install contrib/fedora/openpbx $RPM_BUILD_ROOT%{_initrddir}/openpbx
mv $RPM_BUILD_ROOT/%{_datadir}/openpbx.org/ogi/eogi-*test $RPM_BUILD_ROOT/%{_sbindir}

%clean
rm -rf $RPM_BUILD_ROOT

%pre
%__fe_groupadd 30 -r openpbx &>/dev/null || :
%__fe_useradd  30 -r -s /sbin/nologin -d %{_sysconfdir}/openpbx.org -M \
                    -c 'OpenPBX user' -g openpbx openpbx &>/dev/null || :
%post
/sbin/chkconfig --add openpbx

%preun
test "$1" != 0 || /sbin/chkconfig --del openpbx

%postun
%__fe_userdel  openpbx &>/dev/null || :
%__fe_groupdel openpbx &>/dev/null || :

%files
%defattr(-,root,root,-)
%doc COPYING CREDITS LICENSE BUGS AUTHORS SECURITY README HARDWARE
%{_initrddir}/openpbx
%{_sbindir}/*
%{_bindir}/streamplayer
%dir %{_libdir}/openpbx.org
%{_libdir}/openpbx.org/lib*.so.*
%dir %{_libdir}/openpbx.org/modules
%{_libdir}/openpbx.org/modules/*.so
%{_mandir}/man8/openpbx.8.gz
%dir %{_datadir}/openpbx.org
%dir %attr(0755,root,root) %{_datadir}/openpbx.org/ogi
%{_datadir}/openpbx.org/ogi/*
%dir %attr(0755,openpbx,openpbx) %{_sysconfdir}/openpbx.org
%config(noreplace) %attr(0644,openpbx,openpbx) %{_sysconfdir}/openpbx.org/*
%attr(0755,openpbx,openpbx) %{_localstatedir}/spool/openpbx.org
%attr(0755,openpbx,openpbx) %{_localstatedir}/log/openpbx.org
%attr(0755,openpbx,openpbx) %{_localstatedir}/run/openpbx.org
# Unneeded
%exclude %{_sysconfdir}/openpbx.org/cdr_tds.conf
# Separately packaged
%exclude %{_libdir}/openpbx.org/modules/*pgsql.so
%exclude %{_libdir}/openpbx.org/modules/app_sql_postgres.so
%exclude %{_libdir}/openpbx.org/modules/app_ldap.so
%exclude %{_libdir}/openpbx.org/modules/cdr_odbc.so
%exclude %{_libdir}/openpbx.org/modules/chan_bluetooth.so
%exclude %{_libdir}/openpbx.org/modules/res_jabber.so
%exclude %{_sysconfdir}/openpbx.org/cdr_pgsql.conf
%exclude %{_sysconfdir}/openpbx.org/cdr_odbc.conf
%exclude %{_sysconfdir}/openpbx.org/chan_bluetooth.conf
%exclude %{_sysconfdir}/openpbx.org/res_jabber.conf

%files devel
%defattr(-,root,root,-)
%dir %{_includedir}/openpbx
%{_includedir}/openpbx/*.h
%{_libdir}/openpbx.org/lib*.so

%files postgresql
%{_libdir}/openpbx.org/modules/*pgsql.so
%{_libdir}/openpbx.org/modules/app_sql_postgres.so
%config(noreplace) %attr(0644,openpbx,openpbx) %{_sysconfdir}/openpbx.org/cdr_pgsql.conf

%files odbc
%{_libdir}/openpbx.org/modules/cdr_odbc.so
%config(noreplace) %attr(0644,openpbx,openpbx) %{_sysconfdir}/openpbx.org/cdr_odbc.conf

%files ldap
%{_libdir}/openpbx.org/modules/app_ldap.so

%files bluetooth
%{_libdir}/openpbx.org/modules/chan_bluetooth.so
%config(noreplace) %attr(0644,openpbx,openpbx) %{_sysconfdir}/openpbx.org/chan_bluetooth.conf

%files jabber
%{_libdir}/openpbx.org/modules/res_jabber.so
%config(noreplace) %attr(0644,openpbx,openpbx) %{_sysconfdir}/openpbx.org/res_jabber.conf

%changelog
* Thu Oct  5 2006 David Woodhouse <dwmw2@infradead.org>
- Initial build
