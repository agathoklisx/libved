println "==-- i tests beg  --== "
var n = 1

print "Character literal: "

if 'a' is 97 & '\t' is 9 & 'α' is 945 {
  println " OK"
}  else {
  println " NOTOK at id: ", n
}

n = n+1
print "OK test          : "
if OK is 0 {
  println " OK"
}  else {
  println " NOTOK at id: ", n
}

n = n+1
print "NOTOK test       : "
if NOTOK is -1 {
  println " OK"
}  else {
  println " NOTOK at id: ", n
}

n = n+1
print "false test       : "
ifnot false {
  println " OK"
}  else {
  println " NOTOK at id: ", n
}

n = n+1
print "true test        : "
ifnot true isnot 1 {
  println " OK"
}  else {
  println " NOTOK at id: ", n
}

println "==-- i tests end  --=="
