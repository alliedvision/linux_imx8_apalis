#!/bin/bash -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

KERNELDIR="$DIR/.."
BUILDDIR="$DIR/build_imx8"
DEPLOYDIR="$DIR/deploy"
RESOURCEDIR="$DIR/resources"
INSTALLERFILE="$DIR/apalis-imx8-avt-install.sh"
DTB=freescale/fsl-imx8qm-apalis.dtb

mkdir -p "$BUILDDIR"
rm -rf "$DEPLOYDIR"

# Copy default configuration and fix it: set LOCALVERSION to "avt" and enable LOCALVERSION_AUTO
sed -e 's/^.*\bCONFIG_LOCALVERSION_AUTO\b.*$/CONFIG_LOCALVERSION_AUTO=y/' \
    -e 's/^.*\bCONFIG_LOCALVERSION\b.*$/CONFIG_LOCALVERSION="avt"/'       \
    > "$BUILDDIR/.config"                                                 \
    < "$KERNELDIR/arch/arm64/configs/defconfig"

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

# Update configuration
make -C "$KERNELDIR" O="$BUILDDIR" olddefconfig

# Build kernel, modules and device tree
make -C "$BUILDDIR" Image modules $DTB -j$(nproc)

# Find compiled kernel's version string
KERNEL_VERSION=$(strings "$BUILDDIR"/arch/$ARCH/boot/Image | grep -oP "Linux version \K(\d[^ ]+)")

AVT_FILENAME="AlliedVision_Apalis_iMX8_Torizon-$KERNEL_VERSION"
INSTALL_DIR="$DEPLOYDIR/$AVT_FILENAME"
mkdir -p "$INSTALL_DIR"

# Deploy modules to deploy directory
make -C "$BUILDDIR" INSTALL_MOD_PATH="$DEPLOYDIR" modules_install


# Copy remaining files
cp "$BUILDDIR/arch/$ARCH/boot/"{Image,dts/$DTB} "$INSTALL_DIR"
sed -e 's/^KERNEL_VERSION=".*$/KERNEL_VERSION="'$KERNEL_VERSION'"/' <"$RESOURCEDIR/deploy/install-imx8.sh" >"$INSTALL_DIR/install.sh"
chmod a+x "$INSTALL_DIR/install.sh"


# Create temporary tarball
#TMPTARBALL="/tmp/imx-apalis-avt.tar.gz"
#TMPINSTALL="/tmp/imx-apalis-avt-install-script.sh"
MODULETARBALL="$INSTALL_DIR/modules.tar.gz"
DST_TARBALL="$DEPLOYDIR/$AVT_FILENAME.tar.gz"

(
  cd "$DEPLOYDIR"
  tar --owner=root --group=root -czf "$MODULETARBALL" lib
)


tar czf "$DST_TARBALL" -C "$DEPLOYDIR" "$AVT_FILENAME"

