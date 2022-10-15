#!/bin/bash
PWD=`pwd`
OUT=$PWD/out/target/product/evk_8mp
TF=$OUT/tfcard
UUU=$OUT/uuu
if [ -e $TF ]
then
        rm -fr $TF
fi
mkdir $TF

if [ -e $UUU ]
then
        rm -fr $UUU
fi
mkdir $UUU

simg2img $OUT/logo.img $TF/logo_raw.img
simg2img $OUT/super.img $TF/super_raw.img

cp -fr $OUT/boot.img $UUU/
cp -fr $OUT/dtbo-imx8mp.img $UUU/
cp -fr $OUT/partition-table.img $UUU/
cp -fr $OUT/u-boot-imx8mp-evk-uuu.imx $UUU/
cp -fr $OUT/u-boot-imx8mp.imx $UUU/
cp -fr $OUT/vbmeta-imx8mp.img $UUU/
cp -fr $OUT/vendor_boot.img $UUU/
cp -fr $OUT/super.img $UUU/
cp -fr $OUT/logo.img $UUU/
cp -fr $OUT/uuu_imx_android_flash.bat $UUU/
cp -fr $OUT/uuu_imx_android_flash.sh $UUU/

cp -fr $OUT/boot.img $TF/
cp -fr $OUT/dtbo-imx8mp.img $TF/
cp -fr $OUT/partition-table.img $TF/
cp -fr $OUT/u-boot-imx8mp.imx $TF/
cp -fr $OUT/vbmeta-imx8mp.img $TF/
cp -fr $OUT/vendor_boot.img $TF/
