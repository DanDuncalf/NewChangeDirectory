import re

path = r'test\Win\test_features.bat'
with open(path, 'r') as f:
    content = f.read()

# Pattern: -ArgumentList '%CONF_OVERRIDE% ...'
# We want to replace with: -ArgumentList '-conf','%CONF_PATH%',...
# But we don't have CONF_PATH. Instead we can use array syntax with the variable expanded.
# Actually, in batch, we can use delayed expansion or just construct the array.
# The simplest approach in the batch file is to replace:
#   -ArgumentList '%CONF_OVERRIDE% <rest>'
# with:
#   -ArgumentList '-conf','%TEST_DATA%\NCD\ncd.metadata',<rest_array>
# But <rest> may contain quoted strings that also need splitting.
#
# Alternative: use $psi.Arguments = '...' pattern which works because it's a direct string assignment.
# Wait, $psi.Arguments also suffers from the quote issue if the string contains quotes.
# Actually no - $psi.Arguments is passed directly to CreateProcess, and the quotes in the string
# would still be literal characters in the command line. So that doesn't help.
#
# The best fix is to change all -ArgumentList string uses to array syntax.
# We need to split the arguments properly.

# Since the batch file only has a few patterns, let's just manually fix the remaining ones.
# Or we can write a more targeted regex replacement.

# Let's check what lines still use -ArgumentList '%CONF_OVERRIDE%
lines = content.split('\n')
for i, line in enumerate(lines):
    if "-ArgumentList '%CONF_OVERRIDE%" in line:
        print(f"Line {i+1}: {line.strip()}")
