# We have to override the new %%install behavior because, well... the kernel is special.
%global __spec_install_pre %{___build_pre}

Summary: The Linux kernel

# For a stable, released kernel, released_kernel should be 1. For rawhide
# and/or a kernel built from an rc or git snapshot, released_kernel should
# be 0.
%global released_kernel 1

%global aarch64patches 0

# Sign modules on x86.  Make sure the config files match this setting if more
# architectures are added.
%ifarch %{ix86} x86_64
%global signmodules 1
%global zipmodules 1
%else
%global signmodules 0
%global zipmodules 0
%endif

%if %{zipmodules}
%global zipsed -e 's/\.ko$/\.ko.xz/'
%endif

# % define buildid .local

# baserelease defines which build revision of this kernel version we're
# building.  We used to call this fedora_build, but the magical name
# baserelease is matched by the rpmdev-bumpspec tool, which you should use.
#
# We used to have some extra magic weirdness to bump this automatically,
# but now we don't.  Just use: rpmdev-bumpspec -c 'comment for changelog'
# When changing base_sublevel below or going from rc to a final kernel,
# reset this by hand to 1 (or to 0 and then use rpmdev-bumpspec).
# scripts/rebase.sh should be made to do that for you, actually.
#
# NOTE: baserelease must be > 0 or bad things will happen if you switch
#       to a released kernel (released version will be < rc version)
#
# For non-released -rc kernels, this will be appended after the rcX and
# gitX tags, so a 3 here would become part of release "0.rcX.gitX.3"
#
%global baserelease 23
%global fedora_build %{baserelease}

# base_sublevel is the kernel version we're starting with and patching
# on top of -- for example, 3.1-rc7-git1 starts with a 3.0 base,
# which yields a base_sublevel of 0.
%define base_sublevel 10

## If this is a released kernel ##
%if 0%{?released_kernel}

# Do we have a -stable update to apply?
%define stable_update 84
# Set rpm version accordingly
%if 0%{?stable_update}
%define stablerev %{stable_update}
%define stable_base %{stable_update}
%endif
%define rpmversion 3.%{base_sublevel}.%{stable_update}

## The not-released-kernel case ##
%else
# The next upstream release sublevel (base_sublevel+1)
%define upstream_sublevel %(echo $((%{base_sublevel} + 1)))
# The rc snapshot level
%define rcrev 0
# The git snapshot level
%define gitrev 0
# Set rpm version accordingly
%define rpmversion 3.%{upstream_sublevel}.0
%endif
# Nb: The above rcrev and gitrev values automagically define Patch00 and Patch01 below.

# What parts do we want to build?  We must build at least one kernel.
# These are the kernels that are built IF the architecture allows it.
# All should default to 1 (enabled) and be flipped to 0 (disabled)
# by later arch-specific checks.

# The following build options are enabled by default.
# Use either --without <opt> in your rpmbuild command or force values
# to 0 in here to disable them.
#
# standard kernel
%define with_up        %{?_without_up:        0} %{?!_without_up:        1}
# kernel PAE (only valid for i686 (PAE) and ARM (lpae))
%define with_pae       %{?_without_pae:       0} %{?!_without_pae:       1}
# kernel-debug
%define with_debug     %{?_without_debug:     0} %{?!_without_debug:     1}
# kernel-headers
%define with_headers   %{?_without_headers:   0} %{?!_without_headers:   1}
# perf
%define with_perf      %{?_without_perf:      0} %{?!_without_perf:      1}
# tools
%define with_tools     %{?_without_tools:     0} %{?!_without_tools:     1}
%ifarch %{mips}
# kernel-debuginfo
%define with_debuginfo 0
%else
# kernel-debuginfo
%define with_debuginfo %{?_without_debuginfo: 0} %{?!_without_debuginfo: 1}
%endif
# kernel-bootwrapper (for creating zImages from kernel + initrd)
%define with_bootwrapper %{?_without_bootwrapper: 0} %{?!_without_bootwrapper: 1}
# Want to build a the vsdo directories installed
%define with_vdso_install %{?_without_vdso_install: 0} %{?!_without_vdso_install: 1}
#
# Additional options for user-friendly one-off kernel building:
#
# Only build the base kernel (--with baseonly):
%define with_baseonly  %{?_with_baseonly:     1} %{?!_with_baseonly:     0}
# Only build the pae kernel (--with paeonly):
%define with_paeonly   %{?_with_paeonly:      1} %{?!_with_paeonly:      0}
# Only build the debug kernel (--with dbgonly):
%define with_dbgonly   %{?_with_dbgonly:      1} %{?!_with_dbgonly:      0}
#
# should we do C=1 builds with sparse
%define with_sparse    %{?_with_sparse:       1} %{?!_with_sparse:       0}
#
# Cross compile requested?
%define with_cross    %{?_with_cross:         1} %{?!_with_cross:        0}
#
# build a release kernel on rawhide
%define with_release   %{?_with_release:      1} %{?!_with_release:      0}

# Set debugbuildsenabled to 1 for production (build separate debug kernels)
#  and 0 for rawhide (all kernels are debug kernels).
# See also 'make debug' and 'make release'.
%define debugbuildsenabled 1

# Want to build a vanilla kernel build without any non-upstream patches?
%define with_vanilla %{?_with_vanilla: 1} %{?!_with_vanilla: 0}

# pkg_release is what we'll fill in for the rpm Release: field
%if 0%{?released_kernel}

%define pkg_release %{fedora_build}%{?buildid}%{?dist}

%else

# non-released_kernel
%if 0%{?rcrev}
%define rctag .rc%rcrev
%else
%define rctag .rc0
%endif
%if 0%{?gitrev}
%define gittag .git%gitrev
%else
%define gittag .git0
%endif
%define pkg_release 0%{?rctag}%{?gittag}.%{fedora_build}%{?buildid}%{?dist}

%endif

# The kernel tarball/base version
%define kversion 3.%{base_sublevel}.%{stable_update}

%define make_target bzImage

%define KVERREL %{version}-%{release}.%{_target_cpu}
%define hdrarch %_target_cpu
%define asmarch %_target_cpu

%if 0%{!?nopatches:1}
%define nopatches 0
%endif

%if %{with_vanilla}
%define nopatches 1
%endif

%if %{nopatches}
%define with_bootwrapper 0
%define variant -vanilla
%endif

%if !%{debugbuildsenabled}
%define with_debug 0
%endif

%if !%{with_debuginfo}
%define _enable_debug_packages 0
%endif
%define debuginfodir /usr/lib/debug

# kernel PAE is only built on i686 and ARMv7.
%ifnarch i686 armv7hl
%define with_pae 0
%endif

# if requested, only build base kernel
%if %{with_baseonly}
%define with_pae 0
%define with_debug 0
%endif

# if requested, only build pae kernel
%if %{with_paeonly}
%define with_up 0
%define with_debug 0
%endif

# if requested, only build debug kernel
%if %{with_dbgonly}
%if %{debugbuildsenabled}
%define with_up 0
%define with_pae 0
%endif
%define with_pae 0
%define with_tools 0
%define with_perf 0
%endif

%define all_x86 i386 i686

%if %{with_vdso_install}
# These arches install vdso/ directories.
%define vdso_arches %{all_x86} x86_64 %{power64} s390 s390x aarch64
%endif

# Overrides for generic default options

# don't do debug builds on anything but i686 and x86_64
%ifnarch i686 x86_64
%define with_debug 0
%endif

# don't build noarch kernels or headers (duh)
%ifarch noarch
%define with_up 0
%define with_headers 0
%define with_tools 0
%define with_perf 0
%define all_arch_configs kernel-%{version}-*.config
%endif

# bootwrapper is only on ppc
# sparse blows up on ppc
%ifnarch %{power64}
%define with_bootwrapper 0
%define with_sparse 0
%endif

# Per-arch tweaks

%ifarch %{all_x86}
%define asmarch x86
%define hdrarch i386
%define pae PAE
%define all_arch_configs kernel-%{version}-i?86*.config
%define image_install_path boot
%define kernel_image arch/x86/boot/bzImage
%endif

%ifarch x86_64
%define asmarch x86
%define all_arch_configs kernel-%{version}-x86_64*.config
%define image_install_path boot
%define kernel_image arch/x86/boot/bzImage
%endif

%ifarch %{power64}
%define asmarch powerpc
%define hdrarch powerpc
%define image_install_path boot
%define make_target vmlinux
%define kernel_image vmlinux
%define kernel_image_elf 1
%ifarch ppc64 ppc64p7
%define all_arch_configs kernel-%{version}-ppc64*.config
%endif
%ifarch ppc64le
%define all_arch_configs kernel-%{version}-ppc64le.config
%endif
%endif

%ifarch %{mips}
%define asmarch mips
%define hdrarch mips
%define image_install_path boot
%define make_target vmlinuz
%define kernel_image vmlinuz
%define kernel_image_elf 1
%ifarch %{mips64}
%define all_arch_configs kernel-%{version}-mips64*.config
%else
%define all_arch_configs kernel-%{version}-mips*.config
%endif
%endif



%ifarch s390x
%define asmarch s390
%define hdrarch s390
%define all_arch_configs kernel-%{version}-s390x.config
%define image_install_path boot
%define make_target image
%define kernel_image arch/s390/boot/image
%define with_tools 0
%endif

%ifarch %{arm}
%define all_arch_configs kernel-%{version}-arm*.config
%define image_install_path boot
%define asmarch arm
%define hdrarch arm
%define pae lpae
%define make_target bzImage
%define kernel_image arch/arm/boot/zImage
# http://lists.infradead.org/pipermail/linux-arm-kernel/2012-March/091404.html
%define kernel_mflags KALLSYMS_EXTRA_PASS=1
# we only build headers/perf/tools on the base arm arches
# just like we used to only build them on i386 for x86
%ifnarch armv7hl
%define with_headers 0
%define with_perf 0
%define with_tools 0
%endif
%endif

%ifarch aarch64
%define all_arch_configs kernel-%{version}-aarch64*.config
%define asmarch arm64
%define hdrarch arm64
%define make_target Image.gz
%define kernel_image arch/arm64/boot/Image.gz
%define image_install_path boot
%endif

# Should make listnewconfig fail if there's config options
# printed out?
%if %{nopatches}
%define listnewconfig_fail 0
%else
%define listnewconfig_fail 0
%endif

# To temporarily exclude an architecture from being built, add it to
# %%nobuildarches. Do _NOT_ use the ExclusiveArch: line, because if we
# don't build kernel-headers then the new build system will no longer let
# us use the previous build of that package -- it'll just be completely AWOL.
# Which is a BadThing(tm).

# We only build kernel-headers on the following...
%if 0%{?aarch64patches}
%define nobuildarches i386 s390
%else
%define nobuildarches i386 s390 aarch64
%endif

%ifarch %nobuildarches
%define with_up 0
%define with_pae 0
%define with_debuginfo 0
%define with_perf 0
%define with_tools 0
%define _enable_debug_packages 0
%endif

%define with_pae_debug 0
%if %{with_pae}
%define with_pae_debug %{with_debug}
%endif

# Architectures we build tools/cpupower on
%define cpupowerarchs %{ix86} x86_64 %{power64} %{arm} aarch64 %{mips}

#
# Packages that need to be installed before the kernel is, because the %%post
# scripts use them.
#
%define kernel_prereq  fileutils, systemd >= 203-2
%define initrd_prereq  dracut >= 038-29


Name: kernel%{?variant}
Group: System Environment/Kernel
License: GPLv2 and Redistributable, no modification permitted
URL: http://www.kernel.org/
Version: %{rpmversion}
Release: %{pkg_release}.8
# DO NOT CHANGE THE 'ExclusiveArch' LINE TO TEMPORARILY EXCLUDE AN ARCHITECTURE BUILD.
# SET %%nobuildarches (ABOVE) INSTEAD
ExclusiveArch: %{all_x86} x86_64 ppc64 ppc64p7 s390 s390x %{arm} aarch64 ppc64le %{mips}
ExclusiveOS: Linux
%ifnarch %{nobuildarches}
Requires: kernel-%{?variant:%{variant}-}core-uname-r = %{KVERREL}%{?variant}
Requires: kernel-%{?variant:%{variant}-}modules-uname-r = %{KVERREL}%{?variant}
%endif


#
# List the packages used during the kernel build
#
BuildRequires: kmod, patch, bash, sh-utils, tar
BuildRequires: bzip2, xz, findutils, gzip, m4, perl, perl-Carp, make, diffutils, gawk, lzma
BuildRequires: gcc, binutils, redhat-rpm-config, hmaccalc
BuildRequires: net-tools, hostname, bc, git
%if %{with_sparse}
BuildRequires: sparse
%endif
%if %{with_perf}
BuildRequires: elfutils-devel zlib-devel binutils-devel newt-devel python-devel perl(ExtUtils::Embed) bison flex
BuildRequires: audit-libs-devel
%endif
%if %{with_tools}
BuildRequires: pciutils-devel gettext ncurses-devel
%endif
BuildConflicts: rhbuildsys(DiskFree) < 500Mb
%if %{with_debuginfo}
BuildRequires: rpm-build, elfutils
%define debuginfo_args --strict-build-id -r
%endif

%if %{signmodules}
BuildRequires: openssl
BuildRequires: pesign >= 0.10-4
%endif

%if %{with_cross}
BuildRequires: binutils-%{_build_arch}-linux-gnu, gcc-%{_build_arch}-linux-gnu
%define cross_opts CROSS_COMPILE=%{_build_arch}-linux-gnu-
%endif

Source0: ftp://ftp.kernel.org/pub/linux/kernel/v3.0/linux-%{kversion}.tar.gz

Source10: perf-man-%{kversion}.tar.gz
Source11: x509.genkey

