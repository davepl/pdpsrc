#!/bin/ksh
# Native functional regression suite. Run: ./ksh mentec-tests.ksh
PATH=/bin:/usr/bin:/usr/ucb:/sbin:/usr/sbin
export PATH
SHELL_UNDER_TEST=$PWD/ksh
export SHELL_UNDER_TEST
work=$PWD/test-work.$$
mkdir "$work" || exit 1
pass=0
fail=0
function check {
    name=$1
    shift
    if "$@"; then
        print -r -- "PASS: $name"
        ((pass += 1))
    else
        print -r -- "FAIL: $name"
        ((fail += 1))
    fi
}
function eq { [[ $1 = "$2" ]]; }
function local_test { typeset x=inner; [[ $x = inner ]]; }
function factorial {
    typeset n=$1
    if ((n <= 1)); then print 1; else print $(( n * $(factorial $((n-1))) )); fi
}
check version test -n "$KSH_VERSION"
x=outer
check functions local_test
check local-scope eq "$x" outer
check recursion eq "$(factorial 6)" 720
# Exercise stack growth through nested shell function calls on Mentec.
function stack_walk {
    typeset n=$1
    if ((n)); then stack_walk $((n-1)); else print stack-ok; fi
}
check stack-growth eq "$(stack_walk 12)" stack-ok
check integer-arithmetic eq "$((12345 + 23456))" 35801
check long-arithmetic eq "$((1000000 + 2345678))" 3345678
check arithmetic-precedence eq "$((2+3*4))" 14
check command-substitution eq "$(print hello)" hello
check external-substitution eq "$(/bin/echo external)" external
check nested-substitution eq "$(print "$(print nested)")" nested
check backtick-substitution eq "`print oldstyle`" oldstyle
set -A a zero 'one two' three
check indexed-array eq "${a[1]}" 'one two'
check array-count eq "${#a[*]}" 3
set -- "${a[@]}"
check array-word-boundaries eq "$#:$2" '3:one two'
set -- 'a b' '' c
check positional-quoting eq "$#:$1:$2:$3" '3:a b::c'
shift
check shift eq "$#:$1:$2" '2::c'
x=prefix.middle.suffix
check trim-prefix eq "${x#*.}" middle.suffix
check trim-long-prefix eq "${x##*.}" suffix
check trim-suffix eq "${x%.*}" prefix.middle
check trim-long-suffix eq "${x%%.*}" prefix
check string-length eq "${#x}" 20
unset missing
check default-expansion eq "${missing:-fallback}" fallback
check assign-expansion eq "${missing:=assigned}" assigned
check assigned-variable eq "$missing" assigned
check brace-expansion eq "$(print x{a,b}y)" 'xay xby'
check tilde-expansion eq ~ "$HOME"
case abc.txt in *.txt) result=yes;; *) result=no;; esac
check case-pattern eq "$result" yes
sum=0
for n in 1 2 3 4 5; do ((sum += n)); done
check for-loop eq "$sum" 15
n=0
while ((n < 5)); do ((n += 1)); done
check while-loop eq "$n" 5
until ((n == 8)); do ((n += 1)); done
check until-loop eq "$n" 8
check pipeline eq "$(print pipeline | cat)" pipeline
check pipeline-status "$SHELL_UNDER_TEST" -c 'false | true'
check logical-lists "$SHELL_UNDER_TEST" -c 'false && exit 1; true || exit 1; exit 0'
check conditional-errexit "$SHELL_UNDER_TEST" -ec 'if false; then exit 1; fi; true'
"$SHELL_UNDER_TEST" -ec 'false; echo should-not-run' > "$work/errexit"
check errexit-status eq "$?" 1
check errexit-stopped test ! -s "$work/errexit"
print -r -- 'redirection data' > "$work/data"
print second >> "$work/data"
check redirection eq "$(cat < "$work/data")" "$(print 'redirection data'; print second)"
print -u2 err 2> "$work/stderr"
check stderr-redirection eq "$(cat "$work/stderr")" err
exec 3> "$work/fd"
print -u3 descriptor
exec 3>&-
check descriptor-redirection eq "$(cat "$work/fd")" descriptor
check heredoc eq "$(cat <<HERE
value=$sum
HERE
)" value=15
check literal-heredoc eq "$(cat <<'HERE'
$value
HERE
)" '$value'
IFS=: read first second <<HERE
left:right
HERE
check read eq "$first:$second" left:right
export KSH_TEST_ENV=environment
check environment eq "$(/bin/sh -c 'echo $KSH_TEST_ENV')" environment
x=parent
(x=child)
check subshell-isolation eq "$x" parent
check exit-trap eq "$("$SHELL_UNDER_TEST" -c "trap 'print exit-trap' 0; :")" exit-trap
check signal-trap eq "$("$SHELL_UNDER_TEST" -c 'trap "print signal-trap" USR1; kill -USR1 $$; :')" signal-trap
sleep 1 &
pid=$!
wait "$pid"
check background-wait eq "$?" 0
"$SHELL_UNDER_TEST" -c 'exit 7' &
pid=$!
wait "$pid"
check background-exit eq "$?" 7
print 'print sourced' > "$work/include"
check dot-source eq "$(. "$work/include")" sourced
print 'function loaded { print autoloaded; }' > "$work/loaded"
FPATH=$work
export FPATH
check autoload "$SHELL_UNDER_TEST" -c 'autoload loaded; result=$(loaded) || exit 1; test "$result" = autoloaded'
check getopts "$SHELL_UNDER_TEST" -c 'set -- -a -b value; getopts ab: o && test "$o" = a && getopts ab: o && test "$o:$OPTARG" = b:value'
check test-operators "$SHELL_UNDER_TEST" -c '[[ abc = a* && 7 -gt 3 && -n yes ]]'
check executable-path test -x "$SHELL_UNDER_TEST"
check umask "$SHELL_UNDER_TEST" -c 'umask 077; case $(umask) in 077|0077) exit 0;; *) exit 1;; esac'
check working-directory eq "$(cd "$work" && pwd)" "$work"
check exec eq "$("$SHELL_UNDER_TEST" -c 'exec /bin/echo executed')" executed
check syntax-error "$SHELL_UNDER_TEST" -c '"$SHELL_UNDER_TEST" -n -c "if then" >/dev/null 2>&1; test $? -ne 0'
# -c strings are parsed in full before execution in this pdksh version.
alias greet='print alias-ok'
check alias eq "$(greet)" alias-ok
check alias-eval eq "$("$SHELL_UNDER_TEST" -c "alias greet='print alias-ok'; eval greet")" alias-ok
check eval eq "$(eval 'print evaluated')" evaluated
check integer-variable "$SHELL_UNDER_TEST" -c 'typeset -i n=7; n=n*6; test "$n" = 42'
check readonly "$SHELL_UNDER_TEST" -c '(readonly x=yes; x=no) >/dev/null 2>&1; test $? -ne 0'
check extended-pattern "$SHELL_UNDER_TEST" -c '[[ abc = +(a|b|c) ]]'
check select-loop eq "$(print 2 | "$SHELL_UNDER_TEST" -c 'select choice in red blue; do print "$choice"; break; done' 2>/dev/null)" blue
(read line; print "reply:$line") |&
print -p request
read -p reply
check coprocess eq "$reply" reply:request
wait
# Repeated calls across all parser/evaluator/executor/variable overlays.
n=0
while ((n < 200)); do
    ((n += 1))
    x=$(print "$n" | cat)
    [[ $x = "$n" ]] || break
done
check overlay-stress eq "$n:$x" 200:200
print -r -- "RESULT: $pass passed, $fail failed"
rm -rf "$work"
((fail == 0))
