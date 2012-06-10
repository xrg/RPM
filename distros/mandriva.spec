%define git_repo rpm
# define git_head mdv-xrg

%define lib64arches	x86_64 

%ifarch %lib64arches
    %define _lib lib64
%else
    %define _lib lib
%endif

%define _prefix /usr
%define _libdir %_prefix/%_lib
%define _bindir %_prefix/bin
%define _sysconfdir /etc
%define _datadir /usr/share
%define _defaultdocdir %_datadir/doc
%define _localstatedir /var
%define _infodir %_datadir/info

%if %{?apply_patches:0}%{?!apply_patches:1}
%define apply_patches %(for p in `grep '^Patch.*:' "%{_specdir}/rpm.spec" | cut -d':' -f2-`; do echo "patch -p1 -F0 -i %{_sourcedir}/$p"; done )
%endif

# Define directory which holds rpm config files, and some binaries actually
# NOTE: it remains */lib even on lib64 platforms as only one version
#       of rpm is supported anyway, per architecture
%define rpmdir %{_prefix}/lib/rpm

%if %{?mklibname:0}%{?!mklibname:1}
%define mklibname(ds)  %{_lib}%{1}%{?2:%{2}}%{?3:_%{3}}%{-s:-static}%{-d:-devel}
%endif

%if %{?distsuffix:0}%{?!distsuffix:1}
%define distsuffix .mga
%endif

%if %{?mkrel:0}%{?!mkrel:1}
%define mkrel(c:) %{-c: 0.%{-c*}.}%{1}%{?distsuffix:%distsuffix}%{?!distsuffix:.mga}%{?mageia_release:%mageia_release}%{?subrel:.%subrel}
%endif

%if %{?mips:0}%{?!mips:1}
%define mips		mips mipsel mips32 mips32el mips64 mips64el
%endif

%if %{?pyver:0}%{?!pyver:1}
%define pyver %(python -V 2>&1 | cut -f2 -d" " | cut -f1,2 -d".")
%endif

%define __find_requires %{rpmdir}/%{_real_vendor}/find-requires %{?buildroot:%{buildroot}} %{?_target_cpu:%{_target_cpu}}
%define __find_provides %{rpmdir}/%{_real_vendor}/find-provides

%define rpmversion	%{git_get_ver}
%define libver		4.9
%define libmajor	3
%define libmajorsign    1
%define release		%mkrel %{git_get_rel}
%define librpmname      %mklibname rpm  %{libmajor}
%define librpmnamedevel %mklibname -d rpm
%define librpmsign      %mklibname rpmsign %{libmajor}
%define librpmbuild     %mklibname rpmbuild %{libmajor}

%define buildpython 1
%define rpmsetup_version 1.34

%define builddebug 0
%{?_with_debug:%define builddebug 1}

%{?_with_python:%define buildpython 1}
%{?_without_python:%define buildpython 0}

# disable plugins initially
%define buildplugins 0
%{?_with_plugins:%define buildplugins 1}

Summary:	The RPM package management system
Name:		rpm
Epoch:		1
Version:        %{rpmversion}
Release:	%{release}
Group:		System/Configuration/Packaging
Source:		%git_bs_source rpm-%{version}.tar.gz
Source1:	%{name}-gitrpm.version
Source2:	%{name}-changelog.gitrpm.txt
License:	GPLv2+
BuildRequires:	autoconf
BuildRequires:	zlib-devel
BuildRequires:  bzip2-devel
BuildRequires:	liblzma-devel >= 4.999.6-0.alpha.5
BuildRequires:	automake
BuildRequires:	elfutils-devel
BuildRequires:	libbeecrypt-devel
BuildRequires:	ed
BuildRequires:	gettext-devel
BuildRequires:  libsqlite3-devel
BuildRequires:  db5.2-devel
BuildRequires:  neon-devel
BuildRequires:	popt-devel
BuildRequires:	nss-devel
BuildRequires:	magic-devel
BuildRequires:  rpm-%{_real_vendor}-setup-build %{?rpmsetup_version:>= %{rpmsetup_version}}
BuildRequires:  readline-devel
BuildRequires:	ncurses-devel
BuildRequires:  openssl-devel
BuildRequires:  lua5.1-devel
BuildRequires:  libcap-devel
# Needed for doc
#BuildRequires:	graphviz
BuildRequires:	tetex
%if %buildpython
BuildRequires:	python-devel
%endif
# for testsuite:
BuildRequires: fakechroot