Source15: merge.pl
Source16: mod-extra.list
Source17: mod-extra.sh
Source18: mod-sign.sh
Source89: filter-mips64el.sh
Source90: filter-x86_64.sh
Source91: filter-armv7hl.sh
Source92: filter-i686.sh
Source93: filter-aarch64.sh
Source95: filter-ppc64.sh
Source96: filter-ppc64le.sh
Source97: filter-s390x.sh
Source98: filter-ppc64p7.sh
Source99: filter-modules.sh
%define modsign_cmd %{SOURCE18}

Source19: Makefile.release
Source20: Makefile.config
Source21: config-debug
Source22: config-nodebug
Source23: config-generic
Source24: config-no-extra

Source30: config-x86-generic
Source31: config-i686-PAE
Source32: config-x86-32-generic

Source40: config-x86_64-generic

Source50: config-powerpc-generic
Source53: config-powerpc64
Source54: config-powerpc64p7
Source55: config-powerpc64le


Source70: config-s390x

Source100: config-arm-generic

# Unified ARM kernels
Source101: config-armv7-generic
Source102: config-armv7
Source103: config-armv7-lpae

Source110: config-arm64

# This file is intentionally left empty in the stock kernel. Its a nicety
# added for those wanting to do custom rebuilds with altered config opts.
Source1000: config-local

# Sources for kernel-tools
Source2000: cpupower.service
Source2001: cpupower.config

# Here should be only the patches up to the upstream canonical Linus tree.

# For a stable release kernel
%if 0%{?stable_update}
%if 0%{?stable_base}
%define    stable_patch_00  patch-4.%{base_sublevel}.%{stable_base}.xz
#Patch00: %{stable_patch_00}
%endif

# non-released_kernel case
# These are automagically defined by the rcrev and gitrev values set up
# near the top of this spec file.
%else
%if 0%{?rcrev}
#Patch00: patch-4.%{upstream_sublevel}-rc%{rcrev}.xz
%if 0%{?gitrev}
#Patch01: patch-4.%{upstream_sublevel}-rc%{rcrev}-git%{gitrev}.xz
%endif
%else
# pre-{base_sublevel+1}-rc1 case
%if 0%{?gitrev}
#Patch00: patch-4.%{base_sublevel}-git%{gitrev}.xz
%endif
%endif
%endif

#Patch10000:	0001-Sync-code-to-Linux-4.1.19-from-upstream.patch
%if !%{nopatches}

# END OF PATCH DEFINITIONS

%endif

BuildRoot: %{_tmppath}/kernel-%{KVERREL}-root

%description
The kernel meta package

#
# This macro does requires, provides, conflicts, obsoletes for a kernel package.
#	%%kernel_reqprovconf <subpackage>
# It uses any kernel_<subpackage>_conflicts and kernel_<subpackage>_obsoletes
# macros defined above.
#
%define kernel_reqprovconf \
Provides: kernel = %{rpmversion}-%{pkg_release}\
Provides: kernel-%{_target_cpu} = %{rpmversion}-%{pkg_release}%{?1:+%{1}}\
Provides: kernel-drm-nouveau = 16\
Provides: kernel-uname-r = %{KVERREL}%{?1:+%{1}}\
Requires(pre): %{kernel_prereq}\
Requires(pre): %{initrd_prereq}\
Requires(pre): linux-firmware >= 20130724-29.git31f6b30\
Requires(preun): systemd >= 200\
%{expand:%%{?kernel%{?1:_%{1}}_conflicts:Conflicts: %%{kernel%{?1:_%{1}}_conflicts}}}\
%{expand:%%{?kernel%{?1:_%{1}}_obsoletes:Obsoletes: %%{kernel%{?1:_%{1}}_obsoletes}}}\
%{expand:%%{?kernel%{?1:_%{1}}_provides:Provides: %%{kernel%{?1:_%{1}}_provides}}}\
# We can't let RPM do the dependencies automatic because it'll then pick up\
# a correct but undesirable perl dependency from the module headers which\
# isn't required for the kernel proper to function\
AutoReq: no\
AutoProv: yes\
%{nil}

%package headers
Summary: Header files for the Linux kernel for use by glibc
Group: Development/System
Obsoletes: glibc-kernheaders < 3.0-46
Provides: glibc-kernheaders = 3.0-46
%description headers
Kernel-headers includes the C header files that specify the interface
between the Linux kernel and userspace libraries and programs.  The
header files define structures and constants that are needed for
building most standard programs and are also needed for rebuilding the
glibc package.

%package bootwrapper
Summary: Boot wrapper files for generating combined kernel + initrd images
Group: Development/System
Requires: gzip binutils
%description bootwrapper
Kernel-bootwrapper contains the wrapper code which makes bootable "zImage"
files combining both kernel and initial ramdisk.

%package debuginfo-common-%{_target_cpu}
Summary: Kernel source files used by %{name}-debuginfo packages
Group: Development/Debug
%description debuginfo-common-%{_target_cpu}
This package is required by %{name}-debuginfo subpackages.
It provides the kernel source files common to all builds.

%if %{with_perf}
%package -n perf
Summary: Performance monitoring for the Linux kernel
Group: Development/System
License: GPLv2
%description -n perf
This package contains the perf tool, which enables performance monitoring
of the Linux kernel.

%package -n perf-debuginfo
Summary: Debug information for package perf
Group: Development/Debug
Requires: %{name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}
AutoReqProv: no
%description -n perf-debuginfo
This package provides debug information for the perf package.

# Note that this pattern only works right to match the .build-id
# symlinks because of the trailing nonmatching alternation and
# the leading .*, because of find-debuginfo.sh's buggy handling
# of matching the pattern against the symlinks file.
%{expand:%%global debuginfo_args %{?debuginfo_args} -p '.*%%{_bindir}/perf(\.debug)?|.*%%{_libexecdir}/perf-core/.*|.*%%{_libdir}/traceevent/plugins/.*|XXX' -o perf-debuginfo.list}

%package -n python-perf
Summary: Python bindings for apps which will manipulate perf events
Group: Development/Libraries
%description -n python-perf
The python-perf package contains a module that permits applications
written in the Python programming language to use the interface
to manipulate perf events.

%{!?python_sitearch: %global python_sitearch %(%{__python} -c "from distutils.sysconfig import get_python_lib; print get_python_lib(1)")}

%package -n python-perf-debuginfo
Summary: Debug information for package perf python bindings
Group: Development/Debug
Requires: %{name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}
AutoReqProv: no
%description -n python-perf-debuginfo
This package provides debug information for the perf python bindings.

# the python_sitearch macro should already be defined from above
%{expand:%%global debuginfo_args %{?debuginfo_args} -p '.*%%{python_sitearch}/perf.so(\.debug)?|XXX' -o python-perf-debuginfo.list}


%endif # with_perf

%if %{with_tools}
%package -n kernel-tools
Summary: Assortment of tools for the Linux kernel
Group: Development/System
License: GPLv2
Provides:  cpupowerutils = 1:009-0.6.p1
Obsoletes: cpupowerutils < 1:009-0.6.p1
Provides:  cpufreq-utils = 1:009-0.6.p1
Provides:  cpufrequtils = 1:009-0.6.p1
Obsoletes: cpufreq-utils < 1:009-0.6.p1
Obsoletes: cpufrequtils < 1:009-0.6.p1
Obsoletes: cpuspeed < 1:1.5-16
Requires: kernel-tools-libs = %{version}-%{release}
%description -n kernel-tools
This package contains the tools/ directory from the kernel source
and the supporting documentation.

%package -n kernel-tools-libs
Summary: Libraries for the kernels-tools
Group: Development/System
License: GPLv2
%description -n kernel-tools-libs
This package contains the libraries built from the tools/ directory
from the kernel source.

%package -n kernel-tools-libs-devel
Summary: Assortment of tools for the Linux kernel
Group: Development/System
License: GPLv2
Requires: kernel-tools = %{version}-%{release}
Provides:  cpupowerutils-devel = 1:009-0.6.p1
Obsoletes: cpupowerutils-devel < 1:009-0.6.p1
Requires: kernel-tools-libs = %{version}-%{release}
Provides: kernel-tools-devel
%description -n kernel-tools-libs-devel
This package contains the development files for the tools/ directory from
the kernel source.

%package -n kernel-tools-debuginfo
Summary: Debug information for package kernel-tools
Group: Development/Debug
Requires: %{name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}
AutoReqProv: no
%description -n kernel-tools-debuginfo
This package provides debug information for package kernel-tools.

# Note that this pattern only works right to match the .build-id
# symlinks because of the trailing nonmatching alternation and
# the leading .*, because of find-debuginfo.sh's buggy handling
# of matching the pattern against the symlinks file.
%{expand:%%global debuginfo_args %{?debuginfo_args} -p '.*%%{_bindir}/centrino-decode(\.debug)?|.*%%{_bindir}/powernow-k8-decode(\.debug)?|.*%%{_bindir}/cpupower(\.debug)?|.*%%{_libdir}/libcpupower.*|.*%%{_bindir}/turbostat(\.debug)?|.*%%{_bindir}/x86_energy_perf_policy(\.debug)?|.*%%{_bindir}/tmon(\.debug)?|XXX' -o kernel-tools-debuginfo.list}

%endif # with_tools


#
# This macro creates a kernel-<subpackage>-debuginfo package.
#	%%kernel_debuginfo_package <subpackage>
#
%define kernel_debuginfo_package() \
%package %{?1:%{1}-}debuginfo\
Summary: Debug information for package %{name}%{?1:-%{1}}\
Group: Development/Debug\
Requires: %{name}-debuginfo-common-%{_target_cpu} = %{version}-%{release}\
Provides: %{name}%{?1:-%{1}}-debuginfo-%{_target_cpu} = %{version}-%{release}\
AutoReqProv: no\
%description -n %{name}%{?1:-%{1}}-debuginfo\
This package provides debug information for package %{name}%{?1:-%{1}}.\
This is required to use SystemTap with %{name}%{?1:-%{1}}-%{KVERREL}.\
%{expand:%%global debuginfo_args %{?debuginfo_args} -p '/.*/%%{KVERREL}%{?1:[+]%{1}}/.*|/.*%%{KVERREL}%{?1:\+%{1}}(\.debug)?' -o debuginfo%{?1}.list}\
%{nil}

#
# This macro creates a kernel-<subpackage>-devel package.
#	%%kernel_devel_package <subpackage> <pretty-name>
#
%define kernel_devel_package() \
%package %{?1:%{1}-}devel\
Summary: Development package for building kernel modules to match the %{?2:%{2} }kernel\
Group: System Environment/Kernel\
Provides: kernel%{?1:-%{1}}-devel-%{_target_cpu} = %{version}-%{release}\
Provides: kernel-devel-%{_target_cpu} = %{version}-%{release}%{?1:+%{1}}\
Provides: kernel-devel = %{version}-%{release}%{?1:+%{1}}\
Provides: kernel-devel-uname-r = %{KVERREL}%{?1:+%{1}}\
AutoReqProv: no\
Requires(pre): /usr/bin/find\
Requires: perl\
%description -n kernel%{?variant}%{?1:-%{1}}-devel\
This package provides kernel headers and makefiles sufficient to build modules\
against the %{?2:%{2} }kernel package.\
%{nil}

#
# This macro creates a kernel-<subpackage>-modules-extra package.
#	%%kernel_modules_extra_package <subpackage> <pretty->
#
%define kernel_modules_extra_package() \
%package %{?1:%{1}-}modules-extra\
Summary: Extra kernel modules to match the %{?2:%{2} }kernel\
Group: System Environment/Kernel\
Provides: kernel%{?1:-%{1}}-modules-extra-%{_target_cpu} = %{version}-%{release}\
Provides: kernel%{?1:-%{1}}-modules-extra-%{_target_cpu} = %{version}-%{release}%{?1:+%{1}}\
Provides: kernel%{?1:-%{1}}-modules-extra = %{version}-%{release}%{?1:+%{1}}\
Provides: installonlypkg(kernel-module)\
Provides: kernel%{?1:-%{1}}-modules-extra-uname-r = %{KVERREL}%{?1:+%{1}}\
Requires: kernel-uname-r = %{KVERREL}%{?1:+%{1}}\
Requires: kernel%{?1:-%{1}}-modules-uname-r = %{KVERREL}%{?1:+%{1}}\
AutoReq: no\
AutoProv: yes\
%description -n kernel%{?variant}%{?1:-%{1}}-modules-extra\
This package provides less commonly used kernel modules for the %{?2:%{2} }kernel package.\
%{nil}

#
# This macro creates a kernel-<subpackage>-modules package.
#	%%kernel_modules_package <subpackage> <pretty-name>
#
%define kernel_modules_package() \
%package %{?1:%{1}-}modules\
Summary: kernel modules to match the %{?2:%{2}-}core kernel\
Group: System Environment/Kernel\
Provides: kernel%{?1:-%{1}}-modules-%{_target_cpu} = %{version}-%{release}\
Provides: kernel-modules-%{_target_cpu} = %{version}-%{release}%{?1:+%{1}}\
Provides: kernel-modules = %{version}-%{release}%{?1:+%{1}}\
Provides: installonlypkg(kernel-module)\
Provides: kernel%{?1:-%{1}}-modules-uname-r = %{KVERREL}%{?1:+%{1}}\
Requires: kernel-uname-r = %{KVERREL}%{?1:+%{1}}\
AutoReq: no\
AutoProv: yes\
%description -n kernel%{?variant}%{?1:-%{1}}-modules\
This package provides commonly used kernel modules for the %{?2:%{2}-}core kernel package.\
%{nil}

