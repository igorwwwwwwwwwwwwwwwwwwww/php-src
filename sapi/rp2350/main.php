<?php
require '/lib.php';

while (true) {
    $lib = file_get_contents('/lib.php');
    if ($lib === false) {
        print "fgetc-fail\n";
    } else {
        print "fgetc-ok:";
        print strlen($lib);
        print "\n";
    }
    print tick_line();
    mcu_sleep_ms(1000);
}
