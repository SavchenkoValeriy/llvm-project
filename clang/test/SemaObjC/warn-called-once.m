// RUN: %clang_cc1 -verify -fsyntax-only -fobjc-arc -fblocks %s

#define CALLED_ONCE __attribute__((called_once))

void escape(void (^callback)(void));
void escape_void(void *);
void indirect_call(void (^callback)(void) CALLED_ONCE);

void double_call_one_block(void (^callback)(void) CALLED_ONCE) {
  callback(); // expected-note{{previous call is here}}
  callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
}

void multiple_call_one_block(void (^callback)(void) CALLED_ONCE) {
  // We don't really need to repeat the same warning for the same parameter.
  callback(); // no-warning
  callback(); // no-warning
  callback(); // no-warning
  callback(); // expected-note{{previous call is here}}
  callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
}

void double_call_branching_1(int cond, void (^callback)(void) CALLED_ONCE) {
  if (cond) {
    callback(); // expected-note{{previous call is here}}
  } else {
    cond += 42;
  }
  callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
}

void double_call_branching_2(int cond, void (^callback)(void) CALLED_ONCE) {
  callback(); // expected-note{{previous call is here}}

  if (cond) {
    callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
  } else {
    cond += 42;
  }
}

void double_call_branching_3(int cond, void (^callback)(void) CALLED_ONCE) {
  if (cond) {
    callback();
  } else {
    callback();
  }
  // no-warning
}

void double_call_branching_4(int cond1, int cond2, void (^callback)(void) CALLED_ONCE) {
  if (cond1) {
    cond2 = !cond2;
  } else {
    callback(); // expected-note{{previous call is here}}
  }

  if (cond2) {
    callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
  }
}

void double_call_loop(int counter, void (^callback)(void) CALLED_ONCE) {
  while (counter > 0) {
    counter--;
    // Both note and warning are on the same line, which is a common situation
    // in loops.
    callback(); // expected-note{{previous call is here}}
    // expected-warning@-1{{'callback' parameter marked 'called_once' is called twice}}
  }
}

void never_called_trivial(void (^callback)(void) CALLED_ONCE) {
  // expected-warning@-1{{'callback' parameter marked 'called_once' is never called}}
}

int never_called_branching(int x, void (^callback)(void) CALLED_ONCE) {
  // expected-warning@-1{{'callback' parameter marked 'called_once' is never called}}
  x -= 42;

  if (x == 10) {
    return 0;
  }

  return x + 15;
}

void escaped_one_block_1(void (^callback)(void) CALLED_ONCE) {
  escape(callback); // no-warning
}

void escaped_one_block_2(void (^callback)(void) CALLED_ONCE) {
  escape(callback); // no-warning
  callback();
}

void escaped_one_path_1(int cond, void (^callback)(void) CALLED_ONCE) {
  if (cond) {
    escape(callback); // no-warning
  } else {
    callback();
  }
}

void escaped_one_path_2(int cond, void (^callback)(void) CALLED_ONCE) {
  if (cond) {
    escape(callback); // no-warning
  }

  callback();
}

void escaped_one_path_3(int cond, void (^callback)(void) CALLED_ONCE) {
  // expected-warning@-1{{'callback' parameter marked 'called_once' is never called}}
  if (cond) {
    // Even though 'callback' escpapes here, it doesn't do it on every path to
    // the exit.  For this reason, we assume that it was not called here.
    escape(callback);
  }
}

void escape_in_between_1(void (^callback)(void) CALLED_ONCE) {
  callback(); // expected-note{{previous call is here}}
  escape(callback);
  callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
}

void escape_in_between_2(int cond, void (^callback)(void) CALLED_ONCE) {
  callback(); // expected-note{{previous call is here}}
  if (cond) {
    escape(callback);
  }
  callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
}

void escape_in_between_3(int cond, void (^callback)(void) CALLED_ONCE) {
  callback(); // expected-note{{previous call is here}}

  if (cond) {
    escape(callback);
  } else {
    escape_void((__bridge void *)callback);
  }

  callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
}

void escaped_as_void_ptr(void (^callback)(void) CALLED_ONCE) {
  escape_void((__bridge void *)callback); // no-warning
}

void indirect_call_no_warning_1(void (^callback)(void) CALLED_ONCE) {
  indirect_call(callback); // no-warning
}

void indirect_call_no_warning_2(int cond, void (^callback)(void) CALLED_ONCE) {
  if (cond) {
    indirect_call(callback);
  } else {
    callback();
  }
  // no-warning
}

void indirect_call_double_call(void (^callback)(void) CALLED_ONCE) {
  indirect_call(callback); // expected-note{{previous call is here}}
  callback();              // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
}

void indirect_call_within_direct_call(void (^callback)(void) CALLED_ONCE,
                                      void (^meta)(void (^param)(void) CALLED_ONCE) CALLED_ONCE) {
  // TODO: Report warning for 'callback'.
  //       At the moment, it is not possible to access 'called_once' attribute from the type
  //       alone when there is no actual declaration of the marked parameter.
  meta(callback);
  callback();
  // no-warning
}

void block_call_1(void (^callback)(void) CALLED_ONCE) {
  indirect_call(^{
    callback();
  });         // expected-note@-2{{previous call is here}}
  callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
}

void block_call_2(void (^callback)(void) CALLED_ONCE) {
  // Even though the block escapes we consider it a call.
  escape(^{
    callback();
  });         // expected-note@-2{{previous call is here}}
  callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
}