#
# this macro creates a kernel-<subpackage> meta package.
#	%%kernel_meta_package <subpackage>
#
%define kernel_meta_package() \
%package %{1}\
summary: kernel meta-package for the %{1} kernel\
group: system environment/kernel\
Requires: kernel-%{1}-%{?variant:%{variant}-}core-uname-r = %{KVERREL}%{?variant}+%{1}\
Requires: kernel-%{1}-%{?variant:%{variant}-}modules-uname-r = %{KVERREL}%{?variant}+%{1}\
%description %{1}\
The meta-package for the %{1} kernel\
%{nil}

#
# This macro creates a kernel-<subpackage> and its -devel and -debuginfo too.
#	%%define variant_summary The Linux kernel compiled for <configuration>
#	%%kernel_variant_package [-n <pretty-name>] <subpackage>
#
%define kernel_variant_package(n:) \
%package %{?1:%{1}-}core\
Summary: %{variant_summary}\
Group: System Environment/Kernel\
Provides: kernel-%{?1:%{1}-}core-uname-r = %{KVERREL}%{?1:+%{1}}\
%{expand:%%kernel_reqprovconf}\
%if %{?1:1} %{!?1:0} \
%{expand:%%kernel_meta_package %{?1:%{1}}}\
%endif\
%{expand:%%kernel_devel_package %{?1:%{1}} %{!?{-n}:%{1}}%{?{-n}:%{-n*}}}\
%{expand:%%kernel_modules_package %{?1:%{1}} %{!?{-n}:%{1}}%{?{-n}:%{-n*}}}\
%{expand:%%kernel_modules_extra_package %{?1:%{1}} %{!?{-n}:%{1}}%{?{-n}:%{-n*}}}\
%{expand:%%kernel_debuginfo_package %{?1:%{1}}}\
%{nil}

# Now, each variant package.

%ifnarch armv7hl
%define variant_summary The Linux kernel compiled for PAE capable machines
%kernel_variant_package %{pae}
%description %{pae}-core
This package includes a version of the Linux kernel with support for up to
64GB of high memory. It requires a CPU with Physical Address Extensions (PAE).
The non-PAE kernel can only address up to 4GB of memory.
Install the kernel-PAE package if your machine has more than 4GB of memory.
%else
%define variant_summary The Linux kernel compiled for Cortex-A15
%kernel_variant_package %{pae}
%description %{pae}-core
This package includes a version of the Linux kernel with support for
Cortex-A15 devices with LPAE and HW virtualisation support
%endif


%define variant_summary The Linux kernel compiled with extra debugging enabled for PAE capable machines
%kernel_variant_package %{pae}debug
Obsoletes: kernel-PAE-debug
%description %{pae}debug-core
This package includes a version of the Linux kernel with support for up to
64GB of high memory. It requires a CPU with Physical Address Extensions (PAE).
The non-PAE kernel can only address up to 4GB of memory.
Install the kernel-PAE package if your machine has more than 4GB of memory.

This variant of the kernel has numerous debugging options enabled.
It should only be installed when trying to gather additional information
on kernel bugs, as some of these options impact performance noticably.


%define variant_summary The Linux kernel compiled with extra debugging enabled
%kernel_variant_package debug
%description debug-core
The kernel package contains the Linux kernel (vmlinuz), the core of any
Linux operating system.  The kernel handles the basic functions
of the operating system:  memory allocation, process allocation, device
input and output, etc.

This variant of the kernel has numerous debugging options enabled.
It should only be installed when trying to gather additional information
on kernel bugs, as some of these options impact performance noticably.

# And finally the main -core package

%define variant_summary The Linux kernel
%kernel_variant_package 
%description core
The kernel package contains the Linux kernel (vmlinuz), the core of any
Linux operating system.  The kernel handles the basic functions
of the operating system: memory allocation, process allocation, device
input and output, etc.


%prep
# do a few sanity-checks for --with *only builds
%if %{with_baseonly}
%if !%{with_up}%{with_pae}
echo "Cannot build --with baseonly, up build is disabled"
exit 1
%endif
%endif

%if "%{baserelease}" == "0"
echo "baserelease must be greater than zero"
exit 1
%endif

# more sanity checking; do it quietly
if [ "%{patches}" != "%%{patches}" ] ; then
  for patch in %{patches} ; do
    if [ ! -f $patch ] ; then
      echo "ERROR: Patch  ${patch##/*/}  listed in specfile but is missing"
      exit 1
    fi
  done
fi 2>/dev/null

patch_command='patch -p1 -F1 -s'
ApplyPatch()
{
  local patch=$1
  shift
  if [ ! -f $RPM_SOURCE_DIR/$patch ]; then
    exit 1
  fi
  if ! grep -E "^Patch[0-9]+: $patch\$" %{_specdir}/${RPM_PACKAGE_NAME%%%%%{?variant}}.spec ; then
    if [ "${patch:0:8}" != "patch-3." ] ; then
      echo "ERROR: Patch  $patch  not listed as a source patch in specfile"
      exit 1
    fi
  fi 2>/dev/null
  case "$patch" in
  *.bz2) bunzip2 < "$RPM_SOURCE_DIR/$patch" | $patch_command ${1+"$@"} ;;
  *.gz)  gunzip  < "$RPM_SOURCE_DIR/$patch" | $patch_command ${1+"$@"} ;;
  *.xz)  unxz    < "$RPM_SOURCE_DIR/$patch" | $patch_command ${1+"$@"} ;;
  *) $patch_command ${1+"$@"} < "$RPM_SOURCE_DIR/$patch" ;;
  esac
}

# don't apply patch if it's empty
ApplyOptionalPatch()
{
  local patch=$1
  shift
  if [ ! -f $RPM_SOURCE_DIR/$patch ]; then
    exit 1
  fi
  local C=$(wc -l $RPM_SOURCE_DIR/$patch | awk '{print $1}')
  if [ "$C" -gt 9 ]; then
    ApplyPatch $patch ${1+"$@"}
  fi
}

GitPatch()
{
  local patch=$1
  shift
  if [ ! -f $RPM_SOURCE_DIR/$patch ]; then
    exit 1
  fi
  local C=$(wc -l $RPM_SOURCE_DIR/$patch | awk '{print $1}')
  if [ "$C" -gt 9 ]; then
     git apply "$RPM_SOURCE_DIR/$patch"
  fi
}

# First we unpack the kernel tarball.
# If this isn't the first make prep, we use links to the existing clean tarball
# which speeds things up quite a bit.

# Update to latest upstream.
%if 0%{?released_kernel}
%define vanillaversion 3.%{base_sublevel}
# non-released_kernel case
%else
%if 0%{?rcrev}
%define vanillaversion 3.%{upstream_sublevel}-rc%{rcrev}
%if 0%{?gitrev}
%define vanillaversion 3.%{upstream_sublevel}-rc%{rcrev}-git%{gitrev}
%endif
%else
# pre-{base_sublevel+1}-rc1 case
%if 0%{?gitrev}
%define vanillaversion 3.%{base_sublevel}-git%{gitrev}
%else
%define vanillaversion 3.%{base_sublevel}
%endif
%endif
%endif

# %%{vanillaversion} : the full version name, e.g. 2.6.35-rc6-git3
# %%{kversion}       : the base version, e.g. 2.6.34

# Use kernel-%%{kversion}%%{?dist} as the top-level directory name
# so we can prep different trees within a single git directory.

# Build a list of the other top-level kernel tree directories.
# This will be used to hardlink identical vanilla subdirs.
sharedirs=$(find "$PWD" -maxdepth 1 -type d -name 'kernel-3.*' \
            | grep -x -v "$PWD"/kernel-%{kversion}%{?dist}) ||:

# Delete all old stale trees.
if [ -d kernel-%{kversion}%{?dist} ]; then
  cd kernel-%{kversion}%{?dist}
  for i in linux-*
  do
     if [ -d $i ]; then
       # Just in case we ctrl-c'd a prep already
       rm -rf deleteme.%{_target_cpu}
       # Move away the stale away, and delete in background.
       mv $i deleteme-$i
       rm -rf deleteme* &
     fi
  done
  cd ..
fi

# Generate new tree
if [ ! -d kernel-%{kversion}%{?dist}/vanilla-%{vanillaversion} ]; then

  if [ -d kernel-%{kversion}%{?dist}/vanilla-%{kversion} ]; then

    # The base vanilla version already exists.
    cd kernel-%{kversion}%{?dist}

    # Any vanilla-* directories other than the base one are stale.
    for dir in vanilla-*; do
      [ "$dir" = vanilla-%{kversion} ] || rm -rf $dir &
    done

  else

    rm -f pax_global_header
    # Look for an identical base vanilla dir that can be hardlinked.
    for sharedir in $sharedirs ; do
      if [[ ! -z $sharedir  &&  -d $sharedir/vanilla-%{kversion} ]] ; then
        break
      fi
    done
    if [[ ! -z $sharedir  &&  -d $sharedir/vanilla-%{kversion} ]] ; then
%setup -q -n kernel-%{kversion}%{?dist} -c -T
      cp -al $sharedir/vanilla-%{kversion} .
    else
%setup -q -n kernel-%{kversion}%{?dist} -c
      mv linux-%{kversion} vanilla-%{kversion}
    fi

  fi

%if "%{kversion}" != "%{vanillaversion}"

  for sharedir in $sharedirs ; do
    if [[ ! -z $sharedir  &&  -d $sharedir/vanilla-%{vanillaversion} ]] ; then
      break
    fi
  done
  if [[ ! -z $sharedir  &&  -d $sharedir/vanilla-%{vanillaversion} ]] ; then

    cp -al $sharedir/vanilla-%{vanillaversion} .

  else

    # Need to apply patches to the base vanilla version.
    cp -al vanilla-%{kversion} vanilla-%{vanillaversion}
    cd vanilla-%{vanillaversion}

# Update vanilla to the latest upstream.
# (non-released_kernel case only)
%if 0%{?rcrev}
#    ApplyPatch patch-3.%{upstream_sublevel}-rc%{rcrev}.xz
%if 0%{?gitrev}
#    ApplyPatch patch-3.%{upstream_sublevel}-rc%{rcrev}-git%{gitrev}.xz
%endif
%else
# pre-{base_sublevel+1}-rc1 case
%if 0%{?gitrev}
#    ApplyPatch patch-3.%{base_sublevel}-git%{gitrev}.xz
%endif
%endif

    cd ..

  fi

%endif

else

  # We already have all vanilla dirs, just change to the top-level directory.
  cd kernel-%{kversion}%{?dist}

fi

# Now build the fedora kernel tree.
cp -al vanilla-%{vanillaversion} linux-%{KVERREL}

cd linux-%{KVERREL}

# released_kernel with possible stable updates
%if 0%{?stable_base}
#ApplyPatch %{stable_patch_00}
%endif

# Loongson (mips64el)
# rebuild with specific kernel config file same as gerrit provides. 
%ifnarch %{mips}

# Drop some necessary files from the source dir into the buildroot
cp $RPM_SOURCE_DIR/config-* .
cp %{SOURCE15} .

%if !%{debugbuildsenabled}
%if %{with_release}
# The normal build is a really debug build and the user has explicitly requested
# a release kernel. Change the config files into non-debug versions.
make -f %{SOURCE19} config-release
%endif
%endif

# Dynamically generate kernel .config files from config-* files
make -f %{SOURCE20} VERSION=%{version} configs

# Merge in any user-provided local config option changes
%ifnarch %nobuildarches
for i in %{all_arch_configs}
do
  mv $i $i.tmp
  ./merge.pl %{SOURCE1000} $i.tmp > $i
  rm $i.tmp
done
%endif

%if !%{nopatches}


#GitPatch 0001-Sync-code-to-Linux-4.1.19-from-upstream.patch


%if 0%{?aarch64patches}
ApplyPatch kernel-arm64.patch
%ifnarch aarch64 # this is stupid, but i want to notice before secondary koji does.
ApplyPatch kernel-arm64.patch -R
%else
#  solved with SPCR in future
%endif
%endif

# END OF PATCH APPLICATIONS

%endif

# Any further pre-build tree manipulations happen here.

chmod +x scripts/checkpatch.pl

# This Prevents scripts/setlocalversion from mucking with our version numbers.
touch .scmversion

# only deal with configs if we are going to build for the arch
%ifnarch %nobuildarches

mkdir configs

%if !%{debugbuildsenabled}
rm -f kernel-%{version}-*debug.config
%endif

%define make make %{?cross_opts}

# now run oldconfig over all the config files
for i in *.config
do
  mv $i .config
  Arch=`head -1 .config | cut -b 3-`
  make ARCH=$Arch listnewconfig | grep -E '^CONFIG_' >.newoptions || true
%if %{listnewconfig_fail}
  if [ -s .newoptions ]; then
    cat .newoptions
    exit 1
  fi
%endif
  rm -f .newoptions
  make ARCH=$Arch oldnoconfig
  echo "# $Arch" > configs/$i
  cat .config >> configs/$i
done
# end of kernel config
%endif


%endif

# Loongson3 (mips64el)
#
%ifarch %{mips}

%define make make %{?cross_opts}
sed -i '/LOONGSON3/s@+=@& -Wa,-mno-fix-loongson3-llsc@g' arch/mips/Makefile

%endif

# get rid of unwanted files resulting from patch fuzz
find . \( -name "*.orig" -o -name "*~" \) -exec rm -f {} \; >/dev/null

# remove unnecessary SCM files
find . -name .gitignore -exec rm -f {} \; >/dev/null

cd ..

###
### build
###
%build

%if %{with_sparse}
%define sparse_mflags	C=1
%endif

%if %{with_debuginfo}
# This override tweaks the kernel makefiles so that we run debugedit on an
# object before embedding it.  When we later run find-debuginfo.sh, it will
# run debugedit again.  The edits it does change the build ID bits embedded
# in the stripped object, but repeating debugedit is a no-op.  We do it
# beforehand to get the proper final build ID bits into the embedded image.
# This affects the vDSO images in vmlinux, and the vmlinux image in bzImage.
export AFTER_LINK=\
'sh -xc "/usr/lib/rpm/debugedit -b $$RPM_BUILD_DIR -d /usr/src/debug \
    				-i $@ > $@.id"'
