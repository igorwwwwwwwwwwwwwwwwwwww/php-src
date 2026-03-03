<?php
require '/lib.php';

$did_epd = false;

while (true) {
    if (!$did_epd) {
        print "epd:white\n";
        mcu_epd_fill(false);
        sleep(1);
        print "epd:black\n";
        mcu_epd_fill(true);
        sleep(1);
        print "epd:white\n";
        mcu_epd_fill(false);
        $did_epd = true;
    }

    $lib = file_get_contents('/lib.php');
    if ($lib === false) {
        print "fgetc-fail\n";
    } else {
        print "fgetc-ok:";
        print strlen($lib);
        print "\n";
    }
    print "time:";
    print time();
    print " microtime_s:";
    print microtime();
    print " microtime_f:";
    print microtime(true);
    $hrt = hrtime();
    print " hrtime_a:";
    print $hrt[0];
    print ".";
    print $hrt[1];
    print " hrtime_n:";
    print hrtime(true);
    print "\n";
    print tick_line();
    sleep(1);
}
