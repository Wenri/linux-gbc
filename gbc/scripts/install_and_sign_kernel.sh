#!/usr/bin/env bash

set -e
set -u
set -o pipefail

install_deb() {
    sudo apt install --purge ./*{headers,image}-$1*.deb $(apt list --installed 2>/dev/null | grep -e 'linux-.*hwifi.*-xanmod.' | grep -v "$(uname -r)" | sed 's/\/.*$/-/g')
}

sign_kernel() {
    sudo sbsign --key /var/lib/shim-signed/mok/MOK.priv \
                --cert /var/lib/shim-signed/mok/MOK.pem "/boot/vmlinuz-$1" \
                --output "/boot/vmlinuz-$1.signed"
    sudo mv "/boot/vmlinuz-$1.signed" "/boot/vmlinuz-$1"
}

TARGET_VERSION=$1

install_deb "$TARGET_VERSION"
#sign_kernel "$TARGET_VERSION"

exit 0