%endif

cp_vmlinux()
{
  eu-strip --remove-comment -o "$2" "$1"
}

BuildKernel() {
    MakeTarget=$1
    KernelImage=$2
    Flavour=$3
    Flav=${Flavour:++${Flavour}}
    InstallName=${4:-vmlinuz}

    # Pick the right config file for the kernel we're building
    Config=kernel-%{version}-%{_target_cpu}${Flavour:+-${Flavour}}.config
    DevelDir=/usr/src/kernels/%{KVERREL}${Flav}

    # When the bootable image is just the ELF kernel, strip it.
    # We already copy the unstripped file into the debuginfo package.
    if [ "$KernelImage" = vmlinux ]; then
      CopyKernel=cp_vmlinux
    else
      CopyKernel=cp
    fi

    KernelVer=%{version}-%{release}.%{_target_cpu}${Flav}
    echo BUILDING A KERNEL FOR ${Flavour} %{_target_cpu}...

    %if 0%{?stable_update}
    # make sure SUBLEVEL is incremented on a stable release.  Sigh 3.x.
    perl -p -i -e "s/^SUBLEVEL.*/SUBLEVEL = %{?stablerev}/" Makefile
    %endif

    # make sure EXTRAVERSION says what we want it to say
    perl -p -i -e "s/^EXTRAVERSION.*/EXTRAVERSION = -%{release}.%{_target_cpu}${Flav}/" Makefile

    # if pre-rc1 devel kernel, must fix up PATCHLEVEL for our versioning scheme
    %if !0%{?rcrev}
    %if 0%{?gitrev}
    perl -p -i -e 's/^PATCHLEVEL.*/PATCHLEVEL = %{upstream_sublevel}/' Makefile
    %endif
    %endif

    # and now to start the build process

    make -s mrproper
    %ifnarch %{mips}
    cp configs/$Config .config
    %endif

    %if %{signmodules}
    cp %{SOURCE11} .
    %endif

    chmod +x scripts/sign-file

    Arch=`head -1 .config | cut -b 3-`
    echo USING ARCH=$Arch

    %ifarch %{mips}
    Arch=mips
    make -s ARCH=$Arch loongson3_defconfig
    %else
    make -s ARCH=$Arch oldnoconfig >/dev/null
    %endif
    %{make} -s ARCH=$Arch V=1 %{?_smp_mflags} $MakeTarget %{?sparse_mflags} %{?kernel_mflags}
    %{make} -s ARCH=$Arch V=1 %{?_smp_mflags} modules %{?sparse_mflags} || exit 1

%ifarch %{arm} aarch64
    %{make} -s ARCH=$Arch V=1 dtbs
    mkdir -p $RPM_BUILD_ROOT/%{image_install_path}/dtb-$KernelVer
    install -m 644 arch/$Arch/boot/dts/*.dtb $RPM_BUILD_ROOT/%{image_install_path}/dtb-$KernelVer/
    rm -f arch/$Arch/boot/dts/*.dtb
%endif

    # Start installing the results
%if %{with_debuginfo}
    mkdir -p $RPM_BUILD_ROOT%{debuginfodir}/boot
    mkdir -p $RPM_BUILD_ROOT%{debuginfodir}/%{image_install_path}
%endif
    mkdir -p $RPM_BUILD_ROOT/%{image_install_path}
    install -m 644 .config $RPM_BUILD_ROOT/boot/config-$KernelVer
    install -m 644 System.map $RPM_BUILD_ROOT/boot/System.map-$KernelVer

    # We estimate the size of the initramfs because rpm needs to take this size
    # into consideration when performing disk space calculations. (See bz #530778)
    dd if=/dev/zero of=$RPM_BUILD_ROOT/boot/initramfs-$KernelVer.img bs=1M count=20

    if [ -f arch/$Arch/boot/zImage.stub ]; then
      cp arch/$Arch/boot/zImage.stub $RPM_BUILD_ROOT/%{image_install_path}/zImage.stub-$KernelVer || :
    fi
    %if %{signmodules}
    # Sign the image if we're using EFI
    %pesign -s -i $KernelImage -o vmlinuz.signed
    if [ ! -s vmlinuz.signed ]; then
        echo "pesigning failed"
        exit 1
    fi
    mv vmlinuz.signed $KernelImage
    %endif
    $CopyKernel $KernelImage \
    		$RPM_BUILD_ROOT/%{image_install_path}/$InstallName-$KernelVer
    chmod 755 $RPM_BUILD_ROOT/%{image_install_path}/$InstallName-$KernelVer

    # hmac sign the kernel for FIPS
    echo "Creating hmac file: $RPM_BUILD_ROOT/%{image_install_path}/.vmlinuz-$KernelVer.hmac"
    ls -l $RPM_BUILD_ROOT/%{image_install_path}/$InstallName-$KernelVer
    sha512hmac $RPM_BUILD_ROOT/%{image_install_path}/$InstallName-$KernelVer | sed -e "s,$RPM_BUILD_ROOT,," > $RPM_BUILD_ROOT/%{image_install_path}/.vmlinuz-$KernelVer.hmac;

    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer
    # Override $(mod-fw) because we don't want it to install any firmware
    # we'll get it from the linux-firmware package and we don't want conflicts
    %{make} -s ARCH=$Arch INSTALL_MOD_PATH=$RPM_BUILD_ROOT modules_install KERNELRELEASE=$KernelVer mod-fw=

%ifarch %{vdso_arches}
    %{make} -s ARCH=$Arch INSTALL_MOD_PATH=$RPM_BUILD_ROOT vdso_install KERNELRELEASE=$KernelVer
    if [ ! -s ldconfig-kernel.conf ]; then
      echo > ldconfig-kernel.conf "\
# Placeholder file, no vDSO hwcap entries used in this kernel."
    fi
    %{__install} -D -m 444 ldconfig-kernel.conf \
        $RPM_BUILD_ROOT/etc/ld.so.conf.d/kernel-$KernelVer.conf
    rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/vdso/.build-id
%endif

    # And save the headers/makefiles etc for building modules against
    #
    # This all looks scary, but the end result is supposed to be:
    # * all arch relevant include/ files
    # * all Makefile/Kconfig files
    # * all script/ files

    rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/source
    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    (cd $RPM_BUILD_ROOT/lib/modules/$KernelVer ; ln -s build source)
    # dirs for additional modules per module-init-tools, kbuild/modules.txt
    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/extra
    mkdir -p $RPM_BUILD_ROOT/lib/modules/$KernelVer/updates
    # first copy everything
    cp --parents `find  -type f -name "Makefile*" -o -name "Kconfig*"` $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    cp Module.symvers $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    cp System.map $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    if [ -s Module.markers ]; then
      cp Module.markers $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    fi
    # then drop all but the needed Makefiles/Kconfig files
    rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/Documentation
    rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts
    rm -rf $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include
    cp .config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    cp -a scripts $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    if [ -d arch/$Arch/scripts ]; then
      cp -a arch/$Arch/scripts $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/%{_arch} || :
    fi
    if [ -f arch/$Arch/*lds ]; then
      cp -a arch/$Arch/*lds $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/arch/%{_arch}/ || :
    fi
    rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts/*.o
    rm -f $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/scripts/*/*.o
%ifarch %{power64}
    cp -a --parents arch/powerpc/lib/crtsavres.[So] $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
%endif
    if [ -d arch/%{asmarch}/include ]; then
      cp -a --parents arch/%{asmarch}/include $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
    fi
%ifarch aarch64
    # arch/arm64/include/asm/xen references arch/arm
    cp -a --parents arch/arm/include/asm/xen $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
%endif
    # include the machine specific headers for ARM variants, if available.
%ifarch %{arm}
    if [ -d arch/%{asmarch}/mach-${Flavour}/include ]; then
      cp -a --parents arch/%{asmarch}/mach-${Flavour}/include $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
    fi
%endif
%ifarch %{mips}
	cp -a --parents arch/mips/Kbuild.platforms $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/
    cp --parents `find arch/mips/ -type f -name "Platform"` $RPM_BUILD_ROOT/lib/modules/$KernelVer/build
    cp vmlinux $RPM_BUILD_ROOT/%{image_install_path}/vmlinux_$KernelVer
%endif
    cp -a include $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include

    # Make sure the Makefile and version.h have a matching timestamp so that
    # external modules can be built
    touch -r $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/Makefile $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/generated/uapi/linux/version.h

    # Copy .config to include/config/auto.conf so "make prepare" is unnecessary.
    cp $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/.config $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/include/config/auto.conf

%if %{with_debuginfo}
    if test -s vmlinux.id; then
      cp vmlinux.id $RPM_BUILD_ROOT/lib/modules/$KernelVer/build/vmlinux.id
    else
      echo >&2 "*** ERROR *** no vmlinux build ID! ***"
      exit 1
    fi

    #
    # save the vmlinux file for kernel debugging into the kernel-debuginfo rpm
    #
    mkdir -p $RPM_BUILD_ROOT%{debuginfodir}/lib/modules/$KernelVer
    cp vmlinux $RPM_BUILD_ROOT%{debuginfodir}/lib/modules/$KernelVer
%endif

    find $RPM_BUILD_ROOT/lib/modules/$KernelVer -name "*.ko" -type f >modnames

    # mark modules executable so that strip-to-file can strip them
    xargs --no-run-if-empty chmod u+x < modnames

    # Generate a list of modules for block and networking.

    grep -F /drivers/ modnames | xargs --no-run-if-empty nm -upA |
    sed -n 's,^.*/\([^/]*\.ko\):  *U \(.*\)$,\1 \2,p' > drivers.undef

    collect_modules_list()
    {
      sed -r -n -e "s/^([^ ]+) \\.?($2)\$/\\1/p" drivers.undef |
        LC_ALL=C sort -u > $RPM_BUILD_ROOT/lib/modules/$KernelVer/modules.$1
      if [ ! -z "$3" ]; then
        sed -r -e "/^($3)\$/d" -i $RPM_BUILD_ROOT/lib/modules/$KernelVer/modules.$1
      fi
    }

    collect_modules_list networking \
    			 'register_netdev|ieee80211_register_hw|usbnet_probe|phy_driver_register|rt(l_|2x00)(pci|usb)_probe|register_netdevice'
    collect_modules_list block \
    			 'ata_scsi_ioctl|scsi_add_host|scsi_add_host_with_dma|blk_alloc_queue|blk_init_queue|register_mtd_blktrans|scsi_esp_register|scsi_register_device_handler|blk_queue_physical_block_size' 'pktcdvd.ko|dm-mod.ko'
    collect_modules_list drm \
    			 'drm_open|drm_init'
    collect_modules_list modesetting \
    			 'drm_crtc_init'

    # detect missing or incorrect license tags
    ( find $RPM_BUILD_ROOT/lib/modules/$KernelVer -name '*.ko' | xargs /sbin/modinfo -l | \
        grep -E -v 'GPL( v2)?$|Dual BSD/GPL$|Dual MPL/GPL$|GPL and additional rights$' ) && exit 1

    # remove files that will be auto generated by depmod at rpm -i time
    pushd $RPM_BUILD_ROOT/lib/modules/$KernelVer/
        rm -f modules.{alias*,builtin.bin,dep*,*map,symbols*,devname,softdep}
    popd

    # Call the modules-extra script to move things around
    %{SOURCE17} $RPM_BUILD_ROOT/lib/modules/$KernelVer %{SOURCE16}

    #
    # Generate the kernel-core and kernel-modules files lists
    #

    # Copy the System.map file for depmod to use, and create a backup of the
    # full module tree so we can restore it after we're done filtering
    cp System.map $RPM_BUILD_ROOT/.
    pushd $RPM_BUILD_ROOT
    mkdir restore
    cp -r lib/modules/$KernelVer/* restore/.

    # don't include anything going into k-m-e in the file lists
    rm -rf lib/modules/$KernelVer/extra

    # Find all the module files and filter them out into the core and modules
    # lists.  This actually removes anything going into -modules from the dir.
    find lib/modules/$KernelVer/kernel -name *.ko | sort -n > modules.list
	cp $RPM_SOURCE_DIR/filter-*.sh .
    %{SOURCE99} modules.list %{_target_cpu}
	rm filter-*.sh

    # Run depmod on the resulting module tree and make sure it isn't broken
    depmod -b . -aeF ./System.map $KernelVer &> depmod.out
    if [ -s depmod.out ]; then
        echo "Depmod failure"
        cat depmod.out
        exit 1
    else
        rm depmod.out
    fi
    # remove files that will be auto generated by depmod at rpm -i time
    pushd $RPM_BUILD_ROOT/lib/modules/$KernelVer/
        rm -f modules.{alias*,builtin.bin,dep*,*map,symbols*,devname,softdep}
    popd

    # Go back and find all of the various directories in the tree.  We use this
    # for the dir lists in kernel-core
    find lib/modules/$KernelVer/kernel -type d | sort -n > module-dirs.list

    # Cleanup
    rm System.map
    cp -r restore/* lib/modules/$KernelVer/.
    rm -rf restore
    popd

    # Make sure the files lists start with absolute paths or rpmbuild fails.
    # Also add in the dir entries
    sed -e 's/^lib*/\/lib/' %{?zipsed} $RPM_BUILD_ROOT/k-d.list > ../kernel${Flavour:+-${Flavour}}-modules.list
    sed -e 's/^lib*/%dir \/lib/' %{?zipsed} $RPM_BUILD_ROOT/module-dirs.list > ../kernel${Flavour:+-${Flavour}}-core.list
    sed -e 's/^lib*/\/lib/' %{?zipsed} $RPM_BUILD_ROOT/modules.list >> ../kernel${Flavour:+-${Flavour}}-core.list

    # Cleanup
    rm -f $RPM_BUILD_ROOT/k-d.list
    rm -f $RPM_BUILD_ROOT/modules.list
    rm -f $RPM_BUILD_ROOT/module-dirs.list

%if %{signmodules}
    # Save the signing keys so we can sign the modules in __modsign_install_post
    cp signing_key.priv signing_key.priv.sign${Flav}
    cp signing_key.x509 signing_key.x509.sign${Flav}
%endif

    # Move the devel headers out of the root file system
    mkdir -p $RPM_BUILD_ROOT/usr/src/kernels
    mv $RPM_BUILD_ROOT/lib/modules/$KernelVer/build $RPM_BUILD_ROOT/$DevelDir

    # This is going to create a broken link during the build, but we don't use
    # it after this point.  We need the link to actually point to something
    # when kernel-devel is installed, and a relative link doesn't work across
    # the F17 UsrMove feature.
    ln -sf $DevelDir $RPM_BUILD_ROOT/lib/modules/$KernelVer/build

    # prune junk from kernel-devel
    find $RPM_BUILD_ROOT/usr/src/kernels -name ".*.cmd" -exec rm -f {} \;
}

