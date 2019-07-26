/*  Development Tracker  */

Implement:
  Command Line:
  :!! (last command, direct execution? or
  :!.  to mean also execution

  : 0x1f (CTRL('_')): to mean last component of previous command,
      (on repetition: scroll to history)
    (this usually mapped also and as Alt('.') on shells (for sure bash and zsh))
    ( we don't have ALT here)
     (but CTRL('/') is also 0x1f (decimal 31)

  :w[q]bd
    also:
      command chain:
        :-w bd w q
        :-w bd(self, int: 1) w q
          - that means an intented interpreter buffer
            - and since we went too far:
              a parser and executer
                this smells like an object system
                that it simply add functionality by request
                which then this: (the way it was requested)
                  becomes syntax of the system as-is (or
                  slightly developed) - if supposedly could
                  be kept an image of the last succesfull
                  request, this should produce the requested
                  result with success


  Normal Mode:
    Word movements (space separated)

  Escape on normal Mode:

Extend:
  - libxdiff.c [executable] providing unified diff


Search:
  - re-search (new pattern) (after an accepted (with a way) result)
