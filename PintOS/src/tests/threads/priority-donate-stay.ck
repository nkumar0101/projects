# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(priority-donate-stay) begin
(priority-donate-stay) Main thread acquired locks A and B.
(priority-donate-stay) Main thread released lock B.
(priority-donate-stay) Main thread should print this before high1 acts.
(priority-donate-stay) High priority thread high1 acquired lock.
(priority-donate-stay) High priority thread high1 released lock.
(priority-donate-stay) High priority thread high0 acquired lock.
(priority-donate-stay) High priority thread high0 released lock.
(priority-donate-stay) Main thread released lock A.
(priority-donate-stay) Main thread should print this after high0 acts.
(priority-donate-stay) end
EOF
pass;