###
# DO it...
###

# prepare directories
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/boot
mkdir -p $RPM_BUILD_ROOT%{_libexecdir}

cd linux-%{KVERREL}

%if %{with_debug}
BuildKernel %make_target %kernel_image debug
%endif

%if %{with_pae_debug}
BuildKernel %make_target %kernel_image %{pae}debug
%endif

%if %{with_pae}
BuildKernel %make_target %kernel_image %{pae}
%endif

%if %{with_up}
BuildKernel %make_target %kernel_image
%endif

%global perf_make \
  LD='ld -m elf64ltsmip' make -s %{?cross_opts} %{?_smp_mflags} -C tools/perf V=1 WERROR=0 NO_LIBUNWIND=1 HAVE_CPLUS_DEMANGLE=1 NO_GTK2=1 NO_LIBNUMA=1 NO_STRLCPY=1 NO_BIONIC=1 prefix=%{_prefix}
%if %{with_perf}
# perf
# sed -i 's@$(LD) -r@$(LD) --oformat elf64-tradlittlemips -r@g' tools/build/Makefile.build
%{perf_make} DESTDIR=$RPM_BUILD_ROOT all
%endif

%if %{with_tools}
%ifarch %{cpupowerarchs}
# cpupower
# make sure version-gen.sh is executable.
chmod +x tools/power/cpupower/utils/version-gen.sh
%{make} %{?_smp_mflags} -C tools/power/cpupower CPUFREQ_BENCH=false
%ifarch %{ix86}
    pushd tools/power/cpupower/debug/i386
    %{make} %{?_smp_mflags} centrino-decode powernow-k8-decode
    popd
%endif
%ifarch x86_64
    pushd tools/power/cpupower/debug/x86_64
    %{make} %{?_smp_mflags} centrino-decode powernow-k8-decode
    popd
%endif
%ifarch %{ix86} x86_64
   pushd tools/power/x86/x86_energy_perf_policy/
   %{make}
   popd
   pushd tools/power/x86/turbostat
   %{make}
   popd
%endif #turbostat/x86_energy_perf_policy
%endif
%ifarch  %{rpmversion} >= 4.1
pushd tools/thermal/tmon/
%{make}
popd
%endif
%endif

# In the modsign case, we do 3 things.  1) We check the "flavour" and hard
# code the value in the following invocations.  This is somewhat sub-optimal
# but we're doing this inside of an RPM macro and it isn't as easy as it
# could be because of that.  2) We restore the .tmp_versions/ directory from
# the one we saved off in BuildKernel above.  This is to make sure we're
# signing the modules we actually built/installed in that flavour.  3) We
# grab the arch and invoke mod-sign.sh command to actually sign the modules.
#
# We have to do all of those things _after_ find-debuginfo runs, otherwise
# that will strip the signature off of the modules.

%define __modsign_install_post \
  if [ "%{signmodules}" -eq "1" ]; then \
    if [ "%{with_pae}" -ne "0" ]; then \
      %{modsign_cmd} signing_key.priv.sign+%{pae} signing_key.x509.sign+%{pae} $RPM_BUILD_ROOT/lib/modules/%{KVERREL}+%{pae}/ \
    fi \
    if [ "%{with_debug}" -ne "0" ]; then \
      %{modsign_cmd} signing_key.priv.sign+debug signing_key.x509.sign+debug $RPM_BUILD_ROOT/lib/modules/%{KVERREL}+debug/ \
    fi \
    if [ "%{with_pae_debug}" -ne "0" ]; then \
      %{modsign_cmd} signing_key.priv.sign+%{pae}debug signing_key.x509.sign+%{pae}debug $RPM_BUILD_ROOT/lib/modules/%{KVERREL}+%{pae}debug/ \
    fi \
    if [ "%{with_up}" -ne "0" ]; then \
      %{modsign_cmd} signing_key.priv.sign signing_key.x509.sign $RPM_BUILD_ROOT/lib/modules/%{KVERREL}/ \
    fi \
  fi \
  if [ "%{zipmodules}" -eq "1" ]; then \
    find $RPM_BUILD_ROOT/lib/modules/ -type f -name '*.ko' | xargs xz; \
  fi \
%{nil}

###
### Special hacks for debuginfo subpackages.
###

# This macro is used by %%install, so we must redefine it before that.
%define debug_package %{nil}

%if %{with_debuginfo}

%define __debug_install_post \
  /usr/lib/rpm/find-debuginfo.sh %{debuginfo_args} %{_builddir}/%{?buildsubdir}\
%{nil}

%ifnarch noarch
%global __debug_package 1
%files -f debugfiles.list debuginfo-common-%{_target_cpu}
%defattr(-,root,root)
%endif

%endif

#
# Disgusting hack alert! We need to ensure we sign modules *after* all
# invocations of strip occur, which is in __debug_install_post if
# find-debuginfo.sh runs, and __os_install_post if not.
#
%define __spec_install_post \
  %{?__debug_package:%{__debug_install_post}}\
  %{__arch_install_post}\
  %{__os_install_post}\
  %{__modsign_install_post}

###
### install
###

%install

cd linux-%{KVERREL}

# We have to do the headers install before the tools install because the
# kernel headers_install will remove any header files in /usr/include that
# it doesn't install itself.

%if %{with_headers}
# Install kernel headers
make ARCH=%{hdrarch} INSTALL_HDR_PATH=$RPM_BUILD_ROOT/usr headers_install

find $RPM_BUILD_ROOT/usr/include \
     \( -name .install -o -name .check -o \
     	-name ..install.cmd -o -name ..check.cmd \) | xargs rm -f

%endif

%if %{with_perf}
# perf tool binary and supporting scripts/binaries
%{perf_make} DESTDIR=$RPM_BUILD_ROOT lib=%{_lib} install
# remove the 'trace' symlink.
rm -f %{buildroot}%{_bindir}/trace

# python-perf extension
%{perf_make} DESTDIR=$RPM_BUILD_ROOT install-python_ext

# perf man pages (note: implicit rpm magic compresses them later)
mkdir -p %{buildroot}/%{_mandir}/man1
pushd %{buildroot}/%{_mandir}/man1
tar -xf %{SOURCE10}
popd
%endif

%if %{with_tools}
%ifarch %{cpupowerarchs}
%{make} -C tools/power/cpupower DESTDIR=$RPM_BUILD_ROOT libdir=%{_libdir} mandir=%{_mandir} CPUFREQ_BENCH=false install
rm -f %{buildroot}%{_libdir}/*.{a,la}
%find_lang cpupower
mv cpupower.lang ../
%ifarch %{ix86}
    pushd tools/power/cpupower/debug/i386
    install -m755 centrino-decode %{buildroot}%{_bindir}/centrino-decode
    install -m755 powernow-k8-decode %{buildroot}%{_bindir}/powernow-k8-decode
    popd
%endif
%ifarch x86_64
    pushd tools/power/cpupower/debug/x86_64
    install -m755 centrino-decode %{buildroot}%{_bindir}/centrino-decode
    install -m755 powernow-k8-decode %{buildroot}%{_bindir}/powernow-k8-decode
    popd
%endif
chmod 0755 %{buildroot}%{_libdir}/libcpupower.so*
mkdir -p %{buildroot}%{_unitdir} %{buildroot}%{_sysconfdir}/sysconfig
install -m644 %{SOURCE2000} %{buildroot}%{_unitdir}/cpupower.service
install -m644 %{SOURCE2001} %{buildroot}%{_sysconfdir}/sysconfig/cpupower
%endif
%ifarch %{ix86} x86_64
   mkdir -p %{buildroot}%{_mandir}/man8
   pushd tools/power/x86/x86_energy_perf_policy
   make DESTDIR=%{buildroot} install
   popd
   pushd tools/power/x86/turbostat
   make DESTDIR=%{buildroot} install
   popd
%endif #turbostat/x86_energy_perf_policy
%ifarch %{rpmversion} >= 4.1
pushd tools/thermal/tmon
make INSTALL_ROOT=%{buildroot} install
popd
%endif
%endif

%if %{with_bootwrapper}
make DESTDIR=$RPM_BUILD_ROOT bootwrapper_install WRAPPER_OBJDIR=%{_libdir}/kernel-wrapper WRAPPER_DTSDIR=%{_libdir}/kernel-wrapper/dts
%endif


###
### clean
###

%clean
rm -rf $RPM_BUILD_ROOT

###
### scripts
###
%global TIME $(date +"%Y%m%d%H%M")

%if %{with_tools}
%post -n kernel-tools
/sbin/ldconfig

%postun -n kernel-tools
/sbin/ldconfig
%endif

%pre -n kernel-core
#backup boot conf when kernel changes
if [ -f "/.buildstamp" ];then
	PMON_CFG="/boot/boot.cfg"
	GRUB2_CFG=$(readlink -f /etc/grub.conf 2>/dev/null)
	[ -f "$GRUB2_CFG" ] || GRUB2_CFG=$(readlink -f /etc/grub2-efi.cfg 2>/dev/null)
	[ -f "$GRUB2_CFG" ] && cp $GRUB2_CFG /boot/.grub.cfg.donotremovethisfile.old
	[ -f "$PMON_CFG" ] && cp $PMON_CFG /boot/.boot.cfg.donotremovethisfile.old
fi

#
# This macro defines a %%post script for a kernel*-devel package.
#	%%kernel_devel_post [<subpackage>]
#
%define kernel_devel_post() \
%{expand:%%post %{?1:%{1}-}devel}\
if [ -f /etc/sysconfig/kernel ]\
then\
    . /etc/sysconfig/kernel || exit $?\
fi\
if [ "$HARDLINK" != "no" -a -x /usr/sbin/hardlink ]\
then\
    (cd /usr/src/kernels/%{KVERREL}%{?1:+%{1}} &&\
     /usr/bin/find . -type f | while read f; do\
       hardlink -c /usr/src/kernels/*.fc*.*/$f $f\
     done)\
fi\
%{nil}

#
# This macro defines a %%post script for a kernel*-modules-extra package.
# It also defines a %%postun script that does the same thing.
#	%%kernel_modules_extra_post [<subpackage>]
#
%define kernel_modules_extra_post() \
%{expand:%%post %{?1:%{1}-}modules-extra}\
/sbin/depmod -a %{KVERREL}%{?1:+%{1}}\
%{nil}\
%{expand:%%postun %{?1:%{1}-}modules-extra}\
/sbin/depmod -a %{KVERREL}%{?1:+%{1}}\
%{nil}

#
# This macro defines a %%post script for a kernel*-modules package.
# It also defines a %%postun script that does the same thing.
#	%%kernel_modules_post [<subpackage>]
#
%define kernel_modules_post() \
%{expand:%%post %{?1:%{1}-}modules}\
/sbin/depmod -a %{KVERREL}%{?1:+%{1}}\
%{nil}\
%{expand:%%postun %{?1:%{1}-}modules}\
/sbin/depmod -a %{KVERREL}%{?1:+%{1}}\
%{nil}

# This macro defines a %%posttrans script for a kernel package.
#	%%kernel_variant_posttrans [<subpackage>]
# More text can follow to go at the end of this variant's %%post.
#
%define kernel_variant_posttrans() \
%{expand:%%posttrans %{?1:%{1}-}core}\
/bin/kernel-install add %{KVERREL}%{?1:+%{1}} /%{image_install_path}/vmlinuz-%{KVERREL}%{?1:+%{1}} || exit $?\
if [ -f "/.buildstamp" ];then\
	PMON_CFG="/boot/boot.cfg"\
	GRUB2_CFG=$(readlink -f /etc/grub.conf 2>/dev/null)\
	[ -f "$GRUB2_CFG" ] || GRUB2_CFG=$(readlink -f /etc/grub2-efi.cfg 2>/dev/null)\
	if [ -f "$GRUB2_CFG" ];then\
		if [ -f "/boot/.grub.cfg.donotremovethisfile.old" ];then\
			echo "-----------开始备份安装前开机启动配置文件-----------"\
			mv /boot/.grub.cfg.donotremovethisfile.old $GRUB2_CFG.%{TIME}.old && echo "-----------完成($GRUB2_CFG.%{TIME}.old)------------"\
			[ -f "$PMON_CFG" ] && mv /boot/.boot.cfg.donotremovethisfile.old $PMON_CFG.%{TIME}.old && echo "-----------完成($PMON_CFG.%{TIME}.old)------------"\
		fi\
		echo "-----------开始备份安装后开机启动配置文件-----------"\
		cp $GRUB2_CFG $GRUB2_CFG.%{TIME}.new && echo "-----------完成($GRUB2_CFG.%{TIME}.new)------------"\
		[ -f "$PMON_CFG" ] && cp $PMON_CFG $PMON_CFG.%{TIME}.new && echo "-----------完成($PMON_CFG.%{TIME}.new)------------"\
	elif [ -f "$PMON_CFG" ];then\
		echo "-----------缺少配置文件($GRUB2_CFG)------------"\
	else\
		echo "-----------缺少配置文件($GRUB2_CFG)------------"\
		echo "-----------缺少配置文件($PMON_CFG)------------"\
	fi\