Requires:	bzip2 >= 0.9.0c-2
Requires:	lzma
Requires:	cpio
Requires:	gawk
Requires:	glibc >= 2.1.92
Requires:	mktemp
Requires:	setup >= 2.2.0-8
Requires:	rpm-%{_real_vendor}-setup >= 1.85
Requires:	update-alternatives
Requires:	%librpmname = %epoch:%version-%release
URL:            http://rpm.org/
%define         git_url        http://rpm.org/git/rpm.git
Requires(pre):		rpm-helper
Requires(pre):		coreutils
Requires(postun):	rpm-helper

Conflicts: perl-URPM < 4.0-2.mga3

%description
RPM is a powerful command line driven package management system capable of
installing, uninstalling, verifying, querying, and updating software packages.
Each software package consists of an archive of files along with information
about the package like its version, a description, etc.

%package   -n %librpmbuild
Summary:   Libraries for building and signing RPM packages
Group:     System/Libraries
Obsoletes: rpm-build-libs%{_isa} < %{version}-%{release}
Provides: rpm-build-libs%{_isa} = %{version}-%{release}

%description -n %librpmbuild
This package contains the RPM shared libraries for building and signing
packages.

%package  -n %librpmsign
Summary:  Libraries for building and signing RPM packages
Group:    System/Libraries

%description -n %librpmsign
This package contains the RPM shared libraries for building and signing
packages.

%package -n %librpmname
Summary:  Library used by rpm
Group:	  System/Libraries
Provides: librpm = %version-%release

%description -n %librpmname
RPM is a powerful command line driven package management system capable of
installing, uninstalling, verifying, querying, and updating software packages.
This package contains common files to all applications based on rpm.

%package -n %librpmnamedevel
Summary:	Development files for applications which will manipulate RPM packages
Group:		Development/C
Requires:	rpm = %epoch:%{version}-%{release}
Provides:	librpm-devel = %version-%release
Provides:   	rpm-devel = %version-%release
Requires:       %librpmname = %epoch:%version-%release
Requires:       %librpmbuild = %epoch:%version-%release
Requires:       %librpmsign = %epoch:%version-%release

%description -n %librpmnamedevel
This package contains the RPM C library and header files.  These
development files will simplify the process of writing programs
which manipulate RPM packages and databases and are intended to make
it easier to create graphical package managers or any other tools
that need an intimate knowledge of RPM packages in order to function.

This package should be installed if you want to develop programs that
will manipulate RPM packages and databases.

%package build
Summary:	Scripts and executable programs used to build packages
Group:		System/Configuration/Packaging
Requires:	autoconf
Requires:	automake
Requires:	file
Requires:	gcc-c++
# We need cputoolize & amd64-* alias to x86_64-* in config.sub
Requires:	libtool-base
Requires:	patch
Requires:	make
Requires:	tar
Requires:	unzip
Requires:	elfutils
Requires:	perl(CPAN::Meta) >= 2.112.150
Requires:	perl(ExtUtils::MakeMaker) >= 6.570_700
Requires:       perl(YAML::Tiny)
Requires:	rpm = %epoch:%{version}-%{release}
Requires:	rpm-%{_real_vendor}-setup-build %{?rpmsetup_version:>= %{rpmsetup_version}}

%description build
This package contains scripts and executable programs that are used to
build packages using RPM.

%package sign
Summary: Package signing support
Group:   System/Base

%description sign
This package contains support for digitally signing RPM packages.

%if %buildpython
%package -n python-rpm
Summary:	Python bindings for apps which will manipulate RPM packages
Group:		Development/Python
Requires:	rpm = %epoch:%{version}-%{release}

%description -n python-rpm
The rpm-python package contains a module which permits applications
written in the Python programming language to use the interface
supplied by RPM (RPM Package Manager) libraries.

This package should be installed if you want to develop Python
programs that will manipulate RPM packages and databases.
%endif

%prep
%git_get_source
%setup -q

%build
autoreconf -i

