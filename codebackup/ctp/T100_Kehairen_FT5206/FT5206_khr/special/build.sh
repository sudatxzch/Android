#!/bin/bash
#===========================================================================================
# find android -name Android.mk | xargs -I file mv file file.3rdparty
# find android -iname Android.mk.3rdparty | xargs rename 's/\.3rdparty$//'
lcurdir=$(readlink -f .)
while [ "${lcurdir}" != "/" ]; do
   [ -f ${lcurdir}/customize/shell/build.3rdparty.common.sh ] && { \
   source ${lcurdir}/customize/shell/build.3rdparty.common.sh; break; }
   lcurdir=$(readlink -f ${lcurdir}/..)
done
#===========================================================================================
function my_local_build()
{
# build myself related contents after build android
cd ${MYCHIP_BASE}/driver
make -C ${ANDROID_3RDPARTY_KERNEL_OUTPUT} \
     CROSS_COMPILE=${ANDROID_3RDPARTY_CROSS_COMPILE} M=`pwd` \
     $2 1>&2 # | tee >/dev/null # 2>&1 | tee >/dev/null
}

function my_local_install()
{
# install your created files to the propery folder after android build finish
    mkdir -p ${MYCHIP_BASE_INSTALL}/ko
    cd ${MYCHIP_BASE}/driver
    find . -iname '*.ko' | xargs -I xxxfile cp xxxfile ${MYCHIP_BASE_INSTALL}/ko

    cd ${MYCHIP_BASE_INSTALL}/ko
    ${ANDROID_3RDPARTY_CROSS_COMPILE}strip -g -S -d *.ko
# copy firmware
mkdir -p  ${MYCHIP_BASE_INSTALL}/firmware
cd ${MYCHIP_BASE}/firmware
cp ft5206_fw.bin ${MYCHIP_BASE_INSTALL}/firmware
}

my_local_link_files="
firmware/ft5206_fw.bin:system/etc/firmware
"

$(sprd_build_3rdparty $@)

#///////////////////////////////////////////HowTo////////////////////////////////////////////
#
#///////////////////////////////////////////////////////////////////////////////////////////
