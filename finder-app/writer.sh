#!/bin/sh

if [ $# -ne 2 ]; then
    echo "Usage: $0 <file> <string>"
    exit 1
fi
file=$1
content=$2

mkdir -p "$(dirname "$file")" && touch "$file"

echo "$content" > "$file"
exit $?
