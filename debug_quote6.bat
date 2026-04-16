@echo off
setlocal
set "TESTROOT=T:\"
python -c "import sys; print(sys.argv[1:])" "%TESTROOT%"
python -c "import sys; print(sys.argv[1:])" "T:\"
python -c "import sys; print(sys.argv[1:])" "T:\."
endlocal
