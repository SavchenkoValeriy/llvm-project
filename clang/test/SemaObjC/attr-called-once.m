// RUN: %clang_cc1 -verify -fsyntax-only -fobjc-arc -fblocks %s

#define CALLED_ONCE __attribute__((called_once))

void test1(int x CALLED_ONCE);    // expected-error{{'called_once' attribute only applies to function-like parameters}}
void test2(double x CALLED_ONCE); // expected-error{{'called_once' attribute only applies to function-like parameters}}

void test3(void (*foo)() CALLED_ONCE);   // no-error
void test4(int (^foo)(int) CALLED_ONCE); // no-error
