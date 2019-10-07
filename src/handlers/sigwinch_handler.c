
/* Surely not perfect handler. Never have the chance to test since
 * my constant environ is fullscreen terminals. Here we need the E
 * static variable to iterate over all the available editor instances */
private void sigwinch_handler (int sig) {
  (void) sig;
  ed_t *ed = E->head;
  while (ed) {
    Ed.set.screen_size (ed);
    win_t *w = Ed.get.win_head (ed);
    while (w) {
      Ed.readjust.win_size (ed, w);
      w = Ed.get.win_next (ed, w);
    }

    ed = Ed.get.next (ed);
  }
}

