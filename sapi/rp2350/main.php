<?php
require '/lib.php';

$did_epd = false;

while (true) {
    if (!$did_epd) {
        print "epd:fb-demo\n";
        mcu_epd_clear(false);

        // border
        for ($x = 0; $x < 264; $x++) {
            mcu_epd_set_pixel($x, 0, true);
            mcu_epd_set_pixel($x, 175, true);
        }
        for ($y = 0; $y < 176; $y++) {
            mcu_epd_set_pixel(0, $y, true);
            mcu_epd_set_pixel(263, $y, true);
        }

        // diagonals
        for ($i = 0; $i < 176; $i++) {
            $x1 = (int)($i * 263 / 175);
            $x2 = 263 - $x1;
            mcu_epd_set_pixel($x1, $i, true);
            mcu_epd_set_pixel($x2, $i, true);
        }

        mcu_epd_update();
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
