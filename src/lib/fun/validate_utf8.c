private int __file_validate_utf8__ (buf_t **thisp, char *fname) {
  (void) thisp;
  int retval = NOTOK;
  ifnot (File.exists (fname)) {
    Msg.send_fmt ($myed, COLOR_RED, "%s doesn't exists", fname);
    return retval;
  }

  ifnot (File.is_readable (fname)) {
    Msg.send_fmt ($myed, COLOR_RED, "%s is not readable", fname);
    return retval;
  }

  buf_t *this = Ed.get.scratch_buf ($myed);
  Buf.clear (this);

  vstr_t unused;
  retval = OK;
  File.readlines (fname, &unused, __validate_utf8_cb__, &retval);

  if (retval is NOTOK)
    Ed.scratch ($myed, thisp, NOT_AT_EOF);
  else
    Msg.send_fmt ($myed, COLOR_SUCCESS, "Validating %s ... OK", fname);

  return OK;
}

private int __validate_utf8__ (buf_t **thisp, rline_t *rl) {
  int range[2];
  int retval = Rline.get.range (rl, *thisp, range);
  if (NOTOK is retval) {
    range[0] = Buf.get.row.current_col_idx (*thisp);
    range[1] = range[0];
  }

  int count = range[1] - range[0] + 1;

  buf_t *this = Ed.get.scratch_buf ($myed);
  Buf.clear (this);

  vstr_t unused;
  bufiter_t *iter = Buf.iter.new (*thisp, range[0]);
  int i = 0;

  retval = OK;

  while (iter and i++ < count) {
    __validate_utf8_cb__ (&unused, iter->line->bytes, iter->line->num_bytes,
         iter->idx + 1, &retval);
    iter = Buf.iter.next (*thisp, iter);
  }

  Buf.iter.free (*thisp, iter);
  if (retval is NOTOK)
    Ed.scratch ($myed, thisp, NOT_AT_EOF);
  else
    Msg.send ($myed, COLOR_SUCCESS, "Validating text ... OK");

  return retval;
}

