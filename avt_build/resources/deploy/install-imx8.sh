#!/bin/bash

AVT_TMP="/tmp/avt-kernel-install"
KERNEL_VERSION="unknown"
TAB='\033[60G'


function logr() {
  if [ "$SILENT" != y ]; then
    echo -e "$@"
  fi
}

function log() {
  logr "   --> $1"
}

function logn() {
  logr -n "   --> $1"
}


function header() {
  logr "\033[31m//\033[0m \033[37mAllied Vision\033[0m   Kernel for Toradex Apalis iMX8 installer"
}


function usage() {
  header
  log "Usage:"
  log "  $(basename $0) [-y] [-f] [-r] [-s]" # [-v]"
  log "     -y: Do not ask for confirmation before installing, do not reboot unless -r is given"
  log "     -f: force installation even if sanity checks fail"
  log "     -r: reboot after successful installation"
  log "     -s: silent mode: don't output anything"
}

while getopts ":frsy" o; do
  case "$o" in
    f)
      FORCE=y
      ;;
    r)
      REBOOT=y
      ;;
    s)
      SILENT=y
      ;;
    y)
      DONT_ASK=y
      ;;
    *)
      usage
      exit 0
      ;;
  esac
done


function try() {
  TEXT=$1
  CMD=$2

  logn "$TEXT...${TAB}"

  OUTPUT=$($CMD 2>&1)

  LAST_ERR=$?
  if [ $LAST_ERR -eq 0 ]; then
    logr "[\033[32mok\033[0m]"
  else
    logr "[\033[31merror\033[0m]"
    log ""
    log "Command returned $LAST_ERR, output was:"
    log "$OUTPUT"
    log ""
    if [ "$FORCE" = y ]; then
      log "Forced installation requested, continueing."
    else
      log "Installation may be incomplete, please retry."
      exit $LAST_ERR
    fi
  fi
}


function unpack_tarball() {
  tar -xzf modules.tar.gz --keep-directory-symlink -C /
}


function install_kernel() {
  cp Image $(grep "^kernel_image=" /boot/loader/uEnv.txt | sed -e 's/^kernel_image=/\/boot/')
}


function install_dtb() {
  FDT_PATH="$(grep '^fdt_path=' /boot/loader/uEnv.txt | sed -e 's/^fdt_path=/\/boot/')"
  cp fsl-imx8qm-apalis.dtb "$FDT_PATH/devicetree-fsl-imx8qm-apalis-eval.dtb"
}


function verify_hardware() {
  if [ ! -f /etc/os-release ] || ! grep -q 'ID="torizon"' /etc/os-release; then
    if [ "$FORCE" = y ]; then
      log "\033[33;1mWARNING\033[0m: This doesn't appear to be a Torizon system. Forced installation requested: Continuing."
    else
      log "\033[31mERROR\033[0m: This doesn't appear to be a Torizon system. Refusing to install."
      exit 1
    fi
  fi


  if ! grep 'Toradex Apalis iMX' /proc/device-tree/model >/dev/null; then
    if [ "$FORCE" = y ]; then
      log "\033[33;1mWARNING\033[0m: This doesn't appear to be an i.MX8 CPU. Forced installation requested: Continuing."
    else
      log "\033[31mERROR\033[0m: This doesn't appear to be an i.MX8 CPU. Refusing to install."
      exit 1
    fi
  fi
}


function remount_usr_writable() {
  mount -oremount,rw /usr
}


header

if [ $UID != 0 ]; then
  log "\033[33;1mError\033[0m: The installer needs to be run as root."
  log "Try sudo $0 $@"
  exit 1
fi


log "Currently installed version:${TAB}$(uname -r)"
log "Version to be installed:${TAB}$KERNEL_VERSION"

if [ "$DONT_ASK" != y ]; then
  echo -n "Proceed with installation? [y/n] "
  while read proceed; do
    case "$proceed" in
    y|Y)
      break
      ;;
    n|N)
      exit 0
      ;;
    esac
    echo -n "Proceed with installation? [y/n] "
  done
fi

verify_hardware
try "Remounting filesystem writable" remount_usr_writable
try "Unpacking modules" unpack_tarball
try "Installing kernel" install_kernel
try "Installing device tree" install_dtb
log "Installation successfully completed."

if [ "$REBOOT" = y ]; then
  log "Reboot requested."
  reboot
else
  if [ "$DONT_ASK" != y ]; then
    echo -n "Reboot now? [y/n] "
    while read reboot; do
      case "$reboot" in
      y|Y)
        reboot
        exit 0
        ;;
      n|N)
        exit 0
        ;;
      esac
      echo -n "Reboot now? [y/n] "
    done
  fi
fi


exit 0
