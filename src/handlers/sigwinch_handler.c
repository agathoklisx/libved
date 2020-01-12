
/* Surely not perfect handler. Never have the chance to test since
 * my constant environ is fullscreen terminals. 
 */
private void sigwinch_handler (int sig) {
  (void) sig;
  ed_t *ed = E(get.head);
  int cur_idx = E(get.current_idx);

  while (ed) {
    Ed.set.screen_size (ed);
    win_t *w = Ed.get.win_head (ed);
    while (w) {
      Ed.readjust.win_size (ed, w);
      w = Ed.get.win_next (ed, w);
    }

    ed = E(set.next);
  }

  E(set.current, cur_idx);
}

