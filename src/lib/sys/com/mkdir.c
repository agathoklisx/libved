private int sys_mkdir (char *dir, mode_t mode, int verbose) {
  if (OK is mkdir (dir, mode)) {
    if (verbose) {
      struct stat st;
      if (NOTOK is stat (dir, &st)) {
        Msg.error ($myed, "failed to stat directory, %s", Error.string ($myed, errno));
        return NOTOK;
      }

      char mode_string[16];
      Vsys.stat.mode_to_string (mode_string, st.st_mode);
      char mode_oct[8]; snprintf (mode_oct, 8, "%o", st.st_mode);

      Msg.send_fmt ($myed, COLOR_YELLOW, "created directory `%s', with mode: %s (%s)",
          dir, mode_oct + 1, mode_string);
    }

    return OK;
  }

  Msg.error ($myed, "failed to create directory, (%s)", Error.string ($myed, errno));
  return NOTOK;
}

