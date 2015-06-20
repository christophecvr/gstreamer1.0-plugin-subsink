DESCRIPTION = "gstreamer subsink plugin"
SECTION = "multimedia"
PRIORITY = "optional"
DEPENDS = "gstreamer1.0 gstreamer1.0-plugins-base"
LICENSE = "LGPLv2.1"
LIC_FILES_CHKSUM = "file://COPYING;md5=278f2557e3b277b94e9a8430f6a6d0a9"

SRC_URI = "git://git.code.sf.net/p/openpli/gstsubsink;protocol=git;branch=gst-1.0"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

inherit gitpkgv

GSTVERSION = "1.0"

PV = "${GSTVERSION}+git${SRCPV}"
PKGV = "${GSTVERSION}+git${GITPKGV}"
PR = "r0"

EXTRA_OECONF = "--with-gstversion=${GSTVERSION}"


inherit autotools pkgconfig

FILES_${PN} = "${libdir}/gstreamer-${GSTVERSION}/*.so*"
FILES_${PN}-dev += "${libdir}/gstreamer-${GSTVERSION}/*.la"
FILES_${PN}-staticdev += "${libdir}/gstreamer-${GSTVERSION}/*.a"
FILES_${PN}-dbg += "${libdir}/gstreamer-${GSTVERSION}/.debug"

PACKAGE_ARCH = "${MACHINE_ARCH}"
