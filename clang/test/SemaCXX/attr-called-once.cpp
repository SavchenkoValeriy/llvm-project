// RUN: %clang_cc1 -std=c++11 -verify -fsyntax-only %s

#define CALLED_ONCE __attribute__((called_once))

struct A {
  int x;
};

class B;

class B {};

void test1(int x CALLED_ONCE);    // expected-error{{'called_once' attribute only applies to function-like parameters}}
void test2(double x CALLED_ONCE); // expected-error{{'called_once' attribute only applies to function-like parameters}}
void test3(A a CALLED_ONCE);      // expected-error{{'called_once' attribute only applies to function-like parameters}}
void test4(A *a CALLED_ONCE);     // expected-error{{'called_once' attribute only applies to function-like parameters}}
void test5(B &a CALLED_ONCE);     // expected-error{{'called_once' attribute only applies to function-like parameters}}

class C {
public:
  void operator()();
};

class D;

class E;

void test6(void (*foo)() CALLED_ONCE);    // no-error
void test7(void (&foo)() CALLED_ONCE);    // no-error
void test8(void (C::*foo)() CALLED_ONCE); // no-error
void test9(C functor CALLED_ONCE);        // no-error
void test10(C &functor CALLED_ONCE);      // no-error
void test11(C *functor CALLED_ONCE);      // no-error
void test12(D *functor CALLED_ONCE);      // no-error
void test13(E &functor CALLED_ONCE);      // no-error

class E {
public:
  void operator()();
};
