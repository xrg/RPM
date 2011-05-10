#!/bin/bash

[ $# -ge 1 ] || {
    cat > /dev/null
    exit 0
}

case $1 in
-P|--provides)
    shift
    grep "/usr/bin/python.\..$" \
	| sed -e "s|.*/usr/bin/python\(.\..\)|python(abi) = \1|"
    ;;
-R|--requires)
    shift
    grep "/usr/lib[^/]*/python.\../.*" \
	| sed -e "s|.*/usr/lib[^/]*/python\(.\..\)/.*|python(abi) = \1|g" \
	| sort | uniq
    ;;
esac

exit 0
