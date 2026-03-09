# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(write-stdout) begin
(child-long) begin
This is going to be a rather long message. We are trying to test if the write function to stdout will have everything written in one go.
This may seem familiar; if this is the second time you're reading this, and in order, great!
(child-long) end
child-long: exit(0)
(child-long) begin
This is going to be a rather long message. We are trying to test if the write function to stdout will have everything written in one go.
This may seem familiar; if this is the second time you're reading this, and in order, great!
(child-long) end
child-long: exit(0)
(write-stdout) end
write-stdout: exit(0)
EOF
pass;