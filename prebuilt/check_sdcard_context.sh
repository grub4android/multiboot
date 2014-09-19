#!/multiboot/busybox sh

# ARGS
CONTEXT=$1
VOLD_PATH=$2
ID_FILE=$3

# BUSYBOX
BB=/multiboot/busybox

# CHECK
RESULT=$($BB strings $VOLD_PATH | $BB grep -c $CONTEXT)

# RESULT
if [ $RESULT -ge 1 ];then
$BB touch $ID_FILE
else
$BB rm -f $ID_FILE
fi
