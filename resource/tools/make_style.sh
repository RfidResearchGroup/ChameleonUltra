#!/bin/bash

# Make sure astyle is installed
if ! command -v astyle >/dev/null; then
    echo "Please install 'astyle' package first" ;
    exit 1
fi
# Remove spaces & tabs at EOL, add LF at EOF if needed on *.c, *.h, *.py, Makefile, *.txt
find . \( -not -path "./.git/*" -and -not -path "./firmware/nrf52_sdk/*" -and -not -path "*/venv/*" -and -not -path "*/tmp/*" -and \( -name "*.[ch]" -or -name "*.py" -or -name "Makefile" -or -name "*.txt" \) \) \
    -exec perl -pi -e 's/[ \t]+$$//' {} \; \
    -exec sh -c "tail -c1 {} | xxd -p | tail -1 | grep -q -v 0a\$" \; \
    -exec sh -c "echo >> {}" \;
# Apply astyle on *.c, *.h
find . \( -not -path "./.git/*" -and -not -path "./firmware/nrf52_sdk/*" -and -not -path "*/venv/*" -and -not -path "*/tmp/*" -and -name "*.[ch]" \) -exec astyle --formatted --mode=c --suffix=none \
    --indent=spaces=4 --indent-switches \
    --keep-one-line-blocks --max-instatement-indent=60 \
    --style=google --pad-oper --unpad-paren --pad-header \
    --align-pointer=name {} \;

# Detecting tabs.
if [[ "$(uname)" == "Darwin" ]]; then
    TABSCMD="egrep -l  '\t' {}"
else
    TABSCMD="grep -lP '\t' {}"
fi
${REWRITE_TABS:=false}
if $REWRITE_TABS; then
    TABSCMD="$TABSCMD && vi {} -c ':set tabstop=4' -c ':set et|retab' -c ':wq'"
    echo "Files with tabs: (EDIT enabled, files will be rewritten!)"
else
    echo "Files with tabs: (rerun with \"REWRITE_TABS=true $0\" if you want to convert them with vim)"
fi

# to remove tabs within lines, one can try with: vi $file -c ':set tabstop=4' -c ':set et|retab' -c ':wq'
find . \( -not -path "./.git/*" -and -not -path "./firmware/nrf52_sdk/*" -and -not -path "*/venv/*" -and -not -path "*/tmp/*" -and \( -name "*.[ch]" -or -name "*.py" -or -name "*.md" -or -name "*.txt" \) \) \
      -exec sh -c "$TABSCMD" \;
