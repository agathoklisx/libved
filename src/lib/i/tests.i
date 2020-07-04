println "--== i tests beg: --== "
var n = 1

print "Ascii test - id: ", n

if 'a' is 97 & '\n' is 10 {
  println " OK"
}  else {
  println " NOTOK"
}

n = n+1
print "OK test    - id: ", n
if OK is 0 {
  println " OK"
}  else {
  println " NOTOK"
}

n = n+1
print "NOTOK test - id: ", n
if NOTOK is -1 {
  println " OK"
}  else {
  println " NOTOK"
}

n = n+1
print "false test - id: ", n
ifnot false {
  println " OK"
}  else {
  println " NOTOK"
}

n = n+1
print "true test  - id: ", n
ifnot true isnot 1 {
  println " OK"
}  else {
  println " NOTOK"
}

println "--== i tests end ==--"
