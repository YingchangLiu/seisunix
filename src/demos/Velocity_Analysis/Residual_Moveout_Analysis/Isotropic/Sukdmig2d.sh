#! /bin/sh
# shell for Kirchhoff depth migration
set -v

sukdmig2d < data  par=kdmig.par>kd.data.su 

exit 0