%if %builddebug
RPM_OPT_FLAGS=-g
%endif
CFLAGS="$RPM_OPT_FLAGS -fPIC" CXXFLAGS="$RPM_OPT_FLAGS -fPIC" \
    %configure2_5x \
        --enable-nls \
        --enable-python \
        --enable-sqlite3 \
        --without-javaglue \
%if %builddebug
        --enable-debug \
%endif
        --with-external-db \
%if %buildpython
        --with-python=%{pyver} \
%else
        --without-python \
%endif
%if ! %buildplugins
        --disable-plugins \
%endif
        --with-glob \
        --without-selinux \
        --without-apidocs \
        --with-cap

%make

%install
rm -rf $RPM_BUILD_ROOT

make DESTDIR=%buildroot install

find $RPM_BUILD_ROOT -name "*.la"|xargs rm -f

#mv -f $RPM_BUILD_ROOT/%{rpmdir}/rpmdiff $RPM_BUILD_ROOT/%{_bindir}

# Save list of packages through cron
mkdir -p ${RPM_BUILD_ROOT}/etc/cron.daily
install -m 755 scripts/rpm.daily ${RPM_BUILD_ROOT}/etc/cron.daily/rpm

mkdir -p ${RPM_BUILD_ROOT}/etc/logrotate.d
install -m 644 scripts/rpm.log ${RPM_BUILD_ROOT}/etc/logrotate.d/rpm

mkdir -p $RPM_BUILD_ROOT/var/lib/rpm
for dbi in \
	Basenames Conflictname Dirnames Group Installtid Name Providename \
	Provideversion Removetid Requirename Requireversion Triggername \
	Obsoletename Packages Sha1header Sigmd5 __db.001 __db.002 \
	__db.003 __db.004 __db.005 __db.006 __db.007 __db.008 __db.009
do
    touch $RPM_BUILD_ROOT/var/lib/rpm/$dbi
done

