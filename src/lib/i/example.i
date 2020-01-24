var flags = 0
var frame_zero = 0
var draw = 1
var donot_draw = 0

func libved_layout () {
  var ed = ed_new (2)

  var cwin = ed_get_current_win (ed)

  var buf = win_buf_init (cwin, frame_zero, flags)
  buf_init_fname (buf, "libved.c")
  buf_set_ftype (buf, "c")
  buf_set_row_idx (buf, 0)
  win_append_buf (cwin, buf)

  buf = win_buf_init (cwin, frame_zero, flags)

  buf_init_fname (buf, "libved.h")
  buf_set_ftype (buf, "c")
  buf_set_row_idx (buf, 0)
  win_append_buf (cwin, buf)

  buf = win_buf_init (cwin, frame_zero, flags)
  buf_init_fname (buf, "__libved.h")
  buf_set_ftype (buf, "c")
  buf_set_row_idx (buf, 0)
  win_append_buf (cwin, buf)

  cwin = ed_get_win_next (ed, cwin);

  buf = win_buf_init (cwin, frame_zero, flags)
  buf_init_fname (buf, "libved+.h")
  buf_set_ftype (buf, "c")
  buf_set_row_idx (buf, 0)
  win_append_buf (cwin, buf)

  buf = win_buf_init (cwin, frame_zero, flags)
  buf_init_fname (buf, "libved+.c")
  buf_set_ftype (buf, "c")
  buf_set_row_idx (buf, 0)
  win_append_buf (cwin, buf)

  ed = ed_new (2)
  cwin = ed_get_current_win (ed)

  buf = win_buf_init (cwin, frame_zero, flags)
  buf_init_fname (buf, "lib/i/i.c")
  buf_set_ftype (buf, "c")
  buf_set_row_idx (buf, 0)
  win_append_buf (cwin, buf)

  buf = win_buf_init (cwin, frame_zero, flags)
  buf_init_fname (buf, "lib/i/example.i")
  buf_set_ftype (buf, "i")
  buf_set_row_idx (buf, 0)
  win_append_buf (cwin, buf)

  cwin = ed_get_win_next (ed, cwin);

  buf = win_buf_init (cwin, frame_zero, flags)
  buf_init_fname (buf, "veda.c")
  buf_set_ftype (buf, "c")
  buf_set_row_idx (buf, 0)
  win_append_buf (cwin, buf)

  ed = set_ed_next ()

  cwin = ed_get_current_win (ed)

  buf = win_set_current_buf (cwin, 0, donot_draw)
  win_draw (cwin)
}

libved_layout ();
