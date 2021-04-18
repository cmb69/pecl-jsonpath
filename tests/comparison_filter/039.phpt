--TEST--
Test filter expression with equals number with leading zeros
--SKIPIF--
<?php if (!extension_loaded("jsonpath")) print "skip"; ?>
--FILE--
<?php

$data = [
    [
        "key" => "010",
    ],
    [
        "key" => "10",
    ],
    [
        "key" => 10,
    ],
    [
        "key" => 0,
    ],
    [
        "key" => 8,
    ],
];

$jsonPath = new JsonPath();
$result = $jsonPath->find($data, "$[?(@.key==010)]");

echo "Assertion 1\n";
var_dump($result);
?>
--EXPECT--
Assertion 1
array(1) {
  [0]=>
  array(1) {
    ["key"]=>
    int(10)
  }
}
--XFAIL--
Requires more work on filter expressions