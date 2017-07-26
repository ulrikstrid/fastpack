// Initial check of the interoperability with the Spread module
function foo($$fpack_1) {let {a}  = $$fpack_1, b = $fpack.removeProps($$fpack_1, ["a"]); }

/* Babel: default parameters */
function foo(numVal) {}
function foo(numVal = 2) {}
function foo(numVal) {}
function foo(numVal) {}
function foo(numVal = 2) {}
function foo(numVal = 2) {}

/* Babel:  def-site-variance */
class C1 {}
function f() {}













// BUG: following 2 tests produce wrong output - eating out the last '}'
// due to bug in flow parser. Fix them when flow parser is updated
class C2 { 
class C3 { 


/* Babel: strip-array-types */
var a;
var a;
var a;
var a;
var a;
var a;

/* Babel: strip-call-properties */
var a;
var a;
var a;
var a;


/* Babel: strip-declare-exports */
















/* Babel: strip-declare-module */
// BUG: The following tests all didn't have the ';' in the end
// It looks like flow parser parses that incorrectly as well
// see if this can be fixed by the update






/* Babel: strip-declare-statements */












// BUG: next 4 tests have location issue again, declare remains
declare 
declare 
declare 
declare 


/* Babel: strip-interfaces-module-and-script */





class Foo  {}
class Foo2 extends Bar  {}
class Foo3 extends class Bar  {} {}
class Foo4 extends class Bar  {}  {}

/* Babel: strip-qualified-generic-type */
var a;
var a;
var a;
var a;

/* Babel: strip-string-literal-types */
function createElement(tagName) {}
function createElement(tagName) {}

/* Babel: strip-tuples */
var a = [];
var a = [foo];
var a = [123,];
var a = [123, "duck"];

/* Babel: strip-type-alias*/








/* Babel: strip-type-annotations */
function foo(numVal) {}
function foo(numVal) {}
function foo(numVal, strVal) {}
function foo(numVal, untypedVal) {}
function foo(untypedVal, numVal) {}
function foo(nullableNum) {}
function foo(callback) {}
function foo(callback) {}
function foo(callback) {}
function foo(callback) {}
function foo(callback) {}
function foo(){}
function foo() {}
function foo(){}
function foo(){}
function foo() {}
function foo() {}
function foo() {}
a = function() {};
a = { set fooProp(value) {} };
a = { set fooProp(value) {} };
a = { get fooProp(){} };
a = { id(x) {} };
a = { *id(x) {} };
a = { async id(x) {} };
a = { 123(x) {} };
class Foo {
  set fooProp(value) {}
}
class Foo2 {
  set fooProp(value) {}
}
class Foo3 {
  get fooProp() {}
}
var numVal = otherNumVal;
var a;
var a;
var a;
var a;
var a
var a
var a
var a
var a
// BUG: the next one raises parse error, check it after flow parser update
// var a: { ...any; ...{}|{p: void} };
var a;
var a;
var a;
var a = [1, 2, 3]
a = class Foo {}
a = class Foo extends Bar {}
class Foo4 {}
class Foo5 extends Bar {}
class Foo6 extends mixin(Bar) {}
class Foo7 {
  bar() { return 42; }
}
class Foo8 {
  "bar"() {}
}
function foo(requiredParam, optParam) {}
class Foo9 {
  
  
}
class Foo10 {
  
  
}
var x = 4;
class Array { concat(items) {}; }
var x = fn;
var x = Y;
var x = Y;
var {x} = { x: "hello" };
var {x} = { x: "hello" };
var [x] = [ "hello" ];
function foo({x}) {}
function foo([x]) {}
function foo(...rest) {}
(function (...rest) {});
((...rest) => rest);
var a
var a
var a
var a
var a
var a
var identity
var identity



import type from "foo";
import type2, { foo3 } from "bar";




import { V1} from "foo";

import { V4} from "foo";


import 'foo';