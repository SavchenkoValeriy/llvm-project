// RUN: %clang_analyze_cc1 -verify %s \
// RUN:   -analyzer-checker=core,debug.ExprInspection \
// RUN:   -analyzer-config eagerly-assume=false

#define NULL (void *)0

void clang_analyzer_eval(int);
void clang_analyzer_printState();

int getInt();

void zeroImpliesEquality(int a, int b) {
  clang_analyzer_eval((a - b) == 0); // expected-warning{{UNKNOWN}}
  if (!(a != b)) {
    clang_analyzer_printState();
    clang_analyzer_eval(b != a);    // expected-warning{{FALSE}}
    clang_analyzer_eval(b == a);    // expected-warning{{TRUE}}
    clang_analyzer_eval(!(a != b)); // expected-warning{{TRUE}}
    clang_analyzer_eval(!(b == a)); // expected-warning{{FALSE}}
    return;
  }
  clang_analyzer_eval((a - b) == 0); // expected-warning{{FALSE}}
  clang_analyzer_eval(b == a);       // expected-warning{{FALSE}}
  clang_analyzer_eval(b != a);       // expected-warning{{TRUE}}
}

void zeroImpliesReversedEqual(int a, int b) {
  clang_analyzer_eval((b - a) == 0); // expected-warning{{UNKNOWN}}
  if ((b - a) == 0) {
    clang_analyzer_eval(b != a); // expected-warning{{FALSE}}
    clang_analyzer_eval(b == a); // expected-warning{{TRUE}}
    return;
  }
  clang_analyzer_eval((b - a) == 0); // expected-warning{{FALSE}}
  clang_analyzer_eval(b == a);       // expected-warning{{FALSE}}
  clang_analyzer_eval(b != a);       // expected-warning{{TRUE}}
}

void canonical_equal(int a, int b) {
  clang_analyzer_eval(a == b); // expected-warning{{UNKNOWN}}
  if (a == b) {
    clang_analyzer_eval(b == a); // expected-warning{{TRUE}}
    return;
  }
  clang_analyzer_eval(a == b); // expected-warning{{FALSE}}

  clang_analyzer_eval(b == a); // expected-warning{{FALSE}}
}

void test(int a, int b, int c, int d) {
  if (a == b && c == d) {
    if (a == 0 && b == d) {
      clang_analyzer_eval(c == 0); // expected-warning{{TRUE}}
    }
    c = 10;
    if (b == d) {
      clang_analyzer_eval(c == 10); // expected-warning{{TRUE}}
      clang_analyzer_eval(d == 10); // expected-warning{{UNKNOWN}}
      clang_analyzer_eval(b == a);  // expected-warning{{TRUE}}
      clang_analyzer_eval(a == d);  // expected-warning{{TRUE}}

      b = getInt();
      clang_analyzer_eval(a == d); // expected-warning{{TRUE}}
      clang_analyzer_eval(a == b); // expected-warning{{UNKNOWN}}
    }
  }

  if (a != b && b == c) {
    if (c == 42) {
      clang_analyzer_eval(a != 42); // expected-warning{{TRUE}}
      clang_analyzer_eval(b == 42); // expected-warning{{TRUE}}
    }
  }
}

void testPointers(int *a, int *b, int *c, int *d) {
  if (a == b && c == d) {
    if (a == NULL && b == d) {
      clang_analyzer_eval(c == NULL); // expected-warning{{TRUE}}
    }
  }

  if (a != b && b == c) {
    if (c == NULL) {
      clang_analyzer_eval(a != NULL); // expected-warning{{TRUE}}
    }
  }
}
