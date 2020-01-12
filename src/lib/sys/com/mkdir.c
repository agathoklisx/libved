private int sys_mkdir (char *dir, mode_t mode, int verbose) {
  ed_t *ed = E(get.current);

  if (OK is mkdir (dir, mode)) {
    if (verbose) {
      struct stat st;
      if (NOTOK is stat (dir, &st)) {
        Msg.error (ed, "failed to stat directory, %s", Error.string (ed, errno));
        return NOTOK;
      }

      char mode_string[16];
      Vsys.stat.mode_to_string (mode_string, st.st_mode);
      char mode_oct[8]; snprintf (mode_oct, 8, "%o", st.st_mode);

      Msg.send_fmt (ed, COLOR_YELLOW, "created directory `%s', with mode: %s (%s)",
          dir, mode_oct + 1, mode_string);
    }

    return OK;
  }

  Msg.error (ed, "failed to create directory, (%s)", Error.string (ed, errno));
  return NOTOK;
}
