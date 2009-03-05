#!/bin/sh
#
# ipkg build script for wavemon
#
# Extracted from a former Makefile target by Dave W. Capella
#
# However I could not reach that person, hence $maintainer is commented out.
# Send any updates / suggestions to gerrit@erg.abdn.ac.uk.

# CONFIGURATION
#maintainer="dave w capella <dave.capella@cornell.edu>"	# email address outdated
version="0.5"

build_dir="./ipkg"
ctl_dir="${build_dir}/CONTROL"
bin_dir="${build_dir}/usr/local/bin"
doc_dir="${build_dir}/usr/doc/wavemon"
men_dir="${build_dir}/usr/lib/menu"

set -e

# 'clean' option
case "$1" in
	clean|CLEAN) rm -vfr $build_dir; exit 0;
esac


# Build directories
for dir in $ctl_dir $bin_dir $doc_dir $men_dir
do
	test -d $dir || mkdir -vp $dir
done


# Binary
test -f ../wavemon || exec echo "Run 'make' first to build wavemon"
cp  -vp ../wavemon ${bin_dir}


# Documentation
for ipaq_doc in  ../AUTHORS ../COPYING ../Makefile ../README
do
	cp -vp ${ipaq_doc} ${doc_dir}
done

groff -Tascii -man ../wavemon.1   > ${doc_dir}/wavemon.doc
groff -Tascii -man ../wavemonrc.5 > ${doc_dir}/wavemonrc.doc


# Menu
cat > ${men_dir}/wavemon <<EO_MENU
?package(wavemon): needs="text" section="Utilities" title="wavemon" command="wavemon"
EO_MENU


# Control
cat > ${ctl_dir}/control <<EO_CONTROL
Package: wavemon
Version: $version
Architecture: arm
Maintainer: ${maintainer:-unknown}
Description: Wireless network monitoring tool
Priority: optional
Section: extras
EO_CONTROL

# Build
ipkg-build ${build_dir}
