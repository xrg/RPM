#!/bin/sh

# -P blindly assumed
while read file; do
	case $file in
          */etc/init.d/*)
		provs=`grep '^# *Provides:' $file | sed 's,^.*:,,'`
		for p in $provs; do
			echo "sysvinit($p)"
		done
	  ;;
        esac
done