void block_call_3(int cond, void (^callback)(void) CALLED_ONCE) {
  ^{
    if (cond) {
      callback(); // expected-note{{previous call is here}}
    }
    callback(); // expected-warning{{'callback' parameter marked 'called_once' is called twice}}
  }();          // no-warning
}

void block_call_4(int cond, void (^callback)(void) CALLED_ONCE) {
  ^{ // expected-warning{{captured 'callback' parameter marked 'called_once' is never called}}
    if (cond) {
      escape(callback);
    }
  }();
}

void block_call_5(void (^outer)(void) CALLED_ONCE) {
  ^(void (^inner)(void) CALLED_ONCE){
    // expected-warning@-1{{'inner' parameter marked 'called_once' is never called}}
  }(outer);
}

void block_with_called_once(void (^outer)(void) CALLED_ONCE) {
  escape_void((__bridge void *)^(void (^inner)(void) CALLED_ONCE) {
    inner(); // expected-note{{previous call is here}}
    inner(); // expected-warning{{'inner' parameter marked 'called_once' is called twice}}
  });
  outer(); // expected-note{{previous call is here}}
  outer(); // expected-warning{{'outer' parameter marked 'called_once' is called twice}}
}

void never_called_one_exit(int cond, void (^callback)(void) CALLED_ONCE) {
  if (!cond) // expected-warning{{'callback' parameter marked 'called_once' is never called when taking true branch}}
    return;

  callback();
}

void never_called_if_then_1(int cond, void (^callback)(void) CALLED_ONCE) {
  if (cond) { // expected-warning{{'callback' parameter marked 'called_once' is never called when taking true branch}}
  } else {
    callback();
  }
}

void never_called_if_then_2(int cond, void (^callback)(void) CALLED_ONCE) {
  if (cond) { // expected-warning{{'callback' parameter marked 'called_once' is never called when taking true branch}}
    // This way the first statement in the basic block is different from
    // the first statement in the compound statement
    (void)cond;
  } else {
    callback();
  }
}

void never_called_if_else_1(int cond, void (^callback)(void) CALLED_ONCE) {
  if (cond) { // expected-warning{{'callback' parameter marked 'called_once' is never called when taking false branch}}
    callback();
  } else {
  }
}

void never_called_if_else_2(int cond, void (^callback)(void) CALLED_ONCE) {
  if (cond) { // expected-warning{{'callback' parameter marked 'called_once' is never called when taking false branch}}
    callback();
  }
}

void never_called_two_ifs(int cond1, int cond2, void (^callback)(void) CALLED_ONCE) {
  if (cond1) { // expected-warning{{'callback' parameter marked 'called_once' is never called when taking false branch}}
    if (cond2) { // expected-warning{{'callback' parameter marked 'called_once' is never called when taking true branch}}
      return;
    }
    callback();
  }
}

void never_called_ternary_then(int cond, void (^other)(void), void (^callback)(void) CALLED_ONCE) {
  return cond ? // expected-warning{{'callback' parameter marked 'called_once' is never called when taking true branch}}
    other() :
    callback();
}

void never_called_for_false(int size, void (^callback)(void) CALLED_ONCE) {
  for (int i = 0; i < size; ++i) {
    // expected-warning@-1{{'callback' parameter marked 'called_once' is never called when skipping the loop}}
    callback();
    break;
  }
}

void never_called_for_true(int size, void (^callback)(void) CALLED_ONCE) {
  for (int i = 0; i < size; ++i) {
    // expected-warning@-1{{'callback' parameter marked 'called_once' is never called when entering the loop}}
    return;
  }
  callback();
}

void never_called_while_false(int cond, void (^callback)(void) CALLED_ONCE) {
  while (cond) { // expected-warning{{'callback' parameter marked 'called_once' is never called when skipping the loop}}
    callback();
    break;
  }
}

void never_called_while_true(int cond, void (^callback)(void) CALLED_ONCE) {
  while (cond) { // expected-warning{{'callback' parameter marked 'called_once' is never called when entering the loop}}
    return;
  }
  callback();
}

void never_called_switch_case(int cond, void (^callback)(void) CALLED_ONCE) {
  switch (cond) {
  case 1:
    callback();
    break;
  case 2:
    callback();
    break;
  case 3: // expected-warning{{'callback' parameter marked 'called_once' is never called when handling this case}}
    break;
  default:
    callback();
    break;
  }
}

void never_called_switch_default(int cond, void (^callback)(void) CALLED_ONCE) {
  switch (cond) {
  case 1:
    callback();
    break;
  case 2:
    callback();
    break;
  default: // expected-warning{{'callback' parameter marked 'called_once' is never called when handling this case}}
    break;
  }
}

void never_called_switch_two_cases(int cond, void (^callback)(void) CALLED_ONCE) {
  switch (cond) {
  case 1: // expected-warning{{'callback' parameter marked 'called_once' is never called when handling this case}}
    break;
  case 2: // expected-warning{{'callback' parameter marked 'called_once' is never called when handling this case}}
    break;
  default:
    callback();
    break;
  }
}

void never_called_switch_none(int cond, void (^callback)(void) CALLED_ONCE) {
  switch (cond) { // expected-warning{{'callback' parameter marked 'called_once' is never called when none of the cases applies}}
  case 1:
    callback();
    break;
  case 2:
    callback();
    break;
  }
}

enum YesNoOrMaybe {
  YES,
  NO,
  MAYBE
};

void exhaustive_switch(enum YesNoOrMaybe cond, void (^callback)(void) CALLED_ONCE) {
  switch (cond) {
  case YES:
    callback();
    break;
  case NO:
    callback();
    break;
  case MAYBE:
    callback();
    break;
  }
  // no-warning
}
