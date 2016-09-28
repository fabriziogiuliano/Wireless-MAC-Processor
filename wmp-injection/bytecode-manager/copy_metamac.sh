#!/bin/sh

set -x

make metamac

scp metamac root@alix15:~/metamac/
scp metamac root@alix05:~/metamac/
scp metamac root@alix04:~/metamac/
scp metamac root@alix03:~/metamac/

set +x
