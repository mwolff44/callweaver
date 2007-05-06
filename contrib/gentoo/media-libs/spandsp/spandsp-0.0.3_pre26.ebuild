# Copyright 1999-2006 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: /var/cvsroot/gentoo-x86/media-libs/spandsp/spandsp-0.0.2_pre26.ebuild,v 1.1 2006/09/19 20:10:50 gustavoz Exp $

IUSE=""

DESCRIPTION="SpanDSP is a library of DSP functions for telephony."
HOMEPAGE="http://www.soft-switch.org/"

S="${WORKDIR}/${PN}-0.0.3"
SRC_URI="http://www.soft-switch.org/downloads/spandsp/${P/_/}.tgz"

SLOT="0"
LICENSE="GPL-2"
KEYWORDS="~amd64 ~ppc x86"

DEPEND=">=media-libs/audiofile-0.2.6-r1
	>=media-libs/tiff-3.5.7-r1"

src_unpack (){
	unpack ${P/_/}.tgz

}


src_compile () {
	cd ${S}
	econf --prefix=/usr || die "Configuration failed."
	emake || die "Compilation failed."
}

src_install () {
	einstall || die
	dodoc AUTHORS COPYING INSTALL NEWS README
}