test -d doc-copy || mkdir doc-copy
rm -rf doc-copy/*
ln -f doc/manual/* doc-copy/
rm -f doc-copy/Makefile*

mkdir -p $RPM_BUILD_ROOT/var/spool/repackage

mkdir -p %buildroot%_sysconfdir/rpm/macros.d
cat > %buildroot%_sysconfdir/rpm/macros <<EOF
# Put your own system macros here
# usually contains 

# Set this one according your locales
# %%_install_langs

EOF

%{rpmdir}/%{_host_vendor}/find-lang.pl $RPM_BUILD_ROOT %{name}

%check
exit 0
make check

%pre
/usr/share/rpm-helper/add-user rpm $1 rpm /var/lib/rpm /bin/false

rm -rf /usr/lib/rpm/*-mandrake-*
rm -rf /usr/lib/rpm/*-%{_real_vendor}-*


%post
# nuke __db.00? when updating to this rpm
rm -f /var/lib/rpm/__db.00?

if [ ! -f /var/lib/rpm/Packages ]; then
    /bin/rpm --initdb
fi

%postun
/usr/share/rpm-helper/del-user rpm $1 rpm

%define	rpmattr		%attr(0755, rpm, rpm)

%files -f %{name}.lang
%defattr(-,root,root)
%doc GROUPS CHANGES doc/manual/[a-z]*
%attr(0755,rpm,rpm) /bin/rpm
%attr(0755, rpm, rpm) %{_bindir}/rpm2cpio
%attr(0755, rpm, rpm) %{_bindir}/gendiff
%attr(0755, rpm, rpm) %{_bindir}/rpmdb
%attr(0755, rpm, rpm) %{_bindir}/rpmkeys
%attr(0755, rpm, rpm) %{_bindir}/rpmgraph
%attr(0755, rpm, rpm) %{_bindir}/rpmsign
%attr(0755, rpm, rpm) %{_bindir}/rpmquery
%attr(0755, rpm, rpm) %{_bindir}/rpmverify

%dir %{_localstatedir}/spool/repackage
%dir %{rpmdir}
%dir /etc/rpm
%config(noreplace) /etc/rpm/macros
%dir /etc/rpm/macros.d
%attr(0755, rpm, rpm) %{rpmdir}/config.guess
%attr(0755, rpm, rpm) %{rpmdir}/config.sub
%attr(0755, rpm, rpm) %{rpmdir}/rpmdb_*
%attr(0644, rpm, rpm) %{rpmdir}/macros
%attr(0755, rpm, rpm) %{rpmdir}/mkinstalldirs
%attr(0755, rpm, rpm) %{rpmdir}/rpm.*
%attr(0644, rpm, rpm) %{rpmdir}/rpmpopt*
%attr(0644, rpm, rpm) %{rpmdir}/rpmrc
%attr(0755, rpm, rpm) %{rpmdir}/elfdeps
%attr(0755, rpm, rpm) %{rpmdir}/script.req
%exclude %{rpmdir}/tcl.req

%rpmattr	%{rpmdir}/rpm2cpio.sh
%rpmattr	%{rpmdir}/tgpg

%dir %attr(   -, rpm, rpm) %{rpmdir}/fileattrs
%attr(0644, rpm, rpm) %{rpmdir}/fileattrs/*.attr

%dir %attr(   -, rpm, rpm) %{rpmdir}/platform/
%ifarch %{ix86} x86_64
%attr(   -, rpm, rpm) %{rpmdir}/platform/i*86-*
%attr(   -, rpm, rpm) %{rpmdir}/platform/athlon-*
%attr(   -, rpm, rpm) %{rpmdir}/platform/pentium*-*
%attr(   -, rpm, rpm) %{rpmdir}/platform/geode-*
%endif
%ifarch %{ix86}
%exclude %{rpmdir}/platform/amd64-linux/macros
%exclude %{rpmdir}/platform/ia32e-linux/macros
%exclude %{rpmdir}/platform/x86_64-linux/macros
%endif
%ifarch x86_64
%attr(   -, rpm, rpm) %{rpmdir}/platform/amd64-*
%attr(   -, rpm, rpm) %{rpmdir}/platform/x86_64-*
%attr(   -, rpm, rpm) %{rpmdir}/platform/ia32e-*
%endif
%ifarch %arm
%attr(   -, rpm, rpm) %{rpmdir}/platform/arm*
%else
%exclude %{rpmdir}/platform/arm*/macros
%endif
%ifarch %mips
%attr(   -, rpm, rpm) %{rpmdir}/platform/mips*
%endif
%attr(   -, rpm, rpm) %{rpmdir}/platform/noarch*
# new in 4.10.0:
%exclude %{rpmdir}/platform/alpha*-linux/macros
%exclude %{rpmdir}/platform/sparc*-linux/macros
%exclude %{rpmdir}/platform/ia64*-linux/macros
%exclude %{rpmdir}/platform/ppc*-linux/macros
%exclude %{rpmdir}/platform/s390*-linux/macros
%exclude %{rpmdir}/platform/sh*-linux/macros



