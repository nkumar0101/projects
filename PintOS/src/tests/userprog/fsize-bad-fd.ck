# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF', <<'EOF']);
(fsize-bad-fd) begin
(fsize-bad-fd) end
fsize-bad-fd: exit(0)
EOF
(fsize-bad-fd) begin
fsize-bad-fd: exit(-1)
EOF
pass;
