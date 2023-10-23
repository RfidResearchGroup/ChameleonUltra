#!/bin/bash

if ! command -v recode >/dev/null; then
    echo "Please install 'recode' package first" ;
    exit 1
fi

echo "Files with suspicious chars:"
find . \( -not -path "./.git/*" -and -not -path "./firmware/nrf52_sdk/*" -and -not -path "./firmware/objects/*" -and -not -path "./software/script/venv/*"  -and -not -path "./software/bin/*"  -and -not -path "./software/src/tmp/*" \) -and \( -name "*.[ch]" -or -name "*.py" -or -name "Makefile" -or -name "*.txt" -or -name "*.md" -or -name "*.sh" \) -exec sh -c "cat {} |recode utf8..ascii >/dev/null 2>&1 || ( echo \"\n{}:\"; cat {} |tr -d ' -~'|sed 's/\(.\)/\1\n/g'|sort|uniq|tr -d '\n' )" \;
echo

# Chinese encoding: GB18030 extending EUC-CN
# recode GB18030..UTF-8 file.c
# If it fails it could be due to mix of GB18030 & UTF-8 in the same file, check by hand
