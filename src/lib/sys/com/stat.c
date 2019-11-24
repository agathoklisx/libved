private string_t *__readlink__ (char *obj) {
  string_t *link = String.new (PATH_MAX);

  int loops = 0;
readthelink:
  link->num_bytes = readlink (obj, link->bytes, link->mem_size);
  if (NOTOK is (ssize_t) link->num_bytes)
    return link;

  if (link->num_bytes is link->mem_size and loops++ is 0) {
    String.reallocate (link, (link->mem_size / 2));
    goto readthelink;
  }

  link->bytes[link->num_bytes] = '\0'; // readlink() does not append the nullbyte
  return link;
}

private int sys_stat (buf_t **thisp, char *obj) {
  struct stat st;
  if (NOTOK is lstat (obj, &st)) {
    Msg.error ($myed, "failed to lstat() file, %s", Error.string ($myed, errno));
    return NOTOK;
  }

  Ed.append.toscratch ($myed, CLEAR, "==- stat output -==");

  int islink = S_ISLNK (st.st_mode);
  string_t *link = NULL;

  if (islink) {
    link = __readlink__ (obj);
    if (NOTOK is (ssize_t) link->num_bytes) {
      Msg.error ($myed, "readlink(): %s", Error.string ($myed, errno));
      Ed.append.toscratch_fmt ($myed, DONOT_CLEAR, "  File: %s", obj);
    } else
      Ed.append.toscratch_fmt ($myed, DONOT_CLEAR, "  File: %s -> %s", obj, link->bytes);
  } else
    Ed.append.toscratch_fmt ($myed, DONOT_CLEAR, "  File: %s", obj);

theoutput:
  Ed.append.toscratch_fmt ($myed, DONOT_CLEAR, "  Size: %ld,  Blocks: %ld,  I/O Block: %ld",
      st.st_size, st.st_blocks, st.st_blksize);
  Ed.append.toscratch_fmt ($myed, DONOT_CLEAR, "Device: %ld,  Inode: %ld,  Links: %d",
      st.st_dev, st.st_ino, st.st_nlink);

  char mode_string[16];
  Vsys.stat.mode_to_string (mode_string, st.st_mode);
  char mode_oct[8]; snprintf (mode_oct, 8, "%o", st.st_mode);
  struct passwd *pswd = getpwuid (st.st_uid);
  struct group *grp = getgrgid (st.st_gid);
  Ed.append.toscratch_fmt ($myed, DONOT_CLEAR,
  	"Access: (%s/%s), Uid: (%ld / %s), Gid: (%d / %s)\n",
     mode_oct+2, mode_string, st.st_uid,
    (NULL is pswd ? "NONE" : pswd->pw_name), st.st_gid,
    (NULL is grp  ? "NONE" : grp->gr_name));
  time_t atm = (long int) st.st_atime;
  Ed.append.toscratch_fmt ($myed, DONOT_CLEAR, "       Last Access: %s", Cstring.trim.end (ctime (&atm), '\n'));
  time_t mtm = (long int) st.st_mtime;
  Ed.append.toscratch_fmt ($myed, DONOT_CLEAR, " Last Modification: %s", Cstring.trim.end (ctime (&mtm), '\n'));
  time_t ctm = (long int) st.st_ctime;
  Ed.append.toscratch_fmt ($myed, DONOT_CLEAR, "Last Status Change: %s\n", Cstring.trim.end (ctime (&ctm), '\n'));

  if (islink and NULL isnot link) {
    islink = 0;
    ifnot (Path.is_absolute (link->bytes)) {
      char *dname = Path.dirname (obj);
      String.prepend_fmt (link, "%s/", dname);
      free (dname);
    }
    obj = link->bytes;
    Ed.append.toscratch ($myed, DONOT_CLEAR, "==- Link info -==");
    Ed.append.toscratch_fmt ($myed, DONOT_CLEAR, "  File: %s", obj);
    if (OK is stat (link->bytes, &st)) goto theoutput;
  }
  String.free (link);
  Ed.scratch ($myed, thisp, NOT_AT_EOF);
  return OK;
}

