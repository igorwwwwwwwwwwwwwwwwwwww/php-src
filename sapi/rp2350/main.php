<?php
require '/lib.php';
print "req:lib:ok\n";
require '/logo.php';
print "req:logo:ok\n";

$did_epd = false;

while (true) {
    if (!$did_epd) {
        print "logo:decode:start\n";
        $logo = php_logo_data();
        print "logo:decode:done\n";
        print "epd:render\n";
        if ($logo !== false) {
            $buf = $logo[0];
            $w = $logo[1];
            $h = $logo[2];
            mcu_epd_render($buf, $w, $h);
        } else {
            print "epd:logo-decode-fail\n";
        }
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