fi\
%{nil}

#
# This macro defines a %%post script for a kernel package and its devel package.
#	%%kernel_variant_post [-v <subpackage>] [-r <replace>]
# More text can follow to go at the end of this variant's %%post.
#
%define kernel_variant_post(v:r:) \
%{expand:%%kernel_devel_post %{?-v*}}\
%{expand:%%kernel_modules_post %{?-v*}}\
%{expand:%%kernel_modules_extra_post %{?-v*}}\
%{expand:%%kernel_variant_posttrans %{?-v*}}\
%{expand:%%post %{?-v*:%{-v*}-}core}\
%{-r:\
if [ `uname -i` == "x86_64" -o `uname -i` == "i386" ] &&\
   [ -f /etc/sysconfig/kernel ]; then\
  /bin/sed -r -i -e 's/^DEFAULTKERNEL=%{-r*}$/DEFAULTKERNEL=kernel%{?-v:-%{-v*}}/' /etc/sysconfig/kernel || exit $?\
fi}\
%{nil}

#
# This macro defines a %%preun script for a kernel package.
#	%%kernel_variant_preun <subpackage>
#
%define kernel_variant_preun() \
%{expand:%%preun %{?1:%{1}-}core}\
PMON_CFG="/boot/boot.cfg"\
GRUB2_CFG=$(readlink -f /etc/grub.conf 2>/dev/null)\
[ -f "$GRUB2_CFG" ] || GRUB2_CFG=$(readlink -f /etc/grub2-efi.cfg 2>/dev/null)\
if [ -f "$GRUB2_CFG" ];then\
	if [ ! -f "/boot/.grub.cfg.donotremovethisfile.old" ];then\
		echo "-----------开始备份卸载前开机启动配置文件-----------"\
		cp $GRUB2_CFG $GRUB2_CFG.%{TIME}.old && echo "-----------完成($GRUB2_CFG.%{TIME}.old)------------"\
		[ -f "$PMON_CFG" ] && cp $PMON_CFG $PMON_CFG.%{TIME}.old && echo "-----------完成($PMON_CFG.%{TIME}.old)------------"\
	fi\
elif [ -f "$PMON_CFG" ];then\
		echo "-----------缺少配置文件($GRUB2_CFG)------------"\
else\
	echo "-----------缺少配置文件($GRUB2_CFG)------------"\
	echo "-----------缺少配置文件($PMON_CFG)------------"\
fi\
/bin/kernel-install remove %{KVERREL}%{?1:+%{1}} /%{image_install_path}/vmlinuz-%{KVERREL}%{?1:+%{1}} || exit $?\
if [ -f "$GRUB2_CFG" ];then\
	if [ ! -f "/boot/.grub.cfg.donotremovethisfile.old" ];then\
		echo "-----------开始备份卸载后开机启动配置文件-----------"\
		cp $GRUB2_CFG $GRUB2_CFG.%{TIME}.new && echo "-----------完成($GRUB2_CFG.%{TIME}.new)------------"\
		[ -f "$PMON_CFG" ] && cp $PMON_CFG $PMON_CFG.%{TIME}.new && echo "-----------完成($PMON_CFG.%{TIME}.new)------------"\
	fi\
fi\
%{nil}

%kernel_variant_preun
%kernel_variant_post -r kernel-smp

%kernel_variant_preun %{pae}
%kernel_variant_post -v %{pae} -r (kernel|kernel-smp)

%kernel_variant_post -v %{pae}debug -r (kernel|kernel-smp)
%kernel_variant_preun %{pae}debug

%kernel_variant_preun debug
%kernel_variant_post -v debug

if [ -x /sbin/ldconfig ]
then
    /sbin/ldconfig -X || exit $?
fi

###
### file lists
###

