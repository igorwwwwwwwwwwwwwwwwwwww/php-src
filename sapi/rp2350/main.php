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
        // print "epd:render\n";
        // if ($logo !== false) {
        //     $buf = $logo[0];
        //     $w = $logo[1];
        //     $h = $logo[2];
        //     mcu_epd_render($buf, $w, $h);
        // } else {
        //     print "epd:logo-decode-fail\n";
        // }
        $did_epd = true;
    }

    $fp = fopen('/lib.php', 'rb');
    if ($fp === false) {
        print "fopen-fail\n";
    } else {
        $probe = fread($fp, 1);
        $ch = fgetc($fp);
        fclose($fp);
        if ($probe === false) {
            print "fread-fail\n";
        } else if ($probe === '') {
            print "fread-empty\n";
        } else {
            print "fread-ok:";
            print ord($probe);
            print "\n";
        }
        if ($ch === false) {
            print "fgetc-fail\n";
        } else {
            print "fgetc-ok:";
            print ord($ch);
            print "\n";
        }
    }
    $lib = file_get_contents('/lib.php');
    if ($lib === false) {
        print "fgc-fail\n";
    } else {
        print "fgc-ok:";
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