%{_mandir}/man[18]/*.[18]*
%lang(pl) %{_mandir}/pl/man[18]/*.[18]*
%lang(ru) %{_mandir}/ru/man[18]/*.[18]*
%lang(ja) %{_mandir}/ja/man[18]/*.[18]*
%lang(sk) %{_mandir}/sk/man[18]/*.[18]*
%lang(fr) %{_mandir}/fr/man[18]/*.[18]*
%lang(ko) %{_mandir}/ko/man[18]/*.[18]*

%config(noreplace,missingok)	/etc/cron.daily/rpm
%config(noreplace,missingok)	/etc/logrotate.d/rpm

%attr(0755, rpm, rpm)	%dir %_localstatedir/lib/rpm

%define	rpmdbattr %attr(0644, rpm, rpm) %verify(not md5 size mtime) %ghost %config(missingok,noreplace)

%rpmdbattr	/var/lib/rpm/Basenames
%rpmdbattr	/var/lib/rpm/Conflictname
%rpmdbattr	/var/lib/rpm/__db.0*
%rpmdbattr	/var/lib/rpm/Dirnames
%rpmdbattr	/var/lib/rpm/Group
%rpmdbattr	/var/lib/rpm/Installtid
%rpmdbattr	/var/lib/rpm/Name
%rpmdbattr	/var/lib/rpm/Obsoletename
%rpmdbattr	/var/lib/rpm/Packages
%rpmdbattr	/var/lib/rpm/Providename
%rpmdbattr	/var/lib/rpm/Provideversion
%rpmdbattr	/var/lib/rpm/Removetid
%rpmdbattr	/var/lib/rpm/Requirename
%rpmdbattr	/var/lib/rpm/Requireversion
%rpmdbattr	/var/lib/rpm/Sha1header
%rpmdbattr	/var/lib/rpm/Sigmd5
%rpmdbattr	/var/lib/rpm/Triggername

%files build
%defattr(-,root,root)
%doc CHANGES
%doc doc-copy/*
%rpmattr	%{_bindir}/rpmbuild
%rpmattr        %{_bindir}/rpmspec
%rpmattr	%{_prefix}/lib/rpm/brp-*
%rpmattr	%{_prefix}/lib/rpm/check-files
%rpmattr	%{_prefix}/lib/rpm/debugedit
%rpmattr	%{_prefix}/lib/rpm/desktop-file.prov 
%rpmattr	%{_prefix}/lib/rpm/find-debuginfo.sh
%rpmattr	%{_prefix}/lib/rpm/find-lang.sh
%rpmattr	%{_prefix}/lib/rpm/find-provides
%rpmattr	%{_prefix}/lib/rpm/find-requires
%rpmattr	%{_prefix}/lib/rpm/fontconfig.prov
%rpmattr	%{_prefix}/lib/rpm/perldeps.pl
%rpmattr	%{_prefix}/lib/rpm/perl.prov
%rpmattr	%{_prefix}/lib/rpm/perl.req

%rpmattr	%{_prefix}/lib/rpm/check-buildroot
%rpmattr	%{_prefix}/lib/rpm/check-prereqs
%rpmattr	%{_prefix}/lib/rpm/check-rpaths
%rpmattr	%{_prefix}/lib/rpm/check-rpaths-worker
#rpmattr	%{_prefix}/lib/rpm/javadeps
%rpmattr	%{_prefix}/lib/rpm/libtooldeps.sh
%rpmattr	%{_prefix}/lib/rpm/macros.perl
%rpmattr	%{_prefix}/lib/rpm/macros.php
%rpmattr	%{_prefix}/lib/rpm/macros.python
%rpmattr	%{_prefix}/lib/rpm/mono-find-provides
%rpmattr	%{_prefix}/lib/rpm/mono-find-requires
%rpmattr	%{_prefix}/lib/rpm/ocaml-find-provides.sh
%rpmattr	%{_prefix}/lib/rpm/ocaml-find-requires.sh
%rpmattr	%{_prefix}/lib/rpm/osgideps.pl
%rpmattr	%{_prefix}/lib/rpm/pkgconfigdeps.sh
#%rpmattr	%{_prefix}/lib/rpm/rpmdiff

%rpmattr	%{_prefix}/lib/rpm/rpmdeps
#%rpmattr	%{_prefix}/lib/rpm/trpm
%rpmattr        %{_prefix}/lib/rpm/pythondeps.sh


%{_mandir}/man8/rpmbuild.8*
%{_mandir}/man8/rpmdeps.8*

%if %buildpython
%files -n python-rpm
%defattr(-,root,root)
%{_libdir}/python*/site-packages/rpm
%endif

%files -n %librpmname
%defattr(-,root,root)
%{_libdir}/librpm.so.%{libmajor}*
%{_libdir}/librpmio.so.%{libmajor}*
%if %{with plugins}
%{_libdir}/rpm-plugins
%endif

%files -n %librpmbuild
%{_libdir}/librpmbuild.so.%{libmajor}*

%files -n %librpmsign
%{_libdir}/librpmsign.so.%{libmajorsign}*

%files sign
%defattr(-,root,root)
%{_bindir}/rpmsign
%{_mandir}/man8/rpmsign.8*

%files -n %librpmnamedevel
%defattr(-,root,root)
%{_includedir}/rpm
%{_libdir}/librpm.so
%{_libdir}/librpmio.so
%{_libdir}/librpmbuild.so
%{_libdir}/librpmsign.so
%{_libdir}/pkgconfig/rpm.pc

%changelog -f  %{_sourcedir}/%{name}-changelog.gitrpm.txt