# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(fsize-normal) begin
(fsize-normal) end
fsize-normal: exit(0)
EOF
pass;
