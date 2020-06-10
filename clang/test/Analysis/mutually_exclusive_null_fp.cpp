// RUN: %clang_analyze_cc1 -analyzer-checker=core,debug.ExprInspection -verify %s

// rdar://problem/56586853
// expected-no-diagnostics

void clang_analyzer_printState();
void clang_analyzer_explain(void *);

struct Data {
  int x;
  Data *data;
};

int compare(Data &a, Data &b) {
  Data *aData = a.data;
  Data *bData = b.data;

  // Covers the cases where both pointers are null as well as both pointing to the same buffer.
  if (aData == bData)
    return 0;

  if (aData && !bData)
    return 1;

  if (!aData && bData)
    return -1;

  return compare(*aData, *bData);
}

int foo(Data *a, Data *b) {
  if (a == b) {
    clang_analyzer_printState();
    if (!a) {
      clang_analyzer_printState();
      clang_analyzer_explain(a);
      return b->x;
    }
  }
  return 1;
}

Data *create();

int bar(Data *a, Data *b) {
  a = 0;
  b = create();
  if (a == b) {
    return a->x;
  }
  return 1;
}