%if %{with_headers}
%files headers
%defattr(-,root,root)
/usr/include/*
%endif

%if %{with_bootwrapper}
%files bootwrapper
%defattr(-,root,root)
/usr/sbin/*
%{_libdir}/kernel-wrapper
%endif

%if %{with_perf}
%files -n perf
%defattr(-,root,root)
%{_bindir}/perf
%ifarch  %{rpmversion} >= 4.1
%dir %{_libdir}/traceevent/plugins
%{_libdir}/traceevent/plugins/*
%endif
%dir %{_libexecdir}/perf-core
%{_libexecdir}/perf-core/*
%{_libdir}/traceevent
%{_mandir}/man[1-8]/perf*
%{_sysconfdir}/bash_completion.d/perf
%{_datadir}/perf-core/strace/groups
%{_datadir}/doc/perf-tip/tips.txt
%doc linux-%{KVERREL}/tools/perf/Documentation/examples.txt

%files -n python-perf
%defattr(-,root,root)
%{python_sitearch}

%if %{with_debuginfo}
%files -f perf-debuginfo.list -n perf-debuginfo
%defattr(-,root,root)

%files -f python-perf-debuginfo.list -n python-perf-debuginfo
%defattr(-,root,root)
%endif
%endif # with_perf

%if %{with_tools}
%ifarch %{cpupowerarchs}
%files -n kernel-tools -f cpupower.lang
%defattr(-,root,root)
%{_bindir}/cpupower
%ifarch %{ix86} x86_64
%{_bindir}/centrino-decode
%{_bindir}/powernow-k8-decode
%endif
%{_unitdir}/cpupower.service
%{_mandir}/man[1-8]/cpupower*
%config(noreplace) %{_sysconfdir}/sysconfig/cpupower
%ifarch %{ix86} x86_64
%{_bindir}/x86_energy_perf_policy
%{_mandir}/man8/x86_energy_perf_policy*
%{_bindir}/turbostat
%{_mandir}/man8/turbostat*
%endif
%ifarch %{rpmversion} >= 4.1
%{_bindir}/tmon
%endif
%endif

%if %{with_debuginfo}
%files -f kernel-tools-debuginfo.list -n kernel-tools-debuginfo
%defattr(-,root,root)
%endif

%ifarch %{cpupowerarchs}
%files -n kernel-tools-libs
%{_libdir}/libcpupower.so.0
%{_libdir}/libcpupower.so.0.0.0

%files -n kernel-tools-libs-devel
%{_libdir}/libcpupower.so
%{_includedir}/cpufreq.h
%endif
%endif # with_perf

# empty meta-package
%files
%defattr(-,root,root)

# This is %%{image_install_path} on an arch where that includes ELF files,
# or empty otherwise.
%define elf_image_install_path %{?kernel_image_elf:%{image_install_path}}

#
# This macro defines the %%files sections for a kernel package
# and its devel and debuginfo packages.
#	%%kernel_variant_files [-k vmlinux] <condition> <subpackage>
#
%define kernel_variant_files(k:) \
%if %{1}\
%{expand:%%files -f kernel-%{?2:%{2}-}core.list %{?2:%{2}-}core}\
%defattr(-,root,root)\
%{!?_licensedir:%global license %%doc}\
%license linux-%{KVERREL}/COPYING\
/%{image_install_path}/%{?-k:%{-k*}}%{!?-k:vmlinuz}-%{KVERREL}%{?2:+%{2}}\
/%{image_install_path}/.vmlinuz-%{KVERREL}%{?2:+%{2}}.hmac \
%ifarch %{arm} aarch64\
/%{image_install_path}/dtb-%{KVERREL}%{?2:+%{2}} \
%endif\
%ifarch %{mips}\
/%{image_install_path}/%{?-k:%{-k*}}%{!?-k:vmlinux}_%{KVERREL}%{?2:+%{2}}\
%endif\
%attr(600,root,root) /boot/System.map-%{KVERREL}%{?2:+%{2}}\
/boot/config-%{KVERREL}%{?2:+%{2}}\
%ghost /boot/initramfs-%{KVERREL}%{?2:+%{2}}.img\
%dir /lib/modules\
%dir /lib/modules/%{KVERREL}%{?2:+%{2}}\
%dir /lib/modules/%{KVERREL}%{?2:+%{2}}/kernel\
/lib/modules/%{KVERREL}%{?2:+%{2}}/build\
/lib/modules/%{KVERREL}%{?2:+%{2}}/source\
/lib/modules/%{KVERREL}%{?2:+%{2}}/updates\
%ifarch %{vdso_arches}\
/lib/modules/%{KVERREL}%{?2:+%{2}}/vdso\
/etc/ld.so.conf.d/kernel-%{KVERREL}%{?2:+%{2}}.conf\
%endif\
/lib/modules/%{KVERREL}%{?2:+%{2}}/modules.*\
%{expand:%%files -f kernel-%{?2:%{2}-}modules.list %{?2:%{2}-}modules}\
%defattr(-,root,root)\
%{expand:%%files %{?2:%{2}-}devel}\
%defattr(-,root,root)\
/usr/src/kernels/%{KVERREL}%{?2:+%{2}}\
%{expand:%%files %{?2:%{2}-}modules-extra}\
%defattr(-,root,root)\
/lib/modules/%{KVERREL}%{?2:+%{2}}/extra\
%if %{with_debuginfo}\
%ifnarch noarch\
%{expand:%%files -f debuginfo%{?2}.list %{?2:%{2}-}debuginfo}\
%defattr(-,root,root)\
%endif\
%endif\
%if %{?2:1} %{!?2:0}\
%{expand:%%files %{2}}\
%defattr(-,root,root)\
%endif\
%endif\
%{nil}


%kernel_variant_files %{with_up}
%kernel_variant_files %{with_debug} debug
%kernel_variant_files %{with_pae} %{pae}
%kernel_variant_files %{with_pae_debug} %{pae}debug

# plz don't put in a version string unless you're going to tag
# and build.
#
# 
#                        ___________________________________________________________
#                       / This branch is for Fedora 21. You probably want to commit \
#  _____ ____  _        \ to the F-20 branch instead, or in addition to this one.   /
# |  ___|___ \/ |        -----------------------------------------------------------
# | |_    __) | |             \   ^__^
# |  _|  / __/| |              \  (@@)\_______
# |_|   |_____|_|                 (__)\       )\/\
#                                    ||----w |
#                                    ||     ||
%changelog
* Thu Mar 5 2020 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-23.8
- sync from gerrit:
- commit 43670ff85d47cc0d77e0ded91a6352f066503370
- Author: Juxin Gao <gaojuxin@loongson.cn>
- Date:   Thu Mar 5 16:25:17 2020 +0800
- Loongson-3: Enable CONFIG_NET_VENDOR_MELLANOX for Mellanox devices.
- Change-Id: I398420b1e2f85a1156d249e53e641f2189dc1340
- Signed-off-by: Juxin Gao <gaojuxin@loongson.cn>

* Thu Mar 5 2020 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-23.7
- sync from gerrit:
- commit 4122407020c4cb8c009678925aa30bb3c14f2f0a
- Merge: cbd88d7 60df01a
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Thu Mar 5 11:58:59 2020 +0800
- Merge "Revert "drm/loongson:Added pageflip functionality""

* Wed Mar 4 2020 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-23.6
- sync from gerrit:
- commit cbd88d7b503e11e51206dacb8572b455cdda6efd
- Merge: f9703d0 1e81fe1
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Wed Mar 4 14:09:20 2020 +0800
- Merge "drm/loongson:Added pageflip functionality"

* Tue Feb 18 2020 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-23.5
- sync from gerrit:
- commit c815abb2eea931fab9126cd67469e42243725d97
- Author: Jianmin Lv <lvjianmin@loongson.cn>
- Date:   Tue Feb 18 17:41:39 2020 +0800
- Loongson-3: Add 3nod laptop hotkey support
- Change-Id: I102f1877264329856384c241e608353b0fd22ba6
- Signed-off-by: Jianmin Lv <lvjianmin@loongson.cn>
- Signed-off-by: lvyanbing <lvyanbing@loongson.cn>

* Mon Jan 20 2020 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-23.4
- sync from gerrit:
- commit 0d32dcc8422b8c070a35e4943e4b61f85bd027b6
- Merge: 1acf645 ff6b743
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Mon Jan 20 17:43:08 2020 +0800
- Merge "MIPS/loongson2k: fix compile error with gcc 4.8.5"

* Fri Jan 17 2020 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-23.3
- sync from gerrit:
- commit 129fad527b07adf9b20a509c78d1cb4f5f689b5d
- Merge: 36ccab3 a7ccac5
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Tue Jan 14 17:20:58 2020 +0800
- Merge "Loongson-3:LS7A: Loongson GPU support lvds/eDP plane backlight device"

* Thu Jan 16 2020 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-23.2
- rebuild with specific kernel config file same as gerrit provides.

* Thu Jan  9 2020 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-23.1
- sync from gerrit:
- commit 9596e204be072b79e08e7c02df97af36071a5db5
- Author: Jun Yi <yijun@loongson.cn>
- Date:   Thu Dec 12 11:56:31 2019 +0800
- MIPS: Loongson-3: 3a4000: Fixup bug of loongson_boost governor
- 1. Fixed bug of freq table function.
- Change-Id: I83d598abdf3d2b80f12f9f72cc8dcd031d2d46d7

* Mon Oct 21 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.11
- sync from gerrit:
- commit d57c1d4ef0653aa782e63eaec6d1343fb64ea8d0
- Merge: 0c00719 78d95a2
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Wed Oct 16 15:25:06 2019 +0800
- Merge "Loongson-3: 3A4000: 3a4000 cpufreq support multicores boost"

* Fri Sep 27 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.10
- sync from gerrit:
- commit 29f2fb67cbc4140f99f8845f03c4e4444881556e
- Author: Lixing <lixing@loongson.cn>
- Date:   Tue Sep 24 14:58:59 2019 +0800
- Loongson-3: LS3000/4000VZ: Disable VINT for KVM
- Disable VINT mode if set CONFIG_VIRTUALIZATION or
- CONFIG_KVM_GUEST_LS3A3000 in config file.
- Change-Id: I37a0be620c8d744b4a13e5c7882511b4fe9ba2c5
- Signed-off-by: Lixing <lixing@loongson.cn>

* Thu Sep 5 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.9
- sync from gerrit:
- commit 02366d04ddbf5f6d29fc5ea243aef1f217b45d98
- Merge: 05aecc2 d6e04a6
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Thu Sep 5 09:55:02 2019 +0800
- Merge "Longson-3: LS3000VZ: Should add random(1,0) get/put operations"

* Thu Aug  1 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.8
- sync from gerrit:
- commit 1fd6f30d6845ce00befa256110648cc828604dc3
- Merge: 38125bb 17f6072
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Fri Jul 12 13:13:48 2019 +0800
- Merge "Loongson-3: 3A4000: fix up stable timer"

* Tue Jul 23 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.7
- sync from gerrit:
- commit 860dbe51ea76be1fa9d695c736e57abc8d9f11c7
- Merge: f19d8ad 6b1c37d
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Tue Jul 23 17:04:33 2019 +0800
- Merge "Loongson-3: Export symbol fw_arg2."

* Sat Jul 13 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.6
- sync from gerrit:
- commit 1fd6f30d6845ce00befa256110648cc828604dc3
- Merge: 38125bb 17f6072
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Fri Jul 12 13:13:48 2019 +0800
- Merge "Loongson-3: 3A4000: fix up stable timer"

* Thu Jul 11 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.5
- sync from gerrit:
- commit 5a344911a97e05fe9606586cd81aa28b145f114b
- Merge: 7e4396f d58d627
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Tue Jul 9 19:15:27 2019 +0800
- Merge "Loongson-3: LS3000/4000VZ: Fix VM CPU hotplug"

* Wed Jun 26 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.4
- sync from gerrit:
- commit eba7bb6c6a54151d1d2711cd22f72a269304844e
- Author: Lixing <lixing@loongson.cn>
- Date:   Tue Jun 25 21:36:46 2019 +0800
- Loongson-3: LS4000VZ: Fix the define of VPN2_MASK for Loongson.
- Since Loongson use 48 bits VPN, the use of 32bits VPN2_MASK will
- cause memory higher than 0x100000000 can not work correctly.
- Change-Id: Id1117f1d9e8b58c332c493a03e476efdc8d5412e
- Signed-off-by: Xing Li <lixing@loongson.cn>

* Wed Jun 19 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.3
- commit 5a471fa66b1d4e9dc961ff1c6a54f9a8947faf79
- Merge: a977ca8 729b71e
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Wed Jun 19 10:17:17 2019 +0800
- Merge "Loongson-3: enable amdgpu driver default"

* Mon Jun 10 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-22.2
- sync from gerrit:
- commit df35d5ff5cde75aa06b2b53d05aca47450a13d08
- Merge: fef3887 68d0df0
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Tue Jun 4 16:49:38 2019 +0800
- Merge "Loongson-3: LS3000VZ:Fix the following questions: 1. Correct the problem that some HT registers are written incorrectly in 64-bit mode. 2. Fix the simulation of interrupt distribution."

* Fri May 31 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.19
- sync from gerrit:
- commit ac0a7e0842bf937ebfa8efd5285c0fe9c2566c64
- Merge: 2afef6c 85d9980
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Fri May 31 15:55:02 2019 +0800
- Merge "MIPS/loongson-3: Fixup arch_spin_lock error when enable CONFIG_CPU_SUPPORTS_LAMO_INSTRUCTIONS"

* Tue Apr 16 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.18
- sync from gerrit commit 51f9823d5b784b5c0115d743892e4970441f944c
- Author: maobibo <maobibo@loongson.cn>
- Date:   Mon Apr 15 17:43:22 2019 +0800
- MIPS: LS3000VZ: STLB optimization and bugfix
- during root refill process, refill failing statistic info is wrong; Now this patch fixes this.
- when power off VM, all vcpu resource should be released including stlb entry table. This patch fixes this
- guestOS issues hypercall command to update guesttlb,the hypercall flow will update host tlb by the gva, ITLB/DTLB does not need to be flushed
- modified:   arch/mips/kvm/hypcall.c
- modified:   arch/mips/kvm/ls3a3000_entry.c
- modified:   arch/mips/kvm/ls3a3000_vz.c
- Change-Id: Iad34b43466cc0f1275ac5ca2aa965f7988de0f3a

* Thu Apr 11 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.17
- sync from gerrit commit fba48af6d93852968ee082109eb51300b6320fab
- Author: Lixing <lixing@loongson.cn>
- Date:   Thu Apr 11 21:10:36 2019 +0800
- MIPS: LS3000VZ: Guest Change config file for support docker and sound card.    
- Change-Id: Id5ea8e330319878f1ca20631126b1b146986255a
- Signed-off-by: Xing Li <lixing@loongson.cn>

* Wed Apr 10 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.16
- sync from gerrit commit e1bc60c15e7b82668ef89d723896b14c5d0aabca
- Author: Juxin Gao <gaojuxin@loongson.cn>
- Date:   Thu Apr 11 11:30:24 2019 +0800
- Loongson-3: 3A4000: Add temperature enable detecton
- If temperature enable, temperature will be read from centigrade degree register，
- otherwise "There isn't any temperature" will be notice.
- Change-Id: I414a76fdc421bfa8860aeac73afd8783e3c7cd9c
- Signed-off-by: Juxin Gao <gaojuxin@loongson.cn>

* Tue Apr  9 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.15
- sync from gerrit commit 94d3e9ff4837ebf7e431109859d9948116fe923e
- Author: lvjianmin <lvjianmin@loongson.cn>
- Date:   Tue Apr 9 17:01:36 2019 +0800
- Loongson-3: Fix up build error caused by b647a4697ec198bd9815d18db57a14790b0c4202

* Fri Mar 29 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.14
- sync from gerrit commit 9f052412ba4e30ab5214a9ed793d67b1eb31012e
- Author: Lixing <lixing@loongson.cn>
- Date:   Fri Mar 29 20:26:00 2019 +0800
- MIPS:LS3000VZ: Guest remove CONFIG_EFI_PATITION selection.

* Thu Mar 28 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.13
- sync from gerrit commit 2bfa3a0796503e764797aa7a9b5787a749b229e6
- Merge: 8dd8a07 d5f71dd
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Thu Mar 28 16:55:08 2019 +0800
- Merge "Loongson-3:LS2K&7A bridge: Using mutex instead of semaphore while DC writing"

* Tue Mar 26 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.12
- commit 05c41154879a1943aba9586d8757f504a4ee2ab7
- Merge: 8c0a06b 7416f5f
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Tue Mar 26 19:31:03 2019 +0800
- Merge "loongson3: Fix compile error introduced from f160c8cbe56e1ea24531b94f8495eba426c2dabf"

* Mon Mar 25 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.11
- not changed,only version release changes to 11

* Mon Mar 25 2019 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-21.10
-sync from commit be3e2bc96bde5dec6e08ecce23416ebf1432730a
- Author: Lixing <lixing@loongson.cn>
- Date:   Mon Mar 25 11:00:50 2019 +0800
- Loongson-3: Fix build error without LOONGSON3_ENHANCEMENT configured.

* Wed Mar 13 2019 Nan xiongchao  <nanxiongchao@loongson.cn> - 3.10.84-21.9
- commit 246d9f74559abb8ba0f4620d793c7485c1191705
- Author: Huang Pei <huangpei@loongson.cn>
- Date:   Wed Mar 13 09:49:32 2019 +0800
- MIPS: fix previous commit c6627e1e3f2c1ad06d2dcab5e58860b1b967671d

* Mon Mar  11 2019 Nan xiongchao <nanxiongchao@loongson.cn> -  3.10.84-21.8
- sync from gerrit commit 990e933da662fe1435eb988a5d5c70732883e747
- Author: Huang Pei <huangpei@loongson.cn>
- Date:   Mon Mar 11 16:17:30 2019 +0800
- MIPS: enable CONFIG_MIPS_ASID_BITS_VARIABLE
- It fix warning on 3A4000 FPGA for inconsistency for asid, which is needed for compatibility for 3A4000 and pre-3A4000

* Fri Mar  8 2019 Nan xiongchao <nanxiongchao@loongson.cn> -  3.10.84-21.7
- sync from gerrit commit 9e23db12e6829569651b51fb71999f437842bdba
- Merge: bf7e75f 6b5398a
- Author: 李雪峰 <lixuefeng@loongson.cn>
- Date:   Sat Mar 9 11:38:22 2019 +0800
- Merge "tools include: import upstream f82b77462b8680b84e8cce955b05a6629cb44b36 Add mman macros needed by perf for all arch"

* Thu Mar  7 2019 Nan xiongchao <nanxiongchao@loongson.cn> -  3.10.84-21.6
- sync from gerrit commit 3617bfe5fdb5648873812a384c6fcb967e788e00
- Author: Xuefeng Li <lixuefeng@loongson.cn>
- Date:   Sat Mar 2 17:41:18 2019 +0800
- Resolve conflicts for "remainder of 7.4 point release"

* Thu Feb 28 2019 Nan xiongchao <nanxiongchao@loongson.cn> -  3.10.84-21.5
- change CONFIG_KVM=m to CONFIG_KVM=y 

* Thu Feb 28 2019 Nan xiongchao <nanxiongchao@loongson.cn> -  3.10.84-21.4
- sync from gerrit commit c7838a9278887b2744f3aa98f799c32e8eae31a7
- Author: yangyinglu <yangyinglu@loongson.cn>
- Date:   Thu Feb 28 16:35:59 2019 +0800
- Loongson-3: Disable EFI GUID Partition support

* Wed Feb 27 2019 Nan xiongchao <nanxiongchao@loongson.cn> -  3.10.84-21.3
- sync from gerrit commit ac6b4f9466ea64d28a2601793070aa28f4020ac4
- Author: nanxiongchao <nanxiongchao@loongson.cn>
- Date:   Wed Feb 27 19:28:01 2019
- Loongson-3: LS7A bridge: fix rpmbuild err
- remove unused variable "cpu"

* Wed Feb 27 2019 Nan xiongchao <nanxiongchao@loongson.cn> -  3.10.84-21.2
- commit 24511d4a2a6636259e732e23a4e4599863e1c001
- Merge: 234334b 160c854
- Author: Xuefeng <lixuefeng@loongson.cn>
- Date:   Wed Feb 27 17:33:27 2019
- Merge "MIPS/loongson2k: Fix compile error for loongson2k"

* Wed Feb 27 2019 Nan xiongchao <nanxiongchao@loongson.cn> -  3.10.84-21.1
- sync from gerrit commit 8873603bc5969ce366ed6b54e388b21e954d4011
- MIPS: LSVZ: Add cpu hotplug support
- The cpu will be plugged/unplugged use the event which be transfered between kernel and qemu by "hypercall".
- If the cpu be unplugged,the ipi interrupt of "HOTPLUG" must be send to the cpu 0.

* Tue Feb 26 2019 Nan xiongchao <nanxiongchao@loongson.cn> -  3.10.84-20.6
- sync from gerrit commit 8cd92886c3f36c6a99d4ffc60288ad7fb2d91f0a
- MIPS: LSVZ: Compile kvm as modules and make fpu.S compile in kernel to eliminate the call of functions in CKSSEG trigger TLB miss problem.
- But it's recommend to compile kvm in kernel for performance.

* Mon Feb 18 2019 Nan xiongchao  <nanxiongchao@loongson.cn> - 3.10.84-20.5
- sync from gerrit commit: b71f96058115fb3425c8bddd1c00f9fdfb42a21a
- commit date Mon Jan 28 12:49:25 2019 from QiaoChong
- Loongson2k swiotlb: add late swiotlb init using pages

* Thu Jan 10 2019 Nan xiongchao  <nanxiongchao@loongson.cn> - 3.10.84-20.4
- backup config file when kernel changes

* Tue Oct 30 2018 Ray Wang <wanglei@loongson.cn> - 3.10.84-20.3
- sync from gerrit commit: 0bd5411cbe3035ad20d8c5d1a088697de5aa975c
- Remove config: CONFIG_HOTPLUG_PCI_PCIE

* Mon Oct 22 2018 Ray Wang <wanglei@loongson.cn> - 3.10.84-20.2
- sync from gerrit commit: 3532ff4ee580e1778cd589984a365842549c1b99

* Sun Oct 14 2018 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-20.1
- sync from gerrit commit ea1c7d7d4e1953a95f3bb42145a5ad516d457966
- Loongson-3:RS780E bridge: Fix up RS780E MSI irq on LS3A.

* Thu Sep 27 2018 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-20
- sync from gerrit commit 1d4b5e17a70e499d8c02ac0a1f060a6f08db8218
- Loongson-3:RS780E bridge:Fix up the RS780E MSI irq-flow method

* Fri Jul  6 2018 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-19.2
- sync from gerrit commit 3235c7008c6d60fe5a15fae6a91f2f6ee11fd9b7
- Loongson-3:LS7A bridge: Fixed the display resolution change bug

* Tue Jul  3 2018 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-19.1
- sync from gerrit commit f039445c873612018bf6a706ae0dd44b3f461f0b
- Loongson-3:LS7A bridge:Add the packed attribute to the vbios definition.

* Mon Jul  2 2018 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-19
- sync from gerrit Commit 173ae723b6f1132d7682777be3f2b08d9e0bf540
- Merge "Loongson-3:LS7A bridge:Switch the default video driver from fb to drm."
- config-mips64el : add CONFIG_LS_SPIFLASH=y

* Fri Jun  15 2018 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-18.4
- sync form gerrit Commit 91b8316e524bf9a8e5131677f9103006598511f1
- MIPS/loongson2k:Fix a GPU driver bug. Fix the BUG:GPU driver compilation failed when using loongson2k_deconfig.

* Wed Jun  6 2018 Ray Wang <wanglei@loongson.cn> - 3.10.84-18.3
- config-mips64el: Rename CONFIG_SND_HDA_LS7A to CONFIG_LS_SND_HDA

* Sat Jun 2 2018 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-18.2
- sync from gerrit Commit 1d5f179a8c32d186fd740d00d4495ddefb8929f9
- Loongson-3:LS7A bridge: Fix the "uma" parameter for GPU driver The bios(UEFI) has changed the "uma" parameter(the old version only support 8GB ram),so kernel must fix it in GPU driver accordingly. 

* Wed Apr 18 2018 Ray Wang <wanglei@loongson.cn> - 3.10.84-18.1
- Add ceph support modules: RBD

* Sat Mar 24 2018 Ray Wang <wanglei@loongson.cn> - 3.10.84-18
- sync from gerrit: c014a286ab0f698ea86e581c944ec0494098a10c
- Add some config: HID_HUIONTABLET, SPI_LS
- LS7A support

* Wed Jan 31 2018 Zhang dandan <zhangdandan@loongson.cn> - 3.10.84-17.1
- Add 7A support

* Mon Oct 16 2017 Ray Wang <wanglei@loongson.cn> - 3.10.84-17
- sync form gerrit: c6a72347a00d3170c519edb53b6f845a6ecdfc4e
- tag: 2017/11/10

* Mon Sep 11 2017 Ray Wang <wanglei@loongson.cn> - 3.10.84-16.2
- modify config-mips64el: add some config (reiserfs...)
- Add vmlinux image

* Wed Aug 16 2017 Ray Wang <wanglei@loongson.cn> - 3.10.84-16.1
- modify config-mips64el: add some config (usblp...)

* Fri Jun 16 2017 Nan xiongchao <nanxiongchao@loongson.cn> - 3.10.84-16
- sync from gerrit Loongson3: Modify ec driver
- kernel tag number 2017/06/01
- First rpm version for kernel 3.10.0 
- add docker support in config file
- fix the errors when installing perf in this spec file

* Sat Apr 15 2017 Ma shuai <mashuai@loongson.cn> - 3.10.84-15
- kernel commit number a0f8132b4cb1566c9e052bc069081ec81f718c21
- kernel tag number 2017/04/15
- (1) Loongson3: Fix up realtek wifi driver display problem.
- (2) Loongson3: Fix up: update realtek 8188ee wifi driver.Modify In the system not use Fn+F6 control wifi enable/disable.
- (3) Loongson3:Add non-coherent support for power management subsystem.
- (4) Loongson3:Add 3A2H GPU driver uncache support.
- (5) Loongson-3: Fix cpu topology infomation displayed in sysfs.

* Wed Mar 15 2017 Ma shuai <mashuai@loongson.cn> - 3.10.84-14
- kernel commit number 7513899f454bb3e9a15f6d5a492b2736d05b5645
- kernel tag number 2017/03/15
- (1) Loongson3:Add 3A2H uncache support and fix up unmap_sg function.
- (2)Loongson3:Fix up ec driver for 3A3000 laptop.
- (3) Merge " Fix up the rtl8168 driver for non-coherent mode".

* Wed Mar 8 2017 Ma shuai <mashuai@loongson.cn> - 3.10.84-13
- kernel commit number 75cad8a99ff9e7a810dc707d348038eb18e66315
- kernel tag number 2017/03/08
- (1)Fix up: realtek 8188ee driver log too much.
- (2)Fix up the rtl8168 driver for non-coherent mode.
- (3)realtek 8188ee wifi supplement.
- (4)Loongson3:Fix up natively compile problem.
- (5)3A3000: Add realtek 8188e wifi support.
- (6)Merge "Add r4k_blast_scache_node_indexed functions to solve the problem that r4k_blast_scache functions only blast Node_0's scache on Loongson3 CC-NUMA platforms."
- (7)Merge "Disable GS464(E) prefetch for non-coherent DMA mode."
- (8)Add r4k_blast_scache_node_indexed functions to solve the problem that r4k_blast_scache functions only blast Node_0's scache on Loongson3 CC-NUMA platforms.
- (9)Loongson3: Change Radeon GPU GTT into non-coherent DMA mode. Program glxgears segmentation fault bug fixed.
- (10)Disable GS464(E) prefetch for non-coherent DMA mode.
- (11)Merge "Loongson3: Change Loongson3 Platforms into non-coherent DMA mode."
- (12)Loongson3: Change Loongson3 Platforms into non-coherent DMA mode.
- (13)Place dma_cache_sync in the rightful place for sb-ls2h swiotlb implementation.
- (14)Add 'synci' before weak 'llsc' to avoid 'sync' instruction. invalidation on GS464E platforms.
- (15)Add 'synci' before weak 'llsc' to avoid 'sync' instruction. invalidation on GS464E platforms.
- (16)Loongson3: Add non-coherent DMA support for loongson3 swiotlb.
- (17)target-mips: KVM: Add loongson_kvm guest config file.
- (18)target-mips: KVM: Add basic fulong2e kvm guest kernel support.
- (19)target-mips: KVM: Add loongson_kvm host config file.
- (20)target-mips: KVM: Port loongson_kvm to Loongson3A2000/3000 platforms.
- (21)target-mips: KVM: Add loongson3 basic kvm host support.
- (22)target-mips: KVM: Select HAVE_KVM for CPU_LOONGSON3.
- (23)3A2000: Deleted loongson3a2000_defconfig file. we use the general configuration file for 3A1000,3A2000,3A3000,loongson3a2000_defconfig. file will not be used.
- (24)Merge "3A2000: Disable 3A2000 temperature monitoring function".
- (25)3A20002H/3A2H: Fix the problem that graphics conflict problem.

* Mon Jan 30 2017 Ma shuai <mashuai@loongson.cn> - 3.10.84-12
- kernel commit number 131056dcc8da181689339bf666d472e893b7cfd7
- kernel tag number 2017/01/30
- (1)loongson3: Fixup the loongson3_defconfig
- (2)Modify touchpad driver for 3A2000/3A3000 laptop
- (3)Add alc269 verbtable for 3A2000/3A3000 laptop
- (4)Brightness down / up can work for 3A2000/3A3000 laptop
- (5)Add EC driver for 3A2000/3A3000 laptop
- (6)Fixup CPU Autoplug support of loongson3A2000/3A3000
- (7)Fixup show /proc/boardinfo error when return from S3
- (8)Loongson3A2000/3A3000: Add CPU Autoplug support.Add "autoplug=on/off" kernel parameter to enable or disable CPU autoplug
- (9)loongson:Change the cpu_model. The cpu_name will not use "ICT Loongson" instead of "Loongson", when cat /proc/cpuinfo,the cpu_model is Loongson-xxx.
- (10)Add S3 support for Loongson3 family
- (11)Fixed up ALC662 Headphone and Outside function
- (12)loongson3: When Merge loongson3 and loongson3a2000 configs as loongson3_defconfig,code missed some New features of 3a2000.

* Wed Nov 30 2016 Gao juxin <gaojuxin@loongson.cn> - 3.10.84-11
- kernel commit number ac13de0dee034fe31c2c427c67b36386dbbbb18d
- kernel tag number 2016/11/30
- (1)loongson3: perf event modification in 3A2000 board
- (2)Merge "loongson3: Perf is supported in 3A2000 board"
- (3)loongson3: Perf is supported in 3A2000 board
- (4)Merge "getrandom syscall support"
- (5)move Radeon Firmware from kernel to file system in 3Aconfig
- (6)loongson3:Enable cu0 ~ cu3 before decompressed the vmlinuz
- (7)getrandom syscall support
- (8)Merge "loongson3:loongson3a2000_defconfig & loongson3_defconfig default open debugfs. note:should mount before use."
- (9)loongson3:Enable CU2 when run the gsl[dwhb]x,gss[dwhb]x instruction for loongson3a1000
- (10)loongson3:loongson3a2000_defconfig & loongson3_defconfig default open debugfs.
- (11)Loongson3a1000 is not support gsl[dwhb]x,gss[dwhb]x instruction,add mips64r2 option in Makefile,then the gcc-4.9.3 will not compile
	out the gsl[dwhb]x,gss[dwhb]x instruction.
- (12)Merge "fix the warning when runltp,like this"
- (13)Merge "MIPS: Loongson-3: Set cache flush handlers to cache_noop"
- (14)Merge "MIPS: Fix page table corruption on THP permission changes according to higher kernel.
- (15)Merge " MIPS: SMP: Fix possibility of deadlock when bringing CPUs online according to higher kernel"
- (16)loongson3:Fix up 3a2000 four way DMA operation,the node3 will not do swiotlb
- (17)fix the warning when runltp.
- (18)MIPS: Loongson-3: Set cache flush handlers to cache_noop Loongson-3 maintains cache coherency by hardware
- (19)MIPS: Fix page table corruption on THP permission changes according to higher kernel.
- (20) MIPS: SMP: Fix possibility of deadlock when bringing CPUs online according to higher kernel
- (21)3A2H/3A82H/2HSoC:Fix up 2h3 hda record

* Sun Oct 30 2016 Ma shuai <mashuai@loongson.cn> - 3.10.84-9
- kernel commit number cc6f57e6e8313780229544573336b890c4dcefbb  
- kernel tag number 2016/10/30  
- (1)glances is supported in kernel-3.10
- (2)Merge changes Ib1cddb0f,I1aa1d4de
	1.configs: extra firmware add ATI chipset REDWOOD support
	2.configs: move GPU firmware from kernel to filesystem
- (3)Merge "Add the chip 3A3000 support."
- (4)Add the chip 3A3000 support.
- (5)backport maddf/msub* and fix from 4.4
- (6)configs: extra firmware add ATI chipset REDWOOD support
	1.Products with REDWOOD: HD5550, HD5570, HD5670
	2.REDWOOD firmware: radeon/REDWOOD_me.bin
                            radeon/REDWOOD_pfp.bin
                            radeon/REDWOOD_rlc.bin
                            radeon/REDWOOD_smc.bin
                            radeon/CYPRESS_uvd.bin
- (7)configs: move GPU firmware from kernel to filesystem
- (8)loongson3:Add usb printer support and update loongson3a2000_defconfig file
- (9)3A82H: Add the reference board support.
- (10)loongson3:Fixup the CU2 operation and add fanotify support by modify configuration file.
- (11)3A2H/3A82H: Fixup the DMA operation for sourth bridge devices
- (12)3A2H/3A82H: Fixup the pcie_phys_to_dma and pcie_dma_to_phys function for swiotlb
- (13)Revert "BNX2(X): Update Broadcom NIC firmware from linux-firmware"
- (14)loongson3: Realize one kernel binary file support different system.
- (15)configs: Select Broadcom NIC drivers for loongson3 platforms
- (16)BNX2(X): Update Broadcom NIC firmware from linux-firmware

* Wed Aug 10 2016 Ma shuai <mashuai@loongson.cn> - 3.10.84-8
- kernel commit number 235c9fae4efec1d15eacf9a25a5f0e943c6aa02d  
- kernel tag number 2016/08/10  
- (1)Merge "MIPS: Expand __swp_offset() to carry 40 significant bits for 64-bit kernel."
- (2)fix the system halt error caused by the huge page
- (3)MIPS: Expand __swp_offset() to carry 40 significant bits for 64-bit kernel.
- (4)Loongson3: Move phase lock critical code into non-preemptive context
- (5)Loongson3: Replace RS780E HPET cevt with r4k cevt
        1.Drop HPET cevt rating to 60
        2.Still use HPET counter as global clocksource
- (6)Merge changes I9cbbd8e5,I7c4002bc
	1. add unaligned gssq/gslq/gssqc1/gslqc1 emulation
	2. fix unaligned gslwxc1/gsldxc1/gssdxc1/gsswxc1 emulation

* Wed Jul 27 2016 Ma shuai <mashuai@loongson.cn> - 3.10.84-7
- kernel commit number 2980239839ede8e80b3f987a3aa27c0cfb8039a1 
- kernel tag number 2016/07/29  
- (1)Add AMD E8860 6 miniDP Support
- (2)3A2000: Use realtek-r8168NetDriver replace r8169NetDriver
         slove: 1.TCP_STREAM Low Value
                2.Net system Testing unstabitily  
- (3)fix the compile error when use gcc-4.9.3 on fedora21

* Mon Jul 25 2016 Xiaojuan Yang <yangxiaojuan@loongson.cn> - 3.10.84-6
- update config file to support install from LiveCD 
- Add gslwxc1/gsldxc1/gsswxc1/gssdxc1 emulation 
- Add loongson3 gslh/w/dx, gssh/w/dx unaligned access handle 

* Thu Aug 27 2015 Xiaofu Meng <mengxiaofu@loongson.cn> - 3.10.84-15.08.27.loongso3 
- First rpm version for kernel 3.10.84 

###
# The following Emacs magic makes C-c C-e use UTC dates.
# Local Variables:
# rpm-change-log-uses-utc: t
# End:
###


