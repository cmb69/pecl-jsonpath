--TEST--
Test filter expression with equals array for array slice with range 1
--SKIPIF--
<?php if (!extension_loaded("jsonpath")) print "skip"; ?>
--FILE--
<?php

$data = [
    [
        1,
        2,
        3,
    ],
    [
        1,
    ],
    [
        2,
        3,
    ],
    1,
    2,
];

$jsonPath = new JsonPath();
$result = $jsonPath->find($data, "$[?(@[0:1]==[1])]");

echo "Assertion 1\n";
var_dump($result);
?>
--EXPECT--
??
--XFAIL--
Not sure what would be the best outcome here
