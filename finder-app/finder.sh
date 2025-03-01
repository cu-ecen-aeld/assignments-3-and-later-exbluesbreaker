#!/bin/sh

if [ $# -ne 2 ]; then
    echo "Usage: $0 <directory> <pattern>"
    exit 1
fi
path=$1
pattern=$2
if [ ! -d $path ]; then
  echo "Provided path is not a directory."
  exit 1
fi

find $path -type f -exec grep -c $pattern {} + | awk -F: 'BEGIN {mfiles = 0; mlines = 0} {if ($2 > 0) mfiles += 1; mlines += $2} END {print "The number of files are " mfiles " and the number of matching lines are " mlines}'
