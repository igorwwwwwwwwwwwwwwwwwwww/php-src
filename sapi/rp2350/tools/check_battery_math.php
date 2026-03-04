<?php

require __DIR__ . '/../fs/lib.php';

$samples = array(
    array(2602, 1358),
    array(2601, 1357),
    array(2603, 1358),
    array(169, 1358),
);

foreach ($samples as $s) {
    $raw_vbat = $s[0];
    $raw_vref = $s[1];
    $voltage = mcu_battery_voltage_from_raw($raw_vbat, $raw_vref);
    $pct = mcu_battery_level_from_voltage($voltage);
    echo "raw_vbat={$raw_vbat} raw_vref={$raw_vref} voltage={$voltage} pct={$pct}\n";
}
