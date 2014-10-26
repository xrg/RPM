%define debug_package_and_restore %{nil}

Summary: sh -- dummy sh package
Name: sh
Version: 1.0
Release: 1
Group: Utilities
License: GPL
Distribution: RPM test suite.
Vendor: Red Hat Software
Packager: Red Hat Software <bugs@redhat.com>
URL: http://www.redhat.com
Buildarch: noarch
Provides: /bin/sh

%description
dummy sh package

%clean

%files
%defattr(-,root,root)

%changelog
* Tue Oct 20 1998 Jeff Johnson <jbj@redhat.com>
- create.